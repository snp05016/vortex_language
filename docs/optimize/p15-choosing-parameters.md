# P15. Choosing parameters: models or search

<p class="page-intro">Proving a transformation legal only bounds a space of numbers; it never picks one. This chapter builds an analytic model and a search harness for the same small problem, compares what each one costs to run, and states the one rule every candidate in that space must already obey before its cost is even worth asking about.</p>

<p class="vx-meta" markdown="1">Level: Advanced · Reading time: about 30 minutes · Builds on: [P12. Anatomy of a fast GEMM](p12-fast-gemm.md), [P14. Algorithms and schedules](p14-algorithms-and-schedules.md)</p>

???+ remember "Before you start, remember"

    ??? question "What does a 'missed' optimization remark report, and what is it for?"

        An attempted transformation that legality or profitability blocked, with the reason. It lets a reader, or a later pass, see why a choice was not made instead of guessing.

        Introduced in [O1. The optimizer's contract](o1-optimizer-contract.md#remarks-the-optimizers-report).

    ??? question "What condition must a loop nest satisfy before its iterations can be regrouped into tiles at all?"

        The band being tiled must be fully permutable: every dependence is lexicographically positive, and within the band each one is already carried by an outer loop or free of negative entries.

        Introduced in [P7. Loop transformations](p7-loop-transformations.md#strip-mining-and-tiling).

    ??? question "May a Vortex compiler fuse, reorder or widen any `f32` or `f64` operation to make it faster?"

        No. Each operation is one IEEE 754 result, rounded once to nearest with ties to even, in the written order: no fused multiply-add, no reassociation, no reordering, no wider intermediate format.

        Introduced in [Numbers, decision 56](../decisions/numbers.md#d56).

    ??? question "What does passing `c` as `&mut` let the compiler assume about `a` and `b` in the matmul kernel?"

        That storage reached through `c` is reachable through no other parameter of the same call, so a store to `c` cannot change what `a` or `b` read; the compiler may keep a loaded value across it and needs no runtime alias check.

        Introduced in [O9. Memory: alias analysis and MemorySSA](o9-alias-analysis.md#promises-the-front-end-writes-down).

!!! goals "In this chapter"

    - Explain why proving a transformation legal never by itself decides the numbers a compiler should use.
    - Compare an analytic cost model against grid search and random search on one problem, and read what each strategy actually costs to run.
    - Build a tiny model-and-search harness and see when the model's shortcut lands on the same answer a full search finds, and when a bounded random search misses it.
    - Recognize which parameters of a blocked kernel a compiler may search freely, and which must respect the rule against reordering a floating-point reduction.
    - Design a cost model and a small tuner for your own compiler's tile parameters, and decide what each one must log.

## One tile size, three ways to find it

Take a small version of a problem [P7](p7-loop-transformations.md#strip-mining-and-tiling) already made legal: a loop nest may be blocked, its iterations regrouped so that a chunk of reused data stays resident instead of being reloaded. Blocking a matrix-vector product `y = A * x` over a row-block size `bi` and a reduction-block size `bk` is exactly that move, applied to a toy that is not the Vortex kernel. Once the block sizes are picked, `x` is streamed through memory once per row-block instead of once per row, and every choice of `bi` and `bk` that respects a resident-data budget gives the same answer: only the row order changes, never a sum's terms or their order.

That is the legal, meaning-preserving space [P7](p7-loop-transformations.md#strip-mining-and-tiling) hands over. It says nothing about which `bi` and `bk` to use. The first example scores every pair on a small grid with one cost function: reusing `x` across more rows lowers memory traffic, and switching to a new tile costs a fixed overhead, so cost falls as `bi` grows but only up to whatever `bk` the resident-data budget still allows. Three strategies answer the same question:

--8<-- "includes/examples/optimize/p15-choosing-parameters/tile_search.cpp.md"

Grid search tries all 81 pairs on the candidate grid and finds the true best, `bi=256, bk=256`. Random search, run with a fixed seed so the result is repeatable, tries 12 pairs and lands on a nearby but worse one. The model tries only 7 pairs, one per candidate `bi`, and still finds the same answer as the grid: for each `bi` it reasons that the largest feasible `bk` is always at least as good as a smaller one, since nothing in the cost function makes a smaller `bk` cheaper, so it never has to try more than one `bk` per `bi`.

Real matmul libraries make the same choice at real scale. [P12](p12-fast-gemm.md) names the parameters: `mc`, `kc` and `nc` size the packed blocks that Goto's algorithm and BLIS move through the cache hierarchy, and `mr` and `nr` size the micro-kernel tile that lives in registers.[^goto08] Nothing about proving that blocking is legal picks any of those five numbers. This chapter is about how they get picked.

## Two families: reason, or try

An **analytic cost model** computes a parameter's value from known facts about the problem and the target, by a formula or a short chain of reasoning, the way the toy model above scans one free variable instead of a grid. **Empirical search** tries candidate values and scores each one, by measurement or by the same kind of proxy cost function, without needing to understand why one value beats another.

ATLAS, one of the first widely used self-tuning libraries, took the second route at install time: it generated many versions of a kernel from a code template and timed each one on the machine it was being installed on, keeping the fastest.[^atlas98] That is empirical search in its most direct form, no model of the machine required, just a stopwatch and a lot of candidates.

A later study asked whether the search was doing real work. Yotov and coauthors built a model-driven version of ATLAS that computed its parameters from equations describing the machine's cache and register file, instead of timing generated variants, and found that it performed comparably to ATLAS's own exhaustive search.[^yotov05] BLIS pushes the same idea further: its block sizes are derived from a small set of equations over the target's cache and TLB capacities, with no search step at all.[^low16] Both results say the same thing the toy example just showed on a much smaller scale: when a cost function is well enough understood, a model can reach a search's answer for a fraction of the work.

## A model that skips most of the grid

The model function above did not examine every `(bi, bk)` pair; it examined the cost function's shape. Growing `bk` at a fixed `bi` only ever lowers the overhead term, and nothing in the toy's cost function makes a smaller `bk` cheaper, so the only thing worth choosing per `bi` is the largest `bk` the resident-data budget still allows. That turns a two-parameter search into a one-parameter scan: try each `bi`, take the best feasible `bk` for it, and compare seven numbers instead of eighty-one.

<figure class="vx-figure">
<svg viewBox="0 0 760 380" role="img" aria-label="A heat map of the toy's search space over row block and reduction block, with the analytic model's pick marked" aria-describedby="p15-f1-desc">
<title id="p15-f1-title">The toy's search space, and where the model and the grid search agree</title>
<desc id="p15-f1-desc">A nine by nine grid. Columns are bk from 4 to 1024, rows are bi from 4 to 1024, both powers of two. Cells where bi plus bk exceeds the 512-element budget are drawn dashed and marked infeasible: this rules out every cell in the last two columns (bk 512 and 1024) and the last two rows (bi 512 and 1024). Among the feasible cells, cost falls toward the bottom right. The row bi=128 and the whole row bi=256 are drawn in a lighter accent, meaning their cost is within twice the best found. One cell, bi=256 and bk=256, is filled solid: the lowest cost on the grid, and the cell both the model and the grid search return.</desc>
<text class="vx-text-muted" x="369" y="16" text-anchor="middle">bk (reduction block)</text>
<text class="vx-mono" x="121" y="36" text-anchor="middle">4</text>
<text class="vx-mono" x="183" y="36" text-anchor="middle">8</text>
<text class="vx-mono" x="245" y="36" text-anchor="middle">16</text>
<text class="vx-mono" x="307" y="36" text-anchor="middle">32</text>
<text class="vx-mono" x="369" y="36" text-anchor="middle">64</text>
<text class="vx-mono" x="431" y="36" text-anchor="middle">128</text>
<text class="vx-mono" x="493" y="36" text-anchor="middle">256</text>
<text class="vx-mono" x="555" y="36" text-anchor="middle">512</text>
<text class="vx-mono" x="617" y="36" text-anchor="middle">1024</text>
<text class="vx-text-muted" x="16" y="181" text-anchor="middle" transform="rotate(-90 16 181)">bi (row block)</text>
<text class="vx-mono" x="80" y="65" text-anchor="end">4</text>
<rect class="vx-box" x="90" y="46" width="58" height="26" rx="2"/>
<rect class="vx-box" x="152" y="46" width="58" height="26" rx="2"/>
<rect class="vx-box" x="214" y="46" width="58" height="26" rx="2"/>
<rect class="vx-box" x="276" y="46" width="58" height="26" rx="2"/>
<rect class="vx-box" x="338" y="46" width="58" height="26" rx="2"/>
<rect class="vx-box" x="400" y="46" width="58" height="26" rx="2"/>
<rect class="vx-box" x="462" y="46" width="58" height="26" rx="2"/>
<rect class="vx-box-bad" x="524" y="46" width="58" height="26" rx="2"/>
<rect class="vx-box-bad" x="586" y="46" width="58" height="26" rx="2"/>
<text class="vx-mono" x="80" y="95" text-anchor="end">8</text>
<rect class="vx-box" x="90" y="76" width="58" height="26" rx="2"/>
<rect class="vx-box" x="152" y="76" width="58" height="26" rx="2"/>
<rect class="vx-box" x="214" y="76" width="58" height="26" rx="2"/>
<rect class="vx-box" x="276" y="76" width="58" height="26" rx="2"/>
<rect class="vx-box" x="338" y="76" width="58" height="26" rx="2"/>
<rect class="vx-box" x="400" y="76" width="58" height="26" rx="2"/>
<rect class="vx-box" x="462" y="76" width="58" height="26" rx="2"/>
<rect class="vx-box-bad" x="524" y="76" width="58" height="26" rx="2"/>
<rect class="vx-box-bad" x="586" y="76" width="58" height="26" rx="2"/>
<text class="vx-mono" x="80" y="125" text-anchor="end">16</text>
<rect class="vx-box" x="90" y="106" width="58" height="26" rx="2"/>
<rect class="vx-box" x="152" y="106" width="58" height="26" rx="2"/>
<rect class="vx-box" x="214" y="106" width="58" height="26" rx="2"/>
<rect class="vx-box" x="276" y="106" width="58" height="26" rx="2"/>
<rect class="vx-box" x="338" y="106" width="58" height="26" rx="2"/>
<rect class="vx-box" x="400" y="106" width="58" height="26" rx="2"/>
<rect class="vx-box" x="462" y="106" width="58" height="26" rx="2"/>
<rect class="vx-box-bad" x="524" y="106" width="58" height="26" rx="2"/>
<rect class="vx-box-bad" x="586" y="106" width="58" height="26" rx="2"/>
<text class="vx-mono" x="80" y="155" text-anchor="end">32</text>
<rect class="vx-box" x="90" y="136" width="58" height="26" rx="2"/>
<rect class="vx-box" x="152" y="136" width="58" height="26" rx="2"/>
<rect class="vx-box" x="214" y="136" width="58" height="26" rx="2"/>
<rect class="vx-box" x="276" y="136" width="58" height="26" rx="2"/>
<rect class="vx-box" x="338" y="136" width="58" height="26" rx="2"/>
<rect class="vx-box" x="400" y="136" width="58" height="26" rx="2"/>
<rect class="vx-box" x="462" y="136" width="58" height="26" rx="2"/>
<rect class="vx-box-bad" x="524" y="136" width="58" height="26" rx="2"/>
<rect class="vx-box-bad" x="586" y="136" width="58" height="26" rx="2"/>
<text class="vx-mono" x="80" y="185" text-anchor="end">64</text>
<rect class="vx-box" x="90" y="166" width="58" height="26" rx="2"/>
<rect class="vx-box" x="152" y="166" width="58" height="26" rx="2"/>
<rect class="vx-box" x="214" y="166" width="58" height="26" rx="2"/>
<rect class="vx-box" x="276" y="166" width="58" height="26" rx="2"/>
<rect class="vx-box" x="338" y="166" width="58" height="26" rx="2"/>
<rect class="vx-box" x="400" y="166" width="58" height="26" rx="2"/>
<rect class="vx-box" x="462" y="166" width="58" height="26" rx="2"/>
<rect class="vx-box-bad" x="524" y="166" width="58" height="26" rx="2"/>
<rect class="vx-box-bad" x="586" y="166" width="58" height="26" rx="2"/>
<text class="vx-mono" x="80" y="215" text-anchor="end">128</text>
<rect class="vx-box" x="90" y="196" width="58" height="26" rx="2"/>
<rect class="vx-box" x="152" y="196" width="58" height="26" rx="2"/>
<rect class="vx-box" x="214" y="196" width="58" height="26" rx="2"/>
<rect class="vx-box" x="276" y="196" width="58" height="26" rx="2"/>
<rect class="vx-box" x="338" y="196" width="58" height="26" rx="2"/>
<rect class="vx-box" x="400" y="196" width="58" height="26" rx="2"/>
<rect class="vx-box-accent" x="462" y="196" width="58" height="26" rx="2"/>
<rect class="vx-box-bad" x="524" y="196" width="58" height="26" rx="2"/>
<rect class="vx-box-bad" x="586" y="196" width="58" height="26" rx="2"/>
<text class="vx-mono" x="80" y="245" text-anchor="end">256</text>
<rect class="vx-box-accent" x="90" y="226" width="58" height="26" rx="2"/>
<rect class="vx-box-accent" x="152" y="226" width="58" height="26" rx="2"/>
<rect class="vx-box-accent" x="214" y="226" width="58" height="26" rx="2"/>
<rect class="vx-box-accent" x="276" y="226" width="58" height="26" rx="2"/>
<rect class="vx-box-accent" x="338" y="226" width="58" height="26" rx="2"/>
<rect class="vx-box-accent" x="400" y="226" width="58" height="26" rx="2"/>
<rect class="vx-cell-on" x="462" y="226" width="58" height="26" rx="2"/>
<rect class="vx-box-bad" x="524" y="226" width="58" height="26" rx="2"/>
<rect class="vx-box-bad" x="586" y="226" width="58" height="26" rx="2"/>
<text class="vx-mono" x="80" y="275" text-anchor="end">512</text>
<rect class="vx-box-bad" x="90" y="256" width="58" height="26" rx="2"/>
<rect class="vx-box-bad" x="152" y="256" width="58" height="26" rx="2"/>
<rect class="vx-box-bad" x="214" y="256" width="58" height="26" rx="2"/>
<rect class="vx-box-bad" x="276" y="256" width="58" height="26" rx="2"/>
<rect class="vx-box-bad" x="338" y="256" width="58" height="26" rx="2"/>
<rect class="vx-box-bad" x="400" y="256" width="58" height="26" rx="2"/>
<rect class="vx-box-bad" x="462" y="256" width="58" height="26" rx="2"/>
<rect class="vx-box-bad" x="524" y="256" width="58" height="26" rx="2"/>
<rect class="vx-box-bad" x="586" y="256" width="58" height="26" rx="2"/>
<text class="vx-mono" x="80" y="305" text-anchor="end">1024</text>
<rect class="vx-box-bad" x="90" y="286" width="58" height="26" rx="2"/>
<rect class="vx-box-bad" x="152" y="286" width="58" height="26" rx="2"/>
<rect class="vx-box-bad" x="214" y="286" width="58" height="26" rx="2"/>
<rect class="vx-box-bad" x="276" y="286" width="58" height="26" rx="2"/>
<rect class="vx-box-bad" x="338" y="286" width="58" height="26" rx="2"/>
<rect class="vx-box-bad" x="400" y="286" width="58" height="26" rx="2"/>
<rect class="vx-box-bad" x="462" y="286" width="58" height="26" rx="2"/>
<rect class="vx-box-bad" x="524" y="286" width="58" height="26" rx="2"/>
<rect class="vx-box-bad" x="586" y="286" width="58" height="26" rx="2"/>
<rect class="vx-box" x="20" y="330" width="26" height="18" rx="2"/>
<text class="vx-text-muted" x="52" y="344">feasible</text>
<rect class="vx-box-accent" x="150" y="330" width="26" height="18" rx="2"/>
<text class="vx-text-muted" x="182" y="344">within 2x the best cost</text>
<rect class="vx-cell-on" x="380" y="330" width="26" height="18" rx="2"/>
<text class="vx-text-muted" x="412" y="344">best on the grid, and the model's pick</text>
<rect class="vx-box-bad" x="20" y="356" width="26" height="18" rx="2"/>
<text class="vx-text-muted" x="52" y="370">infeasible: bi + bk &gt; budget</text>
</svg>
<figcaption>Figure 1. The toy's search space from the first example. Rows are the row-block size <code>bi</code>, columns the reduction-block size <code>bk</code>. Cost falls toward the bottom right of the feasible region; the model finds the same cell the grid search does, bi=256 and bk=256, after checking seven candidates instead of eighty-one.</figcaption>
</figure>

??? check "The model never tries bk=4 or bk=8 once it has picked a large bi. Why is that safe here, and what would make it unsafe?"

    It is safe because the cost function has no term that a smaller `bk` ever helps: the overhead term only shrinks as `bk` grows, and nothing else in the function depends on `bk` at all, so the largest feasible `bk` is always at least as good as any smaller one. It would stop being safe the moment a cost function gained a term that a smaller `bk` helped, for example a register-pressure penalty for a micro-kernel tile that outgrows the register file, which is exactly the limit [P12](p12-fast-gemm.md) has to model when it chooses `mr` and `nr`. A model is only as good as the terms it accounts for; a term it leaves out is a term it cannot get right by accident.

## Search when there is no formula

Not every cost function admits a shortcut like the one above. When the relationship between a parameter and its cost is not understood well enough to reason about directly, an analytic model cannot exist, and empirical search is what is left. Two questions decide how a search should be built: how much of the space to visit, and whether one strategy is enough.

**Grid search** visits every candidate on an explicit set, which guarantees finding the best point that set contains, at a cost that multiplies across every parameter: the toy's 9 candidates per parameter become 81 pairs for two parameters, and would become 729 for three. **Random search** samples a fixed number of candidates instead, at whatever budget fits, but samples with no guarantee about which points it lands near. In the first example, random search with 12 samples, about a seventh of the grid, checked 9 feasible pairs and returned a cost of 4224 against the true best of 4160: close, but not the answer grid search found.

OpenTuner's answer to that gap is not to trust one guess about how well a single strategy will do on an unknown cost surface. It runs several search techniques (in the terms of its paper, simulated annealing, genetic search and others) as an ensemble competing for the same evaluation budget, on the reasoning that different techniques suit different kinds of surface and none is safe to assume in advance.[^opentuner14] A learned cost model can also stand in for a plain trial-and-error search: Ansor searches a schedule space that is far larger than this chapter's toy, for programs that look like the algorithm-and-schedule split [P14](p14-algorithms-and-schedules.md) builds, and uses a model trained on earlier measurements to rank candidates instead of measuring every one of them.[^ansor20] Between "try candidates blindly" and "reason from equations to one answer" sits a spectrum of how much structure a tuner is willing to assume about its own cost function.

??? check "Random search checked 9 of 81 grid points and still missed the optimum. What would have to be true of the cost surface for a small random sample to be enough?"

    Roughly, that the surface has no narrow, isolated optimum a sparse sample could step over: that points near the best one score close to it too, so landing anywhere in that neighbourhood is nearly as good as landing on the exact best cell. Nothing in an unknown problem guarantees that shape in advance, which is the reason OpenTuner runs several strategies rather than one fixed sample size and hopes.

## What a tuner may not do

Every strategy above scored candidates with a cost function and moved on. None of them asked whether a candidate was legal to try in the first place, because legality was assumed to already hold for the whole set. The matmul ladder's rung 9 states that assumption directly: parameter tuning keeps the same bits only if the search space has been filtered to variants that do. That filter is not optional and it is not the tuner's to invent; it comes from the same rules [P7](p7-loop-transformations.md#strip-mining-and-tiling) and [O9](o9-alias-analysis.md#promises-the-front-end-writes-down) already established.

A block size that only regroups independent iterations, such as the row-block `bi` above or `mc`, `nc`, `mr` and `nr` in a real GEMM, changes nothing about which values get added to which: those iterations were already free to run in any order. A block size that regroups the reduction dimension is different. The next example sums the same array two ways, in the same left-to-right order within each block of 16 values, but combines the 16 block totals in reverse order:

--8<-- "includes/examples/optimize/p15-choosing-parameters/reduction_order.cpp.md"

The two sums differ in their last bits. Nothing here is a bug: floating-point addition is not associative, so regrouping which partial sums get added to which, even while every individual addition stays correctly rounded, can change the final result. [Decision 56](../decisions/numbers.md#d56) forbids exactly this for Vortex: every `f32` and `f64` operation rounds once, in the written order, with no reassociation. A tuner searching `kc`, the reduction-block size in a real GEMM, is free to try any value only if every value it tries still visits the `k` blocks in increasing order and still feeds each block's product into the same running accumulator; a candidate that violates that is not a slower version of the same program, it is a different program, and no cost function gets to compare it against the one the language specifies.

??? check "Of mc, kc, nc, mr and nr, which may a Vortex tuner search freely, and which need an extra rule?"

    `mc`, `nc`, `mr` and `nr` only regroup work along `i` and `j`, independent iterations that [O9](o9-alias-analysis.md#promises-the-front-end-writes-down)'s `&mut` rule already lets the compiler reorder; any value a search tries there keeps the same bits. `kc` regroups the reduction over `k`; it is safe only under the extra condition this section states, that the `k` blocks stay in increasing order and their partial products keep landing in the same accumulator, the same condition [P7](p7-loop-transformations.md#strip-mining-and-tiling)'s interchange already relies on.

## For Vortex

!!! vortex "Exercise"

    **Decide first.** List every parameter your compiler's lowering of the matmul kernel can vary: at minimum a row-block size and a reduction-block size, and, if you have built [P7](p7-loop-transformations.md)'s register blocking, a micro-kernel size. For each one, write one line saying why choosing it freely keeps the kernel's floating-point bits unchanged (cite decision 56 or your own dependence argument), or, for the reduction-block size, state the extra condition it must meet.

    **Then build**, on one of two tracks:

    1. A small analytic model: a function of facts your build already has about the target, such as a cache size it queries or one hard-coded per target, that picks at least one parameter directly, the way this chapter's toy model picks a block size from a resident-data budget instead of a table.
    2. A bounded search over an explicit candidate set for whichever parameters the model leaves open: a grid search over the whole set and a random search over a fixed, seeded number of candidates, both scored by a cost your compiler can compute without running the generated program, such as an estimate of memory traffic or register pressure. Not a wall-clock timer built into the compiler itself.

    Either way, add an optimization remark for the finished choice, naming its source ([philosophy principle 6](../philosophy.md#6-explain-performance-decisions)), for example `remark: kc=256, chosen by model (budget 512 elements)` or `remark: kc=256, chosen by grid search over 49 feasible candidates`.

    **Not yet:** wall-clock timing inside the compiler binary itself, which belongs to a separate benchmark harness; a learned cost model in the style of Ansor; letting a search touch `mc`, `nc`, `mr` or `nr` in a way that changes which elements different threads read together ([P13](p13-multithreading.md)); relaxing decision 56 to widen the search space.

    **Proof that it works:**

    - Every golden output your compiler already has for the matmul kernel stays byte for byte the same with the tuned parameters in, for every candidate your search or model considered, not only the winner.
    - A run of your grid search and your random search on the same candidate set with the same seed, reproducible, showing how many candidates each evaluated and whether either matched the other's best.
    - The optimization remark captured from your compiler's own output for the finished choice.
    - A table, dated, with your compiler's version:

    | Parameter | Source | Candidates considered | Value chosen | Golden output unchanged? |
    | --- | --- | --- | --- | --- |
    | reduction block | model | | | |
    | reduction block | grid search | | | |
    | row block | random search | | | |

## Key ideas

!!! recap "Questions you can now answer"

    - **Does proving a loop tiling legal also tell you the tile size?** No. Legality only bounds the space of transformations that keep the same meaning; it does not select a point inside that space.
    - **What is the difference between an analytic model and empirical search?** A model reasons from known facts straight to a value; search tries candidate values and scores each one, without needing to know why one wins.
    - **Why did a model-driven version of ATLAS matter?** It chose parameters from equations about the target machine and performed comparably to ATLAS's own exhaustive, install-time search, without needing a search phase at all.
    - **Why can a structural shortcut, like fixing one parameter once another is chosen, still count as a model?** Because it comes from reasoning about the cost function's shape, not from trying candidates and comparing their scores.
    - **What can go wrong with a small random sample of a search space?** It carries no guarantee of landing near the optimum; a strategy that assumes the surface is smooth can be wrong about an unknown one.
    - **Which parameters of a blocked matmul are always safe to search freely?** Ones that only regroup independent iterations, such as the row and column block sizes and the register tile; the reduction-dimension block size needs the extra condition that its blocks stay in increasing order into the same accumulator.
    - **What must every candidate a tuner tries already satisfy, before its cost is even measured?** Vortex's floating-point strictness and aliasing rules. A faster answer that changes the bits or the aliasing guarantee is not a slower candidate; it is a different, illegal program.

## Where this comes back

!!! next "You will use this again in"

    - [P16. Capstone: the ladder, measured](p16-capstone.md): *reporting rung 9's chosen parameters against Accelerate, OpenBLAS and BLIS*
    - [O12. Testing an optimizer](o12-testing-optimizers.md): *golden outputs compared with a parameter on and off*
    - [M9. Schedules as IR: the transform dialect](../mlir/m9-transform-dialect.md): *a search space expressed and explored inside the IR itself*

## Sources and further reading

Read Yotov and coauthors first: it is a retrospective on ATLAS by people who built and later re-examined it, and it states plainly what a model can and cannot replace about search. Whaley and Dongarra's own report is the primary source for what ATLAS actually searched. Ansel and coauthors' OpenTuner paper is the clearest account of why relying on one search strategy is a gamble. Low and coauthors and Zheng and coauthors sit at the two ends of this chapter's spectrum: one replaces search with equations, the other replaces blind search with a learned guide over a far larger space.

[^goto08]: Goto and van de Geijn, ACM Transactions on Mathematical Software 34(3), 2008, on the packed-block and micro-kernel structure. <https://doi.org/10.1145/1356052.1356053> (free copy: <https://www.cs.utexas.edu/~flame/pubs/GotoTOMS_revision.pdf>)
[^atlas98]: Whaley and Dongarra, Proceedings of SC98 (1998), published as LAPACK Working Note 131. <https://www.netlib.org/lapack/lawnspdf/lawn131.pdf> (DOI: <https://doi.org/10.1109/SC.1998.10004>)
[^yotov05]: Yotov et al., Proceedings of the IEEE 93(2), 2005. <https://iss.oden.utexas.edu/Publications/Papers/ieee05.pdf> (DOI: <https://doi.org/10.1109/JPROC.2004.840444>)
[^low16]: Low et al., ACM Transactions on Mathematical Software 43(2), 2016. <https://www.cs.utexas.edu/~flame/pubs/TOMS-BLIS-Analytical.pdf> (DOI: <https://doi.org/10.1145/2925987>)
[^opentuner14]: Ansel et al., Proceedings of PACT 2014, including the description of its ensemble of search techniques. <https://commit.csail.mit.edu/papers/2014/ansel-pact14-opentuner.pdf> (DOI: <https://doi.org/10.1145/2628071.2628092>); project site <https://opentuner.org/>
[^ansor20]: Zheng et al., Proceedings of OSDI 2020, on searching a schedule space with a learned cost model. <https://www.usenix.org/conference/osdi20/presentation/zheng>

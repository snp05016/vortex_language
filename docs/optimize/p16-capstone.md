# P16. Capstone: the ladder, measured

<p class="page-intro">Every earlier chapter of this book proved one transformation legal in isolation. This chapter climbs the whole ladder in one run, gates every rung with a bits test before it gates anything with a stopwatch, and reports the result next to Accelerate, OpenBLAS and BLIS, so the numbers a Vortex compiler prints mean something.</p>

<p class="vx-meta" markdown="1">Level: Advanced · Reading time: about 55 minutes · Builds on: [P1. Measure first](p1-measure-first.md), [P7. Loop transformations](p7-loop-transformations.md), [P12. Anatomy of a fast GEMM](p12-fast-gemm.md), [P15. Choosing parameters: models or search](p15-choosing-parameters.md)</p>

???+ remember "Before you start, remember"

    ??? question "What does an optimization remark record, and why does every rung below need one?"

        The decision (passed, missed or analysis), where in the source it applies, and why. Vortex's philosophy asks a compiler to explain a performance decision rather than only take or refuse it, so a rung that only prints a number and not a reason has not met the contract.

        Introduced in [O1. The optimizer's contract](o1-optimizer-contract.md#remarks-the-optimizers-report).

    ??? question "How does a compiler remove the bounds and overflow checks from the kernel's inner loop, and what must it prove first?"

        By a range proof: for constant loop bounds it can show, from the loop's recurrence and trip count, that every index stays inside the array on every iteration, so the check can never fail and the check for it is dead. Nothing is removed until the proof holds.

        Introduced in [O8. Loops: structure, induction variables and bounds checks](o8-loops.md#removing-a-check-with-a-proof).

    ??? question "Why can a vectorizer treat the kernel's write to `c[row, column]` as invisible to `a` and `b`, with no runtime check in the loop?"

        Because `c` arrives as `&mut`, which the language guarantees is reachable through no other parameter of the same call. A front end may write that guarantee into the IR as `noalias`, and the optimizer takes it as a proof, not an estimate.

        Introduced in [O9. Memory: alias analysis and MemorySSA](o9-alias-analysis.md#the-price-of-not-knowing-runtime-checks).

    ??? question "Which loop transformations keep the kernel's bits identical to the naive one, and which ones only might?"

        Interchange, tiling with in-place accumulation, unrolling and skewing keep every bit when their legality test passes; only an illegal version, or a transformation that regroups one element's own sum, changes them.

        Introduced in [P7. Loop transformations](p7-loop-transformations.md#putting-them-in-order).

!!! goals "In this chapter"

    - Explain why a bits test gates every rung of the ladder before a stopwatch does, and name the two rungs that fail it on purpose.
    - Reconstruct the climb from the naive kernel to a library-competitive one as one ordered list, and say which earlier chapter and which Vortex roadmap item each rung belongs to.
    - Apply the measurement protocol from P1 to a full ladder and to three comparator libraries, reporting a median and a confidence interval instead of a single run.
    - Recognize the two places where speed and bitwise identity trade off, and why Vortex makes both an explicit opt-in rather than a default.

## One kernel, one ladder

Every chapter in this book has worked on one piece of code, the kernel from [stage 10](../compiler/guide/stage-10-matrix-multiplication.md#the-program-the-milestone-asks-for):

```vortex
// items: valid
fn multiply(a: &[f32; 64, 64], b: &[f32; 64, 64], c: &mut [f32; 64, 64]) {
    for row in 0..64 {
        for column in 0..64 {
            let mut sum: f32 = 0.0;
            for k in 0..64 {
                sum += a[row, k] * b[k, column];
            }
            c[row, column] = sum;
        }
    }
}
```

A **ladder** is an ordered list of transformations, each one taking the kernel from the last rung's shape to a faster one, with a reason it is legal and a chapter that teaches it. This book's ladder has eleven rungs, numbered 0 to 10, and this chapter is where they are finally run end to end, on one machine, against three widely used libraries: Apple's Accelerate, OpenBLAS and BLIS.[^accelerate][^openblas][^blis-gh] Nothing here claims a number for you. Every table below is either quoted from a source with its machine and year, or left for you to fill in on your own hardware, because that is the only kind of number this book is willing to print.

The point of climbing the whole ladder in one chapter, instead of stopping at the last rung's own numbers, is that a rung's speedup is only meaningful next to a baseline that does not move. [P1](p1-measure-first.md) already asked for a median and a confidence interval instead of a single run; this chapter asks the same question about the whole climb: does rung 6 beat rung 5, on this machine, today, or does it only look that way because rung 5 was measured on a noisier afternoon?

## The bits test

Section 0.3 of this book's own research into the ladder states a rule plainly enough to quote in full: for the kernel's sum `sum += a[row, k] * b[k, column]`, a transformation is **bit-preserving** if, for every element of `c`, the products are still added one at a time, in increasing `k`, into a running value that starts from the same initial value. Nothing about *which* element runs before which other element matters; only the order of additions *within* one element's own sum does, because addition is not associative in IEEE 754 floating point, and Vortex's own numeric rules forbid regrouping it: an operation is one IEEE 754 operation, rounded once, not contracted or reassociated.[^decisions-numbers]

This is a test you can run, not only a definition. Given the naive kernel's output for some input, and a candidate rung's output for the same input, compare every element byte for byte. If they match, the rung is bit-preserving for that input; if a real proof is wanted rather than one input's luck, the argument is the one in the last paragraph, checked once for the rung's shape rather than once per input.

The included example builds a tiny, reusable version of that check and runs it on two pairs drawn straight from the ladder's own table. The first pair swaps the row and column loops of a 2 by 2 product: only the order *across* C elements changes, so the gate passes. The second pair sums one C element's four products two ways, left to right and as a balanced tree: the same four numbers, added in a different order, land on different bits, because one of the four terms is large enough to swallow a smaller one depending on which partner it is added to first. That second pair is not a contrived curiosity: it is exactly the shape a **k-vectorized** reduction takes, and exactly why rung 3 below vectorizes across `column`, never across `k`.

--8<-- "includes/examples/optimize/p16-capstone/bits_gate.cpp.md"

<figure class="vx-figure">
<svg viewBox="0 0 760 460" role="img" aria-label="Every rung passes through a bits gate before a measurement gate" aria-describedby="p16-f1-desc">
<title id="p16-f1-title">Two gates for every rung</title>
<desc id="p16-f1-desc">A flow diagram. A box "implement rung N" leads down to a box "bits gate: same order per C element as rung 0?". Two arrows leave that box. The left arrow, labeled identical, leads to "measure: P1 protocol, many runs, median plus 95% CI", which leads down to a box "record: remark, median, CI, machine, date". The right arrow, labeled differs, leads to a box "listed as opt-in in the contract? rung 7, FMA; rung 9, k-split". From that box, an arrow labeled yes leads down to the same record box; an arrow labeled no leads to a box marked as a bad outcome, "reject: fix the rung before it is timed".</desc>
<defs><marker id="p16-f1-head" viewBox="0 0 10 10" refX="10" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker></defs>
<rect class="vx-box" x="290" y="20" width="180" height="50" rx="6"/>
<text class="vx-text" x="380" y="50" text-anchor="middle">Implement rung N</text>
<line class="vx-line" x1="380" y1="70" x2="380" y2="106" marker-end="url(#p16-f1-head)"/>
<rect class="vx-box-accent" x="250" y="106" width="260" height="66" rx="6"/>
<text class="vx-text" x="380" y="132" text-anchor="middle">Bits gate</text>
<text class="vx-text-muted" x="380" y="152" text-anchor="middle">same order per C element as rung 0?</text>
<path class="vx-line" d="M260 172 C 180 200, 120 200, 110 236" marker-end="url(#p16-f1-head)"/>
<text class="vx-text-accent" x="150" y="200" text-anchor="middle">identical</text>
<path class="vx-line" d="M500 172 C 580 200, 630 200, 620 236" marker-end="url(#p16-f1-head)"/>
<text class="vx-text-accent" x="610" y="200" text-anchor="middle">differs</text>
<rect class="vx-box" x="10" y="236" width="220" height="66" rx="6"/>
<text class="vx-text" x="120" y="262" text-anchor="middle">Measure: P1 protocol</text>
<text class="vx-text-muted" x="120" y="282" text-anchor="middle">many runs, median, 95% CI</text>
<rect class="vx-box" x="500" y="236" width="250" height="80" rx="6"/>
<text class="vx-text" x="625" y="258" text-anchor="middle">Listed as opt-in?</text>
<text class="vx-text-muted" x="625" y="276" text-anchor="middle">rung 7, FMA; rung 9, k-split</text>
<text class="vx-text-muted" x="625" y="294" text-anchor="middle">(the contract's own list)</text>
<path class="vx-line" d="M120 302 C 120 340, 260 350, 310 356" marker-end="url(#p16-f1-head)"/>
<path class="vx-line" d="M560 316 C 480 350, 400 356, 380 358" marker-end="url(#p16-f1-head)"/>
<text class="vx-text-accent" x="480" y="332" text-anchor="middle">yes</text>
<rect class="vx-box" x="210" y="358" width="340" height="70" rx="6"/>
<text class="vx-text" x="380" y="382" text-anchor="middle">Record</text>
<text class="vx-text-muted" x="380" y="402" text-anchor="middle">remark, median, 95% CI, machine, date</text>
<path class="vx-line" d="M680 316 C 690 340, 665 350, 655 358" marker-end="url(#p16-f1-head)"/>
<text class="vx-text-accent" x="700" y="334" text-anchor="middle">no</text>
<rect class="vx-box-bad" x="590" y="358" width="150" height="70" rx="6"/>
<text class="vx-text" x="665" y="386" text-anchor="middle">Reject</text>
<text class="vx-text-muted" x="665" y="404" text-anchor="middle">fix before timing</text>
</svg>
<figcaption>Every rung answers the bits question first. Only an identical rung, or one the contract already names as an opt-in trade, is allowed to reach the stopwatch; everything else is a bug in the rung, not a speed problem.</figcaption>
</figure>

??? check "Why does the ladder vectorize across `column` (rung 3) and never across `k`, even though both are loops in the nest?"

    Vectorizing across `column` puts a different element of `c` in each SIMD lane: no lane's own sum changes order, so the gate passes. Vectorizing across `k` would sum several of one element's products in a tree inside the vector unit, which is exactly the reordering the second example shows changing bits. LLVM only performs that kind of reduction under `-fassociative-math` and related flags, which Vortex's strict floating-point rules never turn on.[^llvm-vec]

## Reading the ladder, rung by rung

The table below is the ladder in full. "Bits" says whether the rung keeps the naive kernel's exact output; "Proves" names the legality argument a compiler must make before applying it; "Chapter" names where this book builds that argument. "Evidence" is a real number from a real source, with the chip, the paper and the year that produced it: read it as a fact about someone else's machine, not a promise about yours.

| Rung | Change | Bits | Proves | Chapter | Evidence |
| --- | --- | --- | --- | --- | --- |
| 0 | Naive `ijk`, checks in the inner loop | baseline | n/a | this book's kernel | measure |
| 1 | Scalar cleanup: mem2reg, SCCP, GVN/LICM of address math, checks removed by range proof | identical | range proof (SCEV); no-wrap only where proven | [O5](o5-constants-and-dead-code.md), [O6](o6-redundancy.md), [O8](o8-loops.md) | measure |
| 2 | Interchange `ijk` to `ikj`: `b` and `c` walked with unit stride | identical | dependence vectors stay lexicographically positive | [P6](p6-dependence-analysis.md), [P7](p7-loop-transformations.md#interchange) | transposing the loop took a Core 2 build to 23.4% of its original time[^drepper] |
| 3 | Vectorize across `column` | identical (no FMA) | no alias between `c` and `a`/`b` (`noalias`) | [O9](o9-alias-analysis.md), [P10](p10-vectorization.md) | measure |
| 4 | Tile for L1/L2 | identical, if `c` stays resident and is accumulated in place | the tiled band is fully permutable | [P8](p8-cache-blocking.md) | sub-matrix blocking took the same Core 2 build to 17.3%[^drepper] |
| 5 | Pack `a` and `b` into contiguous panels | identical | none; packing only copies | [P12](p12-fast-gemm.md) | measure |
| 6 | Register-blocked micro-kernel, accumulators loaded from `c` | identical when loaded from `c`; not when zero-initialized (rung 6b, BLIS style) | register budget respected | [P7](p7-loop-transformations.md#unroll-and-jam-and-register-blocks), [P12](p12-fast-gemm.md) | measure |
| 7 | FMA contraction | **different**: one rounding instead of two | an explicit FP-mode permission | a future FP option ([decision 56](../decisions/numbers.md#d56) leaves it closed for now) | measure (M4); Haswell's two FMA units give 32 FLOPS/cycle in one published account[^boehm] |
| 8 | Multithread the outer blocks | identical, as long as no reduction is split across threads | race freedom under Vortex's reference rules | [P13](p13-multithreading.md) | four `std::thread` workers took the same build to 16 ms from 70 ms on an i7-6700[^boehm] |
| 9 | Split `k` across threads (a reduction) | **different**: partial sums finish in a different order | correct only as a labeled reduction, summed once at the end | [P13](p13-multithreading.md), [Smith14](https://www.cs.utexas.edu/~flame/pubs/blis3_ipdps14.pdf)[^smith14] | measure |
| 10 | Tune `mc`, `kc`, `nc`, `mr`, `nr` | identical, if the search space is restricted to bit-preserving rungs | the tuning space must stay meaning-preserving | [P15](p15-choosing-parameters.md) | measure |

Two rows do not say "identical", and both say so for a reason the contract already gives a name. Rung 7 fuses a multiply and an add into one instruction with one rounding step, which is a different, and sometimes more accurate, answer than two roundings.[^langref] Rung 9 finishes a `c` element's sum as several partial sums added together at the end rather than one sum threaded through every term in order; correct, but a different grouping. Every other rung on this table can be made to match rung 0's bits exactly, and the table above is also a checklist: a rung that claims a speedup without first passing its row's bits test has not earned a place on this ladder.

A stretch rung sits past 10: Apple's SME matrix-multiply instructions, available on the owner's machine as `FEAT_SME`.[^local] A 2024 measurement reports more than 2.3 FP32 TFLOPS from SME on an M4;[^hellosme] whether that number, or anything near it, holds on this machine is exercise territory, not a claim this page makes.

??? check "Rung 6 has a variant, 6b, written 'BLIS style' in the table. What does it change, and why does that move it out of the identical column?"

    Rung 6 as listed starts each micro-kernel's accumulators from the values already sitting in `c`, so the kernel's own accumulation order is untouched. The BLIS-style variant starts every accumulator at zero and adds the whole micro-kernel's contribution to `c` only once, at the end of the `k` loop.[^blis15] That is a different grouping of the same additions, so it belongs with rung 9 in spirit: correct, and a named, opt-in trade, not identical.

## What the comparators are, and what "fair" means here

Three widely used libraries sit at the far end of this ladder, and the finish line asks how close a Vortex-shaped kernel comes to each of them, at the same precision, the same shapes and the same thread count:

- **Accelerate**, Apple's system library. Since macOS 15, `BLASSetThreading(BLAS_THREADING_SINGLE_THREADED)` pins it to one thread for a fair single-threaded comparison, and leaving threading unset lets it use as many as it likes.[^accelerate]
- **OpenBLAS**, which reads its thread count from the first of three environment variables it finds set: `OPENBLAS_NUM_THREADS`, then `GOTO_NUM_THREADS`, then `OMP_NUM_THREADS`.[^openblas]
- **BLIS**, which reads `BLIS_NUM_THREADS`.[^blis-mt]

None of these numbers is quoted here, because none has been measured on this machine yet. What is worth stating instead is a caution already on record: one widely read account attributes Accelerate's speed on an M1 Pro, roughly 1 ms against roughly 8 ms for OpenBLAS on the same problem, to Apple's matrix-multiply instructions.[^boehm] That is a claim about one author's M1 Pro, not a law about Apple Silicon, and it says nothing about an M4. The only way to know what holds here is to measure here, which is what the rest of this chapter sets up.

??? check "An OpenBLAS binary runs with two threads on one machine and eight on another, even though neither shell sets `OPENBLAS_NUM_THREADS`. What is the most likely explanation, and in what order does OpenBLAS look?"

    One of the other two variables, `GOTO_NUM_THREADS` or `OMP_NUM_THREADS`, is set differently in the two environments; OpenBLAS checks `OPENBLAS_NUM_THREADS` first, then `GOTO_NUM_THREADS`, then `OMP_NUM_THREADS`.[^openblas] A fair comparison pins all three explicitly rather than trusting whichever one happens to be unset.

## Placing every rung on the roofline

[P3](p3-roofline.md) defines **operational intensity** as flops per byte moved from memory, and the roofline model bounds attainable performance by the smaller of the machine's peak flops and its peak bandwidth times that intensity.[^roofline09] Tiling does not raise the machine's ceiling; it raises how many flops the kernel gets out of each byte it moves, which is a property of the algorithm and its block size, not of the hardware. The included example makes that argument in closed form, with no hardware numbers of any kind:

--8<-- "includes/examples/optimize/p16-capstone/intensity_model.cpp.md"

For an N by N by N product, a fully naive model treats every one of the `2N^3` operand reads as a cold load: `8N^3` bytes moved (four bytes per `f32`, two operands per multiply-add) for `2N^3` flops, an intensity of exactly 0.25 flops per byte, whatever `N` is. A block that stays resident for `block_side` reuses before it is evicted divides that traffic by `block_side`, which multiplies the intensity by the same factor: 4.0 flops per byte at `block_side = 16`, still independent of `N`. Rungs 4 and 5 are this argument turned into code: they do not make the CPU faster, they make each byte the CPU already moved worth more.

This model leaves out everything a real cache does apart from that one number, associativity, line size, prefetching, the sizes the [memory hierarchy chapter](p2-memory-hierarchy.md) covers, so it is a lower bound on how good tiling *could* be, not a prediction of how good it *is*. Placing an actual point on an actual roofline needs the machine's own peak flops and peak bandwidth, measured as [P3](p3-roofline.md) describes, never assumed.

## Reporting rules, and the table you fill in

A set of twelve rules for reporting a measurement is worth following in full: report the machine, report a base case, use confidence intervals rather than a single number or a best-of-N, and give an upper bound alongside the result rather than letting it stand alone.[^hb15] [P1](p1-measure-first.md) already asked for a median with a bootstrap confidence interval instead of one run, and the reason is not caution for its own sake: a benchmark's own variance, from thread placement to which cache lines happen to collide, can be larger than the difference between two rungs.[^georges07][^kj13] The included example builds the statistic this chapter's report asks for, over a synthetic sample so nothing here is mistaken for a real timing:

--8<-- "includes/examples/optimize/p16-capstone/bootstrap_ci.cpp.md"

Nine numbers, one of them far from the rest, still bootstrap to a tight interval around the true middle: a median is not dragged around by one bad run the way a mean is, and the interval says how much to trust it without needing to know why the ninth run was slow.

The finish line's report is the same statistic, run once per rung and once per comparator, at a shape and a thread count you choose and hold fixed across the whole row. The table is empty because no run has produced these numbers yet on this machine:

| Kernel | N | Threads | Median GFLOP/s | 95% CI | % of measured roofline peak |
| --- | --- | --- | --- | --- | --- |
| Rung 0, naive | | | | | |
| Rung 2, interchanged | | | | | |
| Rung 4, tiled | | | | | |
| Rung 6, register-blocked | | | | | |
| Rung 8, multithreaded | | | | | |
| Accelerate, single-threaded | | | | | |
| Accelerate, multi-threaded | | | | | |
| OpenBLAS | | | | | |
| BLIS | | | | | |

`GFLOP/s` is `2*M*N*K / t` for an `M` by `K` times `K` by `N` product and a time `t` in nanoseconds, as [P1](p1-measure-first.md) defines it. Record the CPU, the compiler and its version, the flags, and the date beside the table: [P4](p4-counters-and-tools.md) and the machine facts this book has cited throughout are only meaningful next to that context, and a table without it is a table nobody else can check.

??? check "A filled-in row shows rung 8 at 40% of the measured roofline peak, with a tight confidence interval. What does that 40% claim, and what would it take to call rung 8 'fast'?"

    It claims that rung 8's median throughput was 40% of *this run's own measured ceiling*, on this machine, at this shape and thread count: nothing about any other machine, and nothing about whether 40% is good, which needs the comparator rows to answer. It also assumes the kernel's operational intensity places it on the side of the ridge point where bandwidth, not peak flops, is the limit; if it is on the other side, the roofline bound is the wrong ceiling to compare against, and [P3](p3-roofline.md) explains how to tell which side a rung is on.

## For Vortex

!!! vortex "Exercise"

    **Build** the finish line for your own compiler, on top of the `vortex-bench` harness from [P1](p1-measure-first.md)'s exercise.

    1. A **ladder registry**: a way to register several implementations of the same kernel under names (`rung0`, `rung2`, ...), each producing a `[f32; N, N]` result for the same inputs.
    2. A **bits gate** that runs before any timing: compare every registered rung's output, byte for byte, against `rung0`'s output for at least two input shapes, including a shape none of the tile or unroll factors divides evenly. A rung that is not on the contract's opt-in list (FMA, k-split) and fails this gate must not be timed; the harness should say why, not only that it failed.
    3. An **opt-in label** for any rung the bits gate is allowed to fail: it is recorded as such in the report, and its output is never compared against a non-opt-in rung as if the two were interchangeable.
    4. A **comparator adapter** that links against Accelerate (or another BLAS your platform provides) for the same shapes, correctness-checked the same way as your own rungs, and threaded the same way for a chosen thread count.
    5. A **report** that runs [P1](p1-measure-first.md)'s protocol for every registered rung and comparator at a chosen shape and thread count, and emits the table this chapter left empty: median GFLOP/s, a 95% confidence interval, the machine facts, and the date, as JSON and as the table.

    **Not yet:** a search over tile or unroll parameters ([P15](p15-choosing-parameters.md)); the SME stretch rung; any GPU comparator ([G10](../gpu/g10-matmul-ladder.md)); any number in your report that this run did not itself measure.

    **Proof that it works:**

    - The bits gate rejects a deliberately broken rung (swap two indices in a copy of `rung0`) and accepts every rung currently on your ladder.
    - Run the report twice on the same machine, same day, with nothing else running: the two 95% intervals overlap. Run it a third time on a busy machine, deliberately, and confirm the interval widens rather than the median silently drifting, which is what a hidden source of noise looks like.
    - The report for an opt-in rung is visibly marked as such, in the JSON and in the table, and the report never lets an opt-in rung's number stand next to a non-opt-in rung's without that mark.

## Key ideas

!!! recap "Questions you can now answer"

    - **What decides whether a rung may run by default?** Whether it keeps the naive kernel's bits: the two that do not, FMA contraction and a split `k` reduction, are opt-in, named in the contract, never silent.
    - **What two gates does every rung pass through before it may claim a number?** The bits gate, then the measurement protocol from P1: a rung that fails the first is a bug, not a benchmark result.
    - **What three libraries does the finish line compare against, and what has to match for the comparison to be fair?** Accelerate, OpenBLAS and BLIS, at the same precision, shapes and thread count, with each library's own thread-count variable pinned explicitly.
    - **What does tiling change, in the roofline model's terms?** Not the machine's ceiling: the algorithm's own bytes moved per flop, which the closed-form model in this chapter computes without any hardware number at all.
    - **Why report a median and a confidence interval instead of the fastest run seen?** Because a benchmark's own variance can be larger than the gap between two rungs, and the fastest run is the least representative one, not the most.
    - **What does an "evidence: measure" cell in the ladder table mean?** That no source cited here gives a number for that step on this machine; the table stays empty until this chapter's own exercise fills it in.

## Where this comes back

!!! next "You will use this again in"

    - [G10. The GPU matmul ladder](../gpu/g10-matmul-ladder.md): *ladder*, *bits gate*, *finish line against a baseline*
    - [M9. Schedules as IR: the transform dialect](../mlir/m9-transform-dialect.md): *legality before profitability*, *search space*
    - [E4. Testing back ends](../backend/e4-testing-backends.md): *differential testing against a reference*

## Sources and further reading

For the full ladder with every rung's legality argument spelled out, read the chapters this table links to in order, O5 through O9 and P6 through P15; for the finish line's statistics, Hoefler and Belli's reporting rules are worth reading in full rather than summarized.[^hb15]

[^decisions-numbers]: Vortex documentation, [Numbers, literals and casts, decision 56](../decisions/numbers.md#d56): floating-point operations are not contracted or reassociated in v0.1.
[^llvm-vec]: LLVM Project, "Auto-Vectorization in LLVM", section "Reductions": reduction vectorization requires `-fassociative-math -fno-signed-zeros -fno-trapping-math`. <https://llvm.org/docs/Vectorizers.html>
[^drepper]: Ulrich Drepper, "What Every Programmer Should Know About Memory", version 1.0, 2007, section 6.2.1: the transposed loop measured at 23.4% of the original matrix-multiplication time, and the sub-matrix (blocked) version at 17.3%, on the paper's Core 2 test machine. <https://www.akkadia.org/drepper/cpumemory.pdf>
[^boehm]: Simon Boehm, "Fast Multidimensional Matrix Multiplication on CPU from Scratch", August 2022: the Haswell FMA throughput figure (two FMA units, 32 FLOPS/cycle), the loop-reorder timing (1512 ms to 89 ms) and the four-thread timing (70 ms to 16 ms) on the post's i7-6700 test machine, and the Accelerate-versus-OpenBLAS timings on an M1 Pro attributed to Apple's matrix instructions. <https://siboehm.com/articles/22/Fast-MMM-on-CPU>
[^smith14]: Tyler M. Smith, Robert van de Geijn, Mikhail Smelyanskiy, Jeff R. Hammond and Field G. Van Zee, "Anatomy of High-Performance Many-Threaded Matrix Multiplication", *IPDPS* 2014: parallelizing the `ic` and `jr` loops preserves the naive accumulation order; parallelizing the `pc` (`k`) loop is a reduction and needs per-thread copies of `c` summed at the end. <https://www.cs.utexas.edu/~flame/pubs/blis3_ipdps14.pdf>
[^blis15]: Field G. Van Zee and Robert A. van de Geijn, "BLIS: A Framework for Rapidly Instantiating BLAS Functionality", *ACM Transactions on Mathematical Software* 41(3), 2015: the micro-kernel contract `C11 := beta*C11 + alpha*A1*B1`, which adds a whole panel's contribution to `C` once rather than accumulating it in place. <https://doi.org/10.1145/2764454> (author's copy: <https://www.cs.utexas.edu/~flame/pubs/blis1_toms_rev3.pdf>)
[^hellosme]: Roman Remke and Ricardo Breuer, "Hello SME!", 2024: an M4 result of more than 2.3 FP32 TFLOPS using the Scalable Matrix Extension. <https://arxiv.org/abs/2409.18779>
[^local]: Owner's machine, `sysctl hw.optional.arm`, checked 2026-09-23: `FEAT_SME` and `FEAT_SME2` both report 1.
[^accelerate]: Apple, Accelerate framework documentation, and its BLAS entry, including `BLASSetThreading`. <https://developer.apple.com/documentation/accelerate> ; <https://developer.apple.com/documentation/accelerate/blas>
[^openblas]: OpenBLAS project, README and site: the thread-count environment variable priority, `OPENBLAS_NUM_THREADS` before `GOTO_NUM_THREADS` before `OMP_NUM_THREADS`. <https://www.openblas.net/> ; <https://github.com/OpenMathLib/OpenBLAS>
[^blis-mt]: BLIS project, "Multithreading" documentation: `BLIS_NUM_THREADS`. <https://github.com/flame/blis/blob/master/docs/Multithreading.md>
[^blis-gh]: BLIS project repository. <https://github.com/flame/blis>
[^langref]: LLVM Project, LLVM Language Reference Manual, the `contract` fast-math flag. <https://llvm.org/docs/LangRef.html>
[^roofline09]: Samuel Williams, Andrew Waterman and David Patterson, "Roofline: An Insightful Visual Performance Model for Multicore Architectures", *Communications of the ACM* 52(4), 2009. <https://doi.org/10.1145/1498765.1498785> (free copy: <https://people.eecs.berkeley.edu/~kubitron/cs252/handouts/papers/RooflineVyNoYellow.pdf>)
[^hb15]: Torsten Hoefler and Roberto Belli, "Scientific Benchmarking of Parallel Computing Systems: Twelve Ways to Tell the Masses When Reporting Performance Results", *SC* 2015. <https://doi.org/10.1145/2807591.2807644> (free copy: <https://htor.inf.ethz.ch/publications/img/hoefler-scientific-benchmarking.pdf>)
[^georges07]: Andy Georges, Dries Buytaert and Lieven Eeckhout, "Statistically Rigorous Java Performance Evaluation", *OOPSLA* 2007. <https://doi.org/10.1145/1297027.1297033> (free copy: <https://dri.es/files/oopsla07-georges.pdf>)
[^kj13]: Tomas Kalibera and Richard Jones, "Rigorous Benchmarking in Reasonable Time", *ISMM* 2013. <https://doi.org/10.1145/2464157.2464160> (free copy: <https://kar.kent.ac.uk/33611/>)

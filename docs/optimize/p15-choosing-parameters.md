# P15. Choosing parameters: models or search

<p class="page-intro">Proving a tiling legal says which tile sizes keep the program's meaning; it never says which one to use. This chapter chooses tile sizes for one small kernel in five ways, by reasoning from cache facts and by trying candidates, compares what each way costs and what it guarantees, and sets the rule a Vortex tuner must apply before it scores anything: every candidate must print the same bits.</p>

<p class="vx-meta" markdown="1">Level: Advanced · Reading time: about 45 minutes · Builds on: [P12. Anatomy of a fast GEMM](p12-fast-gemm.md), [P14. Algorithms and schedules](p14-algorithms-and-schedules.md)</p>

???+ remember "Before you start, remember"

    ??? question "What does a 'missed' optimization remark report, and what is it for?"

        An attempted transformation that legality or profitability blocked, with the reason. It lets a reader, or a later pass, see why a choice was not made instead of guessing.

        Introduced in [O1. The optimizer's contract](o1-optimizer-contract.md#remarks-the-optimizers-report).

    ??? question "A block of a matrix fits in the cache with room to spare, yet the blocked loop still misses far more often than a capacity count predicts. What can cause that?"

        Self-interference: rows of the block map to the same few sets of a set-associative cache and evict each other, even though the block's total size fits.

        Introduced in [P8. Cache blocking](p8-cache-blocking.md#fitting-is-not-enough-self-interference).

    ??? question "Which of the two micro-kernel forms gives the naive loop's bits for every value of `kc`?"

        The C-initialized form. It loads the `c` tile into the accumulators and continues the running sum from panel to panel, so every product is added in the naive order. The zero-initialized form sums each panel from zero and then adds the panel total to `c`, which regroups the additions.

        Introduced in [P12. Anatomy of a fast GEMM](p12-fast-gemm.md#two-ways-to-accumulate).

    ??? question "How do you decide that version B of a kernel is faster than version A?"

        Run several launches of each, interleaved, and report the ratio of the median times with a bootstrap confidence interval. If the interval excludes 1, there is a difference, and its endpoints say how large it plausibly is.

        Introduced in [P1. Measure first](p1-measure-first.md#comparing-two-versions).

    ??? question "May a Vortex compiler fuse, reorder or widen any `f32` or `f64` operation to make it faster?"

        No. Each operation is one IEEE 754 result, rounded once to nearest with ties to even, in the written order: no fused multiply-add, no reassociation, no reordering, no wider intermediate format.

        Introduced in [Numbers, decision 56](../decisions/numbers.md#d56).

!!! goals "In this chapter"

    - Explain why proving a transformation legal leaves its parameters open, and define a search space and a cost function for them.
    - Derive a tile size by hand from a cache-fit inequality, and say what such a model leaves out.
    - Compare grid search, orthogonal line search, random search and a model refined by local search by what each one tries and what it guarantees.
    - Describe how ATLAS, the model-driven ATLAS of Yotov and colleagues, BLIS's analytical model, OpenTuner and Ansor each choose parameters.
    - Filter a search space down to candidates that keep the program's bits, and specify a tuner for your compiler that reports its choice.

## One kernel, thirty-six legal versions

[P8](p8-cache-blocking.md#tiling-shortens-the-distance) blocked a matrix multiplication so that one block of `b` is reused by every row of `a`. Take the same kernel, `c += a * b` on 80 × 80 `f32` matrices, and give the block two sides: `bk` rows of `b` (a stretch of the `k` loop) by `bj` columns (a stretch of the `j` loop). Both strip loops sit outside the `i` loop, so the `bk × bj` block of `b` is loaded once and then read by all 80 rows.

Every pair `(bk, bj)` gives a legal program, and a program with the same bits. For one element `c[i][j]`, the products still arrive in increasing `k`: the `kk` loop runs its blocks in order, and the `k` loop runs within each block in order. Nothing is regrouped. With six sizes for each side, 2, 4, 8, 16, 32 and 64, there are 36 versions of the kernel, all printing the same matrix. They differ only in speed.

A number like `bk` that changes how a program runs but not what it computes is a **tuning parameter**. The set of values a tuner may choose from is its **search space**, here the 36 pairs. Proving the tiling legal, as [P7](p7-loop-transformations.md#strip-mining-and-tiling) did, only tells you that the whole space is safe. It does not say which point to use.

To compare points, a tuner needs a **cost function**: a number for each candidate, where smaller is better. The honest cost is running time, but time on a real machine is noisy and machine-specific. The first example uses a stand-in that gives the same answer everywhere: it plays the kernel's memory accesses through a simulated cache, P8's toy (4 KiB, 2-way set-associative, 32-byte lines, least recently used), and counts the misses. The table of costs over the whole space is the **cost surface**.

--8<-- "includes/examples/optimize/p15-choosing-parameters/tile_search.cpp.md"

The first seven lines of output are the surface. Read along the row `bk=8`: misses fall from 69,224 at `bj=2` to 12,411 at `bj=32`, because `a` is read once for each strip of `bj` columns, so wider strips mean fewer passes over `a`; then they rise again at `bj=64`. Read down the column `bj=16`: misses fall until `bk=32` and rise at `bk=64`. The lowest cell is `bk=32, bj=16`, with 8,414 misses.

## Scoring every candidate: grid search

The simplest strategy is **grid search**: score every candidate and keep the best. The example's `grid search` line tried all 36 pairs and found `bk=32, bj=16`. On this grid it cannot do worse, because it looks everywhere.

Its cost is the size of the space, and that size multiplies. Six values per parameter give 36 pairs for two parameters; for the five parameters of [P12](p12-fast-gemm.md#choosing-mc-kc-nc-mr-and-nr), `mc`, `kc`, `nc`, `mr` and `nr`, six values each would give 7,776 candidates. Grid search also finds only the best point on the grid: if the best tile is 24 and the grid holds 16 and 32, it reports one of those.

Figure 1 draws the surface. It is not a smooth bowl. Cost falls as the block grows and then jumps, and the jump comes earlier in rows with a larger `bk`: in the rows `bk=8` and `bk=16` cost first rises at `bj=64`, in the rows `bk=32` and `bk=64` already at `bj=32`. These are the points where the block of `b`, the row segment of `c` and the line of `a` no longer fit together, or map onto the same sets and evict each other ([P8](p8-cache-blocking.md#fitting-is-not-enough-self-interference)).

<figure class="vx-figure">
<svg viewBox="0 0 760 420" role="img" aria-label="A six by six grid of simulated miss counts for the toy kernel, rows bk and columns bj from 2 to 64, with the pick of each search strategy marked" aria-describedby="p15-f1-desc">
<title id="p15-f1-title">The toy's cost surface, and where each strategy landed</title>
<desc id="p15-f1-desc">Rows are bk and columns bj, each 2, 4, 8, 16, 32 and 64. Each cell holds the simulated misses in thousands. The lowest cell, bk 32 and bj 16 with 8.4 thousand misses, has a heavy border and is marked G for grid search and L plus for model plus local search. Two cells within a quarter of the best, bk 16 bj 16 with 9.7 thousand and bk 16 bj 32 with 10.0 thousand, have accent borders; the first is marked M for the model's prediction and R1 for random search with seed 1. Cells more than three times the best have dashed borders: the whole row bk 2, the whole column bj 2, the small-block corner at the top left, and the large-block cells at the right of rows bk 16 and 32 and the right half of row bk 64. Orthogonal line search, marked L, stopped at bk 8 bj 32 with 12.4 thousand. Random search with seed 2 stopped at bk 32 bj 32, 13.2 thousand, and with seed 3 at bk 4 bj 64, 21.8 thousand.</desc>
<text class="vx-text-muted" x="362" y="22" text-anchor="middle">bj (columns of the b block)</text>
<text class="vx-mono" x="152" y="46" text-anchor="middle">2</text>
<text class="vx-mono" x="236" y="46" text-anchor="middle">4</text>
<text class="vx-mono" x="320" y="46" text-anchor="middle">8</text>
<text class="vx-mono" x="404" y="46" text-anchor="middle">16</text>
<text class="vx-mono" x="488" y="46" text-anchor="middle">32</text>
<text class="vx-mono" x="572" y="46" text-anchor="middle">64</text>
<text class="vx-text-muted" x="22" y="180" text-anchor="middle" transform="rotate(-90 22 180)">bk (rows of the b block)</text>
<text class="vx-mono" x="98" y="85" text-anchor="end">2</text>
<rect class="vx-box-bad" x="112" y="62" width="80" height="36" rx="2"/>
<text class="vx-mono" x="152" y="78" text-anchor="middle">223.9</text>
<rect class="vx-box-bad" x="196" y="62" width="80" height="36" rx="2"/>
<text class="vx-mono" x="236" y="78" text-anchor="middle">111.9</text>
<rect class="vx-box-bad" x="280" y="62" width="80" height="36" rx="2"/>
<text class="vx-mono" x="320" y="78" text-anchor="middle">56.0</text>
<rect class="vx-box-bad" x="364" y="62" width="80" height="36" rx="2"/>
<text class="vx-mono" x="404" y="78" text-anchor="middle">44.8</text>
<rect class="vx-box-bad" x="448" y="62" width="80" height="36" rx="2"/>
<text class="vx-mono" x="488" y="78" text-anchor="middle">42.3</text>
<rect class="vx-box-bad" x="532" y="62" width="80" height="36" rx="2"/>
<text class="vx-mono" x="572" y="78" text-anchor="middle">39.8</text>
<text class="vx-mono" x="98" y="125" text-anchor="end">4</text>
<rect class="vx-box-bad" x="112" y="102" width="80" height="36" rx="2"/>
<text class="vx-mono" x="152" y="118" text-anchor="middle">121.5</text>
<rect class="vx-box-bad" x="196" y="102" width="80" height="36" rx="2"/>
<text class="vx-mono" x="236" y="118" text-anchor="middle">60.8</text>
<rect class="vx-box-bad" x="280" y="102" width="80" height="36" rx="2"/>
<text class="vx-mono" x="320" y="118" text-anchor="middle">30.4</text>
<rect class="vx-box" x="364" y="102" width="80" height="36" rx="2"/>
<text class="vx-mono" x="404" y="118" text-anchor="middle">24.0</text>
<rect class="vx-box" x="448" y="102" width="80" height="36" rx="2"/>
<text class="vx-mono" x="488" y="118" text-anchor="middle">22.4</text>
<rect class="vx-box" x="532" y="102" width="80" height="36" rx="2"/>
<text class="vx-mono" x="572" y="118" text-anchor="middle">21.8</text>
<text class="vx-text-accent" x="572" y="134" text-anchor="middle">R3</text>
<text class="vx-mono" x="98" y="165" text-anchor="end">8</text>
<rect class="vx-box-bad" x="112" y="142" width="80" height="36" rx="2"/>
<text class="vx-mono" x="152" y="158" text-anchor="middle">69.2</text>
<rect class="vx-box-bad" x="196" y="142" width="80" height="36" rx="2"/>
<text class="vx-mono" x="236" y="158" text-anchor="middle">34.6</text>
<rect class="vx-box" x="280" y="142" width="80" height="36" rx="2"/>
<text class="vx-mono" x="320" y="158" text-anchor="middle">17.3</text>
<rect class="vx-box" x="364" y="142" width="80" height="36" rx="2"/>
<text class="vx-mono" x="404" y="158" text-anchor="middle">13.3</text>
<rect class="vx-box" x="448" y="142" width="80" height="36" rx="2"/>
<text class="vx-mono" x="488" y="158" text-anchor="middle">12.4</text>
<text class="vx-text-accent" x="488" y="174" text-anchor="middle">L</text>
<rect class="vx-box" x="532" y="142" width="80" height="36" rx="2"/>
<text class="vx-mono" x="572" y="158" text-anchor="middle">15.8</text>
<text class="vx-mono" x="98" y="205" text-anchor="end">16</text>
<rect class="vx-box-bad" x="112" y="182" width="80" height="36" rx="2"/>
<text class="vx-mono" x="152" y="198" text-anchor="middle">54.8</text>
<rect class="vx-box-bad" x="196" y="182" width="80" height="36" rx="2"/>
<text class="vx-mono" x="236" y="198" text-anchor="middle">27.4</text>
<rect class="vx-box" x="280" y="182" width="80" height="36" rx="2"/>
<text class="vx-mono" x="320" y="198" text-anchor="middle">13.7</text>
<rect class="vx-box-accent" x="364" y="182" width="80" height="36" rx="2"/>
<text class="vx-mono" x="404" y="198" text-anchor="middle">9.7</text>
<text class="vx-text-accent" x="404" y="214" text-anchor="middle">M  R1</text>
<rect class="vx-box-accent" x="448" y="182" width="80" height="36" rx="2"/>
<text class="vx-mono" x="488" y="198" text-anchor="middle">10.0</text>
<rect class="vx-box-bad" x="532" y="182" width="80" height="36" rx="2"/>
<text class="vx-mono" x="572" y="198" text-anchor="middle">30.1</text>
<text class="vx-mono" x="98" y="245" text-anchor="end">32</text>
<rect class="vx-box-bad" x="112" y="222" width="80" height="36" rx="2"/>
<text class="vx-mono" x="152" y="238" text-anchor="middle">49.7</text>
<rect class="vx-box" x="196" y="222" width="80" height="36" rx="2"/>
<text class="vx-mono" x="236" y="238" text-anchor="middle">24.8</text>
<rect class="vx-box" x="280" y="222" width="80" height="36" rx="2"/>
<text class="vx-mono" x="320" y="238" text-anchor="middle">12.4</text>
<rect class="vx-box-strong" x="364" y="222" width="80" height="36" rx="2"/>
<text class="vx-mono" x="404" y="238" text-anchor="middle">8.4</text>
<text class="vx-text-accent" x="404" y="254" text-anchor="middle">G  L+</text>
<rect class="vx-box" x="448" y="222" width="80" height="36" rx="2"/>
<text class="vx-mono" x="488" y="238" text-anchor="middle">13.2</text>
<text class="vx-text-accent" x="488" y="254" text-anchor="middle">R2</text>
<rect class="vx-box-bad" x="532" y="222" width="80" height="36" rx="2"/>
<text class="vx-mono" x="572" y="238" text-anchor="middle">51.2</text>
<text class="vx-mono" x="98" y="285" text-anchor="end">64</text>
<rect class="vx-box-bad" x="112" y="262" width="80" height="36" rx="2"/>
<text class="vx-mono" x="152" y="278" text-anchor="middle">78.4</text>
<rect class="vx-box-bad" x="196" y="262" width="80" height="36" rx="2"/>
<text class="vx-mono" x="236" y="278" text-anchor="middle">39.2</text>
<rect class="vx-box" x="280" y="262" width="80" height="36" rx="2"/>
<text class="vx-mono" x="320" y="278" text-anchor="middle">19.6</text>
<rect class="vx-box" x="364" y="262" width="80" height="36" rx="2"/>
<text class="vx-mono" x="404" y="278" text-anchor="middle">15.6</text>
<rect class="vx-box-bad" x="448" y="262" width="80" height="36" rx="2"/>
<text class="vx-mono" x="488" y="278" text-anchor="middle">48.3</text>
<rect class="vx-box-bad" x="532" y="262" width="80" height="36" rx="2"/>
<text class="vx-mono" x="572" y="278" text-anchor="middle">52.5</text>
<rect class="vx-box-strong" x="110" y="324" width="22" height="16" rx="2"/><text class="vx-text-muted" x="138" y="337">best on the grid</text>
<rect class="vx-box-accent" x="260" y="324" width="22" height="16" rx="2"/><text class="vx-text-muted" x="288" y="337">within 25% of the best</text>
<rect class="vx-box-bad" x="450" y="324" width="22" height="16" rx="2"/><text class="vx-text-muted" x="478" y="337">over 3 times the best</text>
<text class="vx-text-muted" x="110" y="364">G grid search · M model (predicted) · L+ model then local search · L orthogonal line search · R1 to R3 random, seeds 1 to 3</text>
</svg>
<figcaption>Figure 1. The first example's cost surface: simulated misses, in thousands, for each pair of tile sizes. The cost is not a smooth bowl. It falls as the blocks grow, then jumps once the <code>b</code> block and its neighbours no longer fit, and the jump comes at different places in different rows. Letters mark where each strategy stopped.</figcaption>
</figure>

## Reasoning instead of trying: an analytic model

An **analytic cost model** computes a parameter from facts about the machine and the loop nest, without running or simulating any candidate. The model in the example follows the reasoning of Yotov and colleagues for ATLAS's tile size. Their first estimate asks that three `NB × NB` tiles fit in the L1 cache, $3 N_B^2 \le C_1$. They then observe that, in their loop order, only one tile has to stay whole, together with one column of another and one element of the third, which gives $N_B^2 + N_B + 1 \le C_1$, and they refine it again for cache lines.[^yotov05]

Apply the same reasoning to the toy's loop order. The block of `b` must stay resident while `i` sweeps all rows; during one row, the `bj` elements of `c` are reused for every `k`; and one element of `a` is reused across `j`. Counted in cache lines of 8 `f32` values, with a square tile of side $t$, the working set fits when

$$
t \left\lceil \frac{t}{8} \right\rceil + \left\lceil \frac{t}{8} \right\rceil + 1 \le 128,
$$

because the toy cache holds 4096 / 32 = 128 lines. (Each row of the block starts on a line boundary here, since a row of 80 `f32` values is 320 bytes, exactly 10 lines.) Now try the candidates by hand:

| `t` | Lines for the `b` block | Lines for `c` | Line for `a` | Total | Fits in 128? |
| --- | --- | --- | --- | --- | --- |
| 8 | 8 × 1 = 8 | 1 | 1 | 10 | yes |
| 16 | 16 × 2 = 32 | 2 | 1 | 35 | yes |
| 32 | 32 × 4 = 128 | 4 | 1 | 133 | no |

The model picks the largest square tile that fits, `t = 16`, and it spends no simulation to do so: the example's `model` line reports zero candidates tried. The surface puts `(16, 16)` at 9,688 misses, 15% more than the best cell. The model found the right region and missed the best point in it.

It missed for two reasons that are typical of models. It only considered square tiles, as ATLAS's `NB` does, so it never asked about `bk=32, bj=16`, whose block takes 32 × 2 + 2 + 1 = 67 lines and fits. And it counts capacity, not conflicts: `bk=16, bj=32` also needs 67 lines, yet costs 10,020 misses to the other shape's 8,414, because the two shapes land on the cache's sets differently. Yotov and colleagues name the same gap: their models ignore conflict misses, which they judge intractable to model in general.[^yotov05]

Their remedy is the one the example's last line uses. Start at the model's point and run a **local search**: score the point's **neighbours**, here the cells one step up, down, left and right in Figure 1, move to the best of them if it improves, and stop when none does. They suggest that this combination of a model and local search around its answer may be the most tractable approach for large programs.[^yotov05]

Walk the local search by hand with Figure 1. From `(16, 16)` at 9.7 thousand misses, the four neighbours are `(8, 16)` at 13.3, `(32, 16)` at 8.4, `(16, 8)` at 13.7 and `(16, 32)` at 10.0 thousand. The best is `(32, 16)`, so the search moves there. Its neighbours are `(16, 16)`, already scored, `(64, 16)` at 15.6, `(32, 8)` at 12.4 and `(32, 32)` at 13.2 thousand; none is lower, so the search stops. It scored 8 of the 36 cells, counting the model's own, and ended on the grid's best.

??? check "Local search stopped at `(32, 16)` because no neighbour was better. Does that prove `(32, 16)` is the best cell on the grid? What would you need to know about the surface for it to?"

    No. It proves only that `(32, 16)` is a **local minimum**: better than each of its neighbours. A cell further away could still be lower. On this surface none is, and you can check more: every other cell in Figure 1 has a neighbour lower than itself, so `(32, 16)` is the only local minimum and a local search from any starting cell would end there. Nothing guarantees that shape in advance. On a surface with several valleys, where the search ends depends on where it starts, which is why the starting point a model supplies matters, and only an exhaustive search can confirm that the valley it found is the lowest.

## Trying one parameter at a time: ATLAS

ATLAS, Whaley and Dongarra's Automatically Tuned Linear Algebra Software, took the other road. It isolates the machine-specific part of matrix multiplication in an on-chip multiply, a kernel that works on blocks sized to fit the L1 cache, and a code generator produces many versions of that kernel. A timer runs each version on the machine where the library is being installed, and the fastest version is kept.[^atlas98] Choosing parameters by generating candidates and timing them is **empirical search**, and doing it once per machine, before the library is used, is **install-time tuning**.

The search in the 1998 report tries blocking factors first with no unrolling, then latency-hiding factors with the chosen block, then every unrolling of the `M` and `N` loops the register count allows, and finally all blocking and latency factors again with that unrolling. Results are stored in files, so a later or interrupted search never times the same case twice, and the report gives a typical install as 1 to 2 hours for each precision.[^atlas98] The example imitates the file store: every strategy asks for costs through one table, so a pair scored twice is simulated once and counted once.

Yotov and colleagues describe ATLAS's search as **orthogonal line search**. To optimize a function of $n$ parameters, it solves $n$ one-parameter problems in a fixed order, and while it tunes one parameter it holds each parameter not yet tuned at a **reference value**. They point out that this is a heuristic: it need not find the optimum even for a convex function.[^yotov05]

The example's line search tunes `bk` first with `bj` held at 64, then tunes `bj` with the chosen `bk`. Figure 2 shows the cells it tried. The column `bj=64` puts `bk=8` lowest, at 15.8 thousand misses; along the row `bk=8`, `bj=32` is lowest, at 12.4 thousand. It tried 11 cells and stopped 47% above the best.

<figure class="vx-figure">
<svg viewBox="0 0 760 330" role="img" aria-label="Two copies of the six by six grid showing which cells orthogonal line search and model plus local search tried, and in which round" aria-describedby="p15-f2-desc">
<title id="p15-f2-title">Two searches on the same grid</title>
<desc id="p15-f2-desc">Left, orthogonal line search: round 1 tries the whole column bj 64, six cells, and keeps bk 8; round 2 tries the rest of row bk 8, five more cells, and stops at bk 8 bj 32. Eleven cells tried. Right, model plus local search: round 0 is the model's cell, bk 16 bj 16; round 1 tries its four neighbours and moves to bk 32 bj 16; round 2 tries that cell's three untried neighbours, none better, and stops. Eight cells tried, ending at the grid's best cell.</desc>
<text class="vx-text" x="80" y="46">Orthogonal line search: 11 tried</text>
<text class="vx-mono" x="104" y="70" text-anchor="middle">2</text>
<text class="vx-mono" x="152" y="70" text-anchor="middle">4</text>
<text class="vx-mono" x="200" y="70" text-anchor="middle">8</text>
<text class="vx-mono" x="248" y="70" text-anchor="middle">16</text>
<text class="vx-mono" x="296" y="70" text-anchor="middle">32</text>
<text class="vx-mono" x="344" y="70" text-anchor="middle">64</text>
<text class="vx-mono" x="72" y="100" text-anchor="end">2</text>
<rect class="vx-box" x="82" y="82" width="44" height="26" rx="2"/>
<rect class="vx-box" x="130" y="82" width="44" height="26" rx="2"/>
<rect class="vx-box" x="178" y="82" width="44" height="26" rx="2"/>
<rect class="vx-box" x="226" y="82" width="44" height="26" rx="2"/>
<rect class="vx-box" x="274" y="82" width="44" height="26" rx="2"/>
<rect class="vx-box-accent" x="322" y="82" width="44" height="26" rx="2"/>
<text class="vx-mono" x="344" y="101" text-anchor="middle">1</text>
<text class="vx-mono" x="72" y="130" text-anchor="end">4</text>
<rect class="vx-box" x="82" y="112" width="44" height="26" rx="2"/>
<rect class="vx-box" x="130" y="112" width="44" height="26" rx="2"/>
<rect class="vx-box" x="178" y="112" width="44" height="26" rx="2"/>
<rect class="vx-box" x="226" y="112" width="44" height="26" rx="2"/>
<rect class="vx-box" x="274" y="112" width="44" height="26" rx="2"/>
<rect class="vx-box-accent" x="322" y="112" width="44" height="26" rx="2"/>
<text class="vx-mono" x="344" y="131" text-anchor="middle">1</text>
<text class="vx-mono" x="72" y="160" text-anchor="end">8</text>
<rect class="vx-box-accent" x="82" y="142" width="44" height="26" rx="2"/>
<text class="vx-mono" x="104" y="161" text-anchor="middle">2</text>
<rect class="vx-box-accent" x="130" y="142" width="44" height="26" rx="2"/>
<text class="vx-mono" x="152" y="161" text-anchor="middle">2</text>
<rect class="vx-box-accent" x="178" y="142" width="44" height="26" rx="2"/>
<text class="vx-mono" x="200" y="161" text-anchor="middle">2</text>
<rect class="vx-box-accent" x="226" y="142" width="44" height="26" rx="2"/>
<text class="vx-mono" x="248" y="161" text-anchor="middle">2</text>
<rect class="vx-box-strong" x="274" y="142" width="44" height="26" rx="2"/>
<text class="vx-mono" x="296" y="161" text-anchor="middle">2</text>
<rect class="vx-box-accent" x="322" y="142" width="44" height="26" rx="2"/>
<text class="vx-mono" x="344" y="161" text-anchor="middle">1</text>
<text class="vx-mono" x="72" y="190" text-anchor="end">16</text>
<rect class="vx-box" x="82" y="172" width="44" height="26" rx="2"/>
<rect class="vx-box" x="130" y="172" width="44" height="26" rx="2"/>
<rect class="vx-box" x="178" y="172" width="44" height="26" rx="2"/>
<rect class="vx-box" x="226" y="172" width="44" height="26" rx="2"/>
<rect class="vx-box" x="274" y="172" width="44" height="26" rx="2"/>
<rect class="vx-box-accent" x="322" y="172" width="44" height="26" rx="2"/>
<text class="vx-mono" x="344" y="191" text-anchor="middle">1</text>
<text class="vx-mono" x="72" y="220" text-anchor="end">32</text>
<rect class="vx-box" x="82" y="202" width="44" height="26" rx="2"/>
<rect class="vx-box" x="130" y="202" width="44" height="26" rx="2"/>
<rect class="vx-box" x="178" y="202" width="44" height="26" rx="2"/>
<rect class="vx-box" x="226" y="202" width="44" height="26" rx="2"/>
<rect class="vx-box" x="274" y="202" width="44" height="26" rx="2"/>
<rect class="vx-box-accent" x="322" y="202" width="44" height="26" rx="2"/>
<text class="vx-mono" x="344" y="221" text-anchor="middle">1</text>
<text class="vx-mono" x="72" y="250" text-anchor="end">64</text>
<rect class="vx-box" x="82" y="232" width="44" height="26" rx="2"/>
<rect class="vx-box" x="130" y="232" width="44" height="26" rx="2"/>
<rect class="vx-box" x="178" y="232" width="44" height="26" rx="2"/>
<rect class="vx-box" x="226" y="232" width="44" height="26" rx="2"/>
<rect class="vx-box" x="274" y="232" width="44" height="26" rx="2"/>
<rect class="vx-box-accent" x="322" y="232" width="44" height="26" rx="2"/>
<text class="vx-mono" x="344" y="251" text-anchor="middle">1</text>
<text class="vx-text" x="470" y="46">Model, then local search: 8 tried</text>
<text class="vx-mono" x="494" y="70" text-anchor="middle">2</text>
<text class="vx-mono" x="542" y="70" text-anchor="middle">4</text>
<text class="vx-mono" x="590" y="70" text-anchor="middle">8</text>
<text class="vx-mono" x="638" y="70" text-anchor="middle">16</text>
<text class="vx-mono" x="686" y="70" text-anchor="middle">32</text>
<text class="vx-mono" x="734" y="70" text-anchor="middle">64</text>
<text class="vx-mono" x="462" y="100" text-anchor="end">2</text>
<rect class="vx-box" x="472" y="82" width="44" height="26" rx="2"/>
<rect class="vx-box" x="520" y="82" width="44" height="26" rx="2"/>
<rect class="vx-box" x="568" y="82" width="44" height="26" rx="2"/>
<rect class="vx-box" x="616" y="82" width="44" height="26" rx="2"/>
<rect class="vx-box" x="664" y="82" width="44" height="26" rx="2"/>
<rect class="vx-box" x="712" y="82" width="44" height="26" rx="2"/>
<text class="vx-mono" x="462" y="130" text-anchor="end">4</text>
<rect class="vx-box" x="472" y="112" width="44" height="26" rx="2"/>
<rect class="vx-box" x="520" y="112" width="44" height="26" rx="2"/>
<rect class="vx-box" x="568" y="112" width="44" height="26" rx="2"/>
<rect class="vx-box" x="616" y="112" width="44" height="26" rx="2"/>
<rect class="vx-box" x="664" y="112" width="44" height="26" rx="2"/>
<rect class="vx-box" x="712" y="112" width="44" height="26" rx="2"/>
<text class="vx-mono" x="462" y="160" text-anchor="end">8</text>
<rect class="vx-box" x="472" y="142" width="44" height="26" rx="2"/>
<rect class="vx-box" x="520" y="142" width="44" height="26" rx="2"/>
<rect class="vx-box" x="568" y="142" width="44" height="26" rx="2"/>
<rect class="vx-box-accent" x="616" y="142" width="44" height="26" rx="2"/>
<text class="vx-mono" x="638" y="161" text-anchor="middle">1</text>
<rect class="vx-box" x="664" y="142" width="44" height="26" rx="2"/>
<rect class="vx-box" x="712" y="142" width="44" height="26" rx="2"/>
<text class="vx-mono" x="462" y="190" text-anchor="end">16</text>
<rect class="vx-box" x="472" y="172" width="44" height="26" rx="2"/>
<rect class="vx-box" x="520" y="172" width="44" height="26" rx="2"/>
<rect class="vx-box-accent" x="568" y="172" width="44" height="26" rx="2"/>
<text class="vx-mono" x="590" y="191" text-anchor="middle">1</text>
<rect class="vx-box-accent" x="616" y="172" width="44" height="26" rx="2"/>
<text class="vx-mono" x="638" y="191" text-anchor="middle">0</text>
<rect class="vx-box-accent" x="664" y="172" width="44" height="26" rx="2"/>
<text class="vx-mono" x="686" y="191" text-anchor="middle">1</text>
<rect class="vx-box" x="712" y="172" width="44" height="26" rx="2"/>
<text class="vx-mono" x="462" y="220" text-anchor="end">32</text>
<rect class="vx-box" x="472" y="202" width="44" height="26" rx="2"/>
<rect class="vx-box" x="520" y="202" width="44" height="26" rx="2"/>
<rect class="vx-box-accent" x="568" y="202" width="44" height="26" rx="2"/>
<text class="vx-mono" x="590" y="221" text-anchor="middle">2</text>
<rect class="vx-box-strong" x="616" y="202" width="44" height="26" rx="2"/>
<text class="vx-mono" x="638" y="221" text-anchor="middle">1</text>
<rect class="vx-box-accent" x="664" y="202" width="44" height="26" rx="2"/>
<text class="vx-mono" x="686" y="221" text-anchor="middle">2</text>
<rect class="vx-box" x="712" y="202" width="44" height="26" rx="2"/>
<text class="vx-mono" x="462" y="250" text-anchor="end">64</text>
<rect class="vx-box" x="472" y="232" width="44" height="26" rx="2"/>
<rect class="vx-box" x="520" y="232" width="44" height="26" rx="2"/>
<rect class="vx-box" x="568" y="232" width="44" height="26" rx="2"/>
<rect class="vx-box-accent" x="616" y="232" width="44" height="26" rx="2"/>
<text class="vx-mono" x="638" y="251" text-anchor="middle">2</text>
<rect class="vx-box" x="664" y="232" width="44" height="26" rx="2"/>
<rect class="vx-box" x="712" y="232" width="44" height="26" rx="2"/>
<text class="vx-text-muted" x="80" y="290">rows bk, columns bj; numbers give the round in which a cell was first tried; heavy border: where the search stopped</text>
</svg>
<figcaption>Figure 2. What the two structured searches of the first example tried. Line search fixes <code>bj</code> at a reference value, tunes <code>bk</code>, then tunes <code>bj</code>: the answer depends on the reference. Local search starts from the model's cell and moves only while a neighbour is better.</figcaption>
</figure>

Now finish a second line search yourself, with the reference changed to `bj=16`. First tune `bk` down the column `bj=16` in Figure 1: the six costs are 44.8, 24.0, 13.3, 9.7, 8.4 and 15.6 thousand, so `bk=32`. Then tune `bj` along the row `bk=32`. Which cell do you stop on, and how many cells did you try?

??? check "Answer: the line search with reference `bj=16`"

    Along the row `bk=32` the costs are 49.7, 24.8, 12.4, 8.4, 13.2 and 51.2 thousand, so `bj=16`: the search stops on `(32, 16)`, the grid's best, after 11 cells (6 in the column, then 5 new ones in the row). The same strategy with a different reference value found the best cell instead of one 47% worse. Nothing inside the search tells you which reference to use; that is part of specifying it, alongside the order of the parameters and the values tried for each.[^yotov05]

## Models against search: what the studies found

The belief behind ATLAS was that a machine is too complicated to model, so parameters must be found by trying. Yotov and colleagues tested it directly. They replaced ATLAS's search engine with a model-driven engine, kept the same code generator, and compared the code the two produced on ten machines; since the generator was shared, any difference had to come from the parameter values.[^yotov05] The model-driven version produced code comparable to ATLAS's search.[^yotov05]

The costs were not comparable. ATLAS's search took from 8 minutes on a DEC Alpha to more than 8 hours on an Intel Itanium 2 to settle its parameters, while the model took no measurable time.[^yotov05] The model does ask for something search does not: accurate machine facts. A search needs the L1 size only to bound its range, so an estimate will do; a model needs the true capacity, line size, register count and latencies, and the authors wrote a separate tool, X-Ray, to measure them.[^yotov05]

The same paper shows where a simple model misleads. On two of the machines their sensitivity graphs showed that tiling for the L2 cache beat tiling for the L1, a choice the L1 inequality never considers.[^yotov05] And the authors do not claim that models replace search everywhere: systems such as FFTW and SPIRAL search over whole algorithms rather than parameter values, a setting they leave open.[^yotov05]

Low, Igual, Smith and Quintana-Ortí went further for BLIS. They argue that Yotov's comparison was made against an approach to matrix multiplication already shown to be suboptimal, and instead derive all of BLIS's parameters from a model of the machine.[^low16] For the register tile they reason from latency: an FMA that updates an element of the `c` tile must wait for the previous FMA on that element, so enough independent elements must be in flight to keep every FMA unit busy, which gives

$$
m_r \, n_r \ge N_{\mathrm{vec}} \, L_{\mathrm{vfma}} \, N_{\mathrm{vfma}},
$$

where $N_{\mathrm{vec}}$ is the number of elements in a vector register, $L_{\mathrm{vfma}}$ the FMA latency in cycles and $N_{\mathrm{vfma}}$ the number of FMA instructions issued per cycle.[^low16] They then place `kc` from the size and associativity of the L1, and `mc` from the L2.[^low16] Their `mr`, `nr` and `kc` matched the values BLIS's developers had chosen by hand, and their `mc` was similar or identical, as [P12](p12-fast-gemm.md#choosing-mc-kc-nc-mr-and-nr) reports.

The two papers agree on the lesson that matters for a compiler. When the kernel's structure is fixed and understood, a model of a few machine facts reaches the neighbourhood of the best parameters at almost no cost, and a small search around it, if needed, closes the rest of the gap. Figure 1's example did the same: zero simulations for the model's point, eight to reach the grid's best.

| Strategy | What it needs | Scores in the example | Guarantee |
| --- | --- | --- | --- |
| Grid search | an explicit candidate set | 36 | best point of the set |
| Orthogonal line search | an order and reference values | 11 | none: depends on the reference |
| Random search, 8 draws | a budget and a seed | 7 each | none |
| Analytic model | accurate machine facts | 0 | only as good as its terms |
| Model, then local search | facts, and a neighbourhood | 8 | a local minimum near the model's point |

??? check "Your model picks a tile size in no time, and a search over 200 candidates takes an hour and finds one 2% faster, inside the measurement's confidence interval. Which do you ship in a compiler, and why?"

    The model's pick. A difference inside the confidence interval is not a demonstrated difference ([P1](p1-measure-first.md#comparing-two-versions)), so the search has not shown that its pick is better at all. The model's answer is also reproducible: the same machine facts give the same parameter on every build, while a timed search can pick a different winner on the next run. Keep the search as a check on the model: if it ever finds a candidate faster by more than the interval, the model is missing a term, and that is worth a missed remark and a look.

## Search when there is no formula

A model needs a cost function understood well enough to reason about. For many spaces there is none: a schedule space with loop orders, fusion choices and tile sizes for a whole program, or a machine whose behaviour nobody has written down. Then search is what is left, and the question becomes how to spend a limited number of evaluations.

**Random search** draws candidates from the space with a random generator and keeps the best one it scored. It needs no structure at all, and its cost is whatever budget you give it. The example runs it three times, 8 draws each, with seeds 1, 2 and 3 of a fixed generator, so that the output is repeatable. The three runs stopped at `(16, 16)`, `(32, 32)` and `(4, 64)`, at 9.7, 13.2 and 21.8 thousand misses. With 7 of 36 cells scored, missing the one best cell is the likely outcome, and which near miss you get depends on the seed.

OpenTuner starts from the observation that the right search technique depends on the shape of the space. A hill climber suits a surface that rises steadily toward the best point; an evolutionary algorithm may do better on a discontinuous one; and real spaces mix plateaus, discontinuities and parameters that interact.[^opentuner14] Instead of betting on one technique, OpenTuner runs an **ensemble**: several techniques at once, such as differential evolution, Nelder-Mead and Torczon hill climbers, pattern search, particle swarm optimization and random search, all sharing one database of results.[^opentuner14]

The ensemble divides its budget of tests with a **multi-armed bandit**, a rule for repeatedly choosing among options whose payoff is unknown while balancing trying the best-known option against learning about the others. OpenTuner credits a technique each time it finds a new overall best, over a sliding window of recent tests, and gives the techniques that earn credit more of the tests.[^opentuner14] A technique that suits the surface gets most of the budget without anyone deciding in advance which one that will be.

When each evaluation is a compile and a timed run, even a good search spends most of its time measuring. [P14](p14-algorithms-and-schedules.md#tensor-programs-tvm) showed TVM's answer: guide the search with a model trained on earlier measurements. Such a **learned cost model** is fitted to measured running times of programs already tried, and predicts the cost of programs not yet run.

Ansor, which generates tensor programs for deep learning, builds its whole search around one. It first writes the space as a hierarchy: a few **sketches**, high-level loop structures, each with billions of low-level choices such as tile sizes left open, and it fills those choices by random sampling.[^ansor20] It then improves the samples by **evolutionary search**, mutating and crossing programs, and uses the learned model to predict each program's throughput. Querying the model is orders of magnitude faster than measuring, so thousands of programs can be scored in seconds.[^ansor20]

At each round Ansor measures only a small batch of the programs the model ranks highest, and retrains the model, a gradient-boosted decision tree, on the new measurements.[^ansor20] The model does not replace measurement; it decides which few programs are worth measuring.

## When the cost is a measurement

Everything in the example was deterministic, so one evaluation of a candidate was enough. Replace simulated misses by running time and each evaluation becomes the comparison of [P1](p1-measure-first.md#comparing-two-versions): several launches per candidate, a median, an interval. A search that keeps whichever candidate had the fastest single run will, on a noisy machine, sometimes keep a candidate that was lucky rather than fast. ATLAS's report already guards against this: its timers use large amounts of work per timing so that results repeat even on a machine running other jobs.[^atlas98]

Measurement also fixes when a search can run. ATLAS searched at install time, once per machine. Ansor searches when a model is compiled, on the target hardware. A compiler that must produce the same output for the same input cannot time candidates during an ordinary build: the winner, and so the binary, would change from one run to the next. That is why the Vortex exercise below scores candidates by a cost the compiler can compute, and leaves timing to a separate benchmark harness.

To see how far a model's pick is from a measured one on your own machine, use the blocked matrix multiplication from [P12](p12-fast-gemm.md#the-five-loops-around-one-micro-kernel) and this method:

1. Compute the model's `kc` and `mc` from `sysctl hw.perflevel0.l1dcachesize` and `hw.perflevel0.l2cachesize` with P12's rules, and write the values down before measuring anything.
2. Choose a grid: for example `kc` in {64, 128, 256, 512} and `mc` in {32, 64, 128, 256}, with `nc` fixed.
3. For each candidate, time several interleaved launches as [P1](p1-measure-first.md#repeat-at-the-right-level) describes, and record the median and a 95% bootstrap interval.
4. Run random search with a fixed seed and a budget of 4 candidates, three times with different seeds, from the same recorded measurements.
5. Report whether the grid's best is separated from the model's pick by more than their intervals.

| Choice | `kc` | `mc` | Median time | 95% interval | Candidates timed | Machine, compiler, date |
| --- | --- | --- | --- | --- | --- | --- |
| Model | | | | | 0 | |
| Grid search | | | | | 16 | |
| Random search, seed 1 | | | | | 4 | |
| Random search, seed 2 | | | | | 4 | |
| Random search, seed 3 | | | | | 4 | |

## What a tuner may not do

Every strategy above compared candidates on speed alone. That is only correct if every candidate computes the same thing, and the toy's space was built so that each one did. Rung 9 of the CPU matmul ladder in [P16](p16-capstone.md#the-ladder-rung-by-rung) states the condition: tuning keeps the bits only if the search space holds only identical variants. Vortex's philosophy puts it as a rule: "Auto-tuning must not change the observable meaning of a program" ([Performance philosophy](../philosophy.md#performance-philosophy)).

Real spaces do not come pre-filtered. [P12](p12-fast-gemm.md#two-ways-to-accumulate) showed that the zero-initialized micro-kernel regroups the sum at every panel boundary. The second example sweeps `kc` for one element of `c`, a dot product of length 512, under both forms, and prints the bits of each result:

--8<-- "includes/examples/optimize/p15-choosing-parameters/kc_filter.cpp.md"

Under the C-initialized form, all six values of `kc` give `0x43846a05`, the naive loop's bits. Under the zero-initialized form they give four different results, and only `kc=512`, a single panel that is no blocking at all, matches the naive loop. A tuner that searched `kc` over the zero-initialized kernel would ship whichever of four answers ran fastest on the day it ran. The printed output would depend on a timing.

The fix is not a better cost function. It is a **filter** applied to the space before any strategy scores it: a candidate that does not print the reference's bits is removed, not ranked. Figure 3 shows where the filter sits. [Decision 56](../decisions/numbers.md#d56) is what makes the filter strict: each `f32` operation rounds once, in the written order, so a candidate that regroups a sum is a different program, not a slower version of the same one.

<figure class="vx-figure">
<svg viewBox="0 0 760 300" role="img" aria-label="The shape of a tuner: candidates pass a bits filter before any strategy scores them, a strategy and a cost function loop until the strategy stops, and the pick is reported in a remark" aria-describedby="p15-f3-desc">
<title id="p15-f3-title">Where the filter sits in a tuner</title>
<desc id="p15-f3-desc">Left to right. A box labelled candidates, the parameter values the transformation allows. An arrow to a box labelled bits filter, which drops any candidate whose output differs from the reference in any bit; dropped candidates leave by a dashed arrow downward to a box labelled rejected, never timed. An arrow from the filter to a box labelled strategy: grid, line, random, local, ensemble. The strategy and a box below it labelled cost: model, simulator or measurement, are joined by two arrows, one proposing a candidate and one returning its score, forming a loop. From the strategy an arrow leads to a box labelled pick, and from the pick to a box labelled remark: value, source, candidates tried.</desc>
<defs><marker id="p15-f3-head" viewBox="0 0 10 10" refX="10" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker></defs>
<rect class="vx-box" x="10" y="40" width="130" height="64" rx="4"/>
<text class="vx-text" x="75" y="66" text-anchor="middle">candidates</text>
<text class="vx-text-muted" x="75" y="88" text-anchor="middle">what is legal</text>
<rect class="vx-box-accent" x="175" y="40" width="130" height="64" rx="4"/>
<text class="vx-text" x="240" y="66" text-anchor="middle">bits filter</text>
<text class="vx-text-muted" x="240" y="88" text-anchor="middle">same output, every bit</text>
<rect class="vx-box-bad" x="175" y="190" width="130" height="50" rx="4"/>
<text class="vx-text" x="240" y="212" text-anchor="middle">rejected</text>
<text class="vx-text-muted" x="240" y="230" text-anchor="middle">never scored</text>
<rect class="vx-box" x="340" y="40" width="140" height="64" rx="4"/>
<text class="vx-text" x="410" y="66" text-anchor="middle">strategy</text>
<text class="vx-text-muted" x="410" y="88" text-anchor="middle">grid, line, random, local</text>
<rect class="vx-box" x="340" y="190" width="140" height="50" rx="4"/>
<text class="vx-text" x="410" y="212" text-anchor="middle">cost</text>
<text class="vx-text-muted" x="410" y="230" text-anchor="middle">model, simulator, clock</text>
<rect class="vx-box-strong" x="515" y="40" width="90" height="64" rx="4"/>
<text class="vx-text" x="560" y="78" text-anchor="middle">pick</text>
<rect class="vx-box" x="640" y="40" width="112" height="64" rx="4"/>
<text class="vx-text" x="696" y="66" text-anchor="middle">remark</text>
<text class="vx-text-muted" x="696" y="88" text-anchor="middle">value, source, tried</text>
<path class="vx-flow" d="M140 72 L174 72" marker-end="url(#p15-f3-head)"/>
<path class="vx-flow" d="M305 72 L339 72" marker-end="url(#p15-f3-head)"/>
<path class="vx-line" d="M240 104 L240 189" stroke-dasharray="4 4" marker-end="url(#p15-f3-head)"/>
<path class="vx-flow" d="M395 104 L395 189" marker-end="url(#p15-f3-head)"/>
<path class="vx-flow" d="M425 190 L425 105" marker-end="url(#p15-f3-head)"/>
<text class="vx-text-muted" x="388" y="150" text-anchor="end">propose</text>
<text class="vx-text-muted" x="432" y="150">score</text>
<path class="vx-flow" d="M480 72 L514 72" marker-end="url(#p15-f3-head)"/>
<path class="vx-flow" d="M605 72 L639 72" marker-end="url(#p15-f3-head)"/>
<text class="vx-text-muted" x="10" y="280">The filter runs on the whole candidate set before the loop starts, so no score ever ranks a program the language forbids.</text>
</svg>
<figcaption>Figure 3. The parts of a tuner and their order. Only candidates that print the reference's bits reach the loop between strategy and cost; the loop may use a model, a simulator or a clock, and whatever it picks is reported with its source.</figcaption>
</figure>

For the five parameters of P12 the filter reduces to a few rules. `mc`, `nc`, `mr` and `nr` choose which elements of `c` are computed together and in what order; each element still receives its products in increasing `k`, so they are free. `kc` is free only with the C-initialized micro-kernel. Splitting `k` across threads ([P13](p13-multithreading.md)) would join partial sums the same way the zero-initialized form does, so it stays outside the space. Halide's autotuner, as [P14](p14-algorithms-and-schedules.md#a-schedule-space-is-large) described, compared each candidate's output with a reference as a sanity check; for Vortex the check is the definition of the space.

??? check "Of `mc`, `kc`, `nc`, `mr` and `nr`, which may a Vortex tuner search freely, and what must be true before `kc` joins them?"

    `mc`, `nc`, `mr` and `nr` only change which elements of `c` are computed together and in which order; every element still adds its products one at a time in increasing `k`, so every value keeps the bits. `kc` joins them only when the micro-kernel continues each element's running sum across panels, the C-initialized form, and no `k` range is split between threads. Under the zero-initialized form, changing `kc` changes where partial sums are joined and so changes the printed result.

## For Vortex

!!! vortex "Exercise"

    **Decide first.** List every parameter your compiler's lowering of the stage 10 matrix multiplication can vary: at least a block size for `j` and one for `k`, and a register tile if you built [P7](p7-loop-transformations.md#unroll-and-jam-and-register-blocks)'s register blocking. For each, write one line saying why every value keeps the kernel's bits, citing decision 56 and the order in which each element of `c` receives its products, or the condition under which it does. Any parameter you cannot argue for stays fixed.

    **Build:**

    1. **The filter.** A test mode that compiles the kernel with every candidate in your space, runs each, and compares its output byte for byte with the untiled build. A candidate that differs is an error in your space, not a slow candidate.
    2. **A model.** A function of target facts your compiler already holds, such as the L1 size and line size from a per-target table, that picks the block sizes from a fit inequality you derive for your own loop order, as this chapter did for the toy. It runs no code.
    3. **Optionally, local search** around the model's pick, scored by a cost your compiler can compute without running the program, such as the number of distinct cache lines the block touches or a simulated miss count.
    4. **A remark** for the finished choice, in the stream you built for [O1](o1-optimizer-contract.md#for-vortex), naming the values, their source and how many candidates were scored ([principle 6](../philosophy.md#6-explain-performance-decisions)); and a missed remark whenever the filter removes a candidate, naming the parameter.

    **Not yet:** timing candidates inside the compiler; a learned cost model; ensembles of search techniques; the zero-initialized micro-kernel; splitting `k` across threads ([P13](p13-multithreading.md)); any option that relaxes decision 56 to enlarge the space.

    **Proof that it works:**

    - The filter test passes for every candidate in the space, not only the winner, and fails, with a missed remark, when you deliberately add a candidate that regroups the `k` sum.
    - The same source compiled twice gives the same parameters and the same remark, byte for byte.
    - A golden remark file for the stage 10 program recording the chosen values and their source.
    - A table, filled in with the method from "When the cost is a measurement", dated and with your compiler's version:

    | Choice | Parameters | Candidates scored | Median time | 95% interval | Output identical to untiled? |
    | --- | --- | --- | --- | --- | --- |
    | untiled | none | 0 | | | reference |
    | model | | 0 | | | |
    | model, then local search | | | | | |
    | best of a grid, timed outside the compiler | | | | | |

## Key ideas

!!! recap "Questions you can now answer"

    - **Does proving a tiling legal also choose the tile size?** No. Legality defines the space of meaning-preserving choices; a cost model or a search picks a point in it.
    - **What does grid search guarantee, and what does it cost?** The best point of the grid, at the price of scoring every candidate, a number that multiplies with each added parameter.
    - **What does an analytic model need that search does not?** Accurate machine facts. It then costs almost nothing, but it is only as good as the terms it includes; the toy's model ignored conflicts and rectangles.
    - **Why combine a model with local search?** The model lands near the best point for free, and a few neighbour steps close the gap its missing terms leave, as the toy's eight scores did.
    - **Why is orthogonal line search a heuristic?** It tunes one parameter at a time with the others at reference values, so its answer depends on those values and need not be the optimum.
    - **What do OpenTuner and Ansor add to plain search?** OpenTuner shares a budget among several techniques by a bandit rule; Ansor uses a learned cost model to decide which few programs to measure.
    - **What must hold before a tuner compares candidates by speed?** Every candidate prints the reference's bits. The filter defines the space; the cost only ranks what is left.

## Where this comes back

!!! next "You will use this again in"

    - [P16. Capstone: the ladder, measured](p16-capstone.md): *rung 9*, *the bits gate for every tuned candidate*
    - [O12. Testing an optimizer](o12-testing-optimizers.md): *golden outputs for every candidate, not only the winner*
    - [M9. Schedules as IR: the transform dialect](../mlir/m9-transform-dialect.md): *tile sizes written in a schedule*, *searching over schedules*
    - [M11. End-to-end ML compilers](../mlir/m11-ml-compilers.md): *schedules chosen by rules, by search or by the programmer*

## Sources and further reading

Read Yotov and colleagues first: sections III and IV set ATLAS's search and their model side by side for the same parameters, and the conclusion is two paragraphs on what models cannot yet replace. Then read Low and colleagues, section 4, for a model that covers every BLIS parameter. For search, read OpenTuner's sections 1 and 3, and Ansor's sections 3 and 5 for sampling and the learned cost model.

[^atlas98]: R. Clint Whaley and Jack J. Dongarra, "Automatically Tuned Linear Algebra Software", *Proceedings of the 1998 ACM/IEEE Conference on Supercomputing (SC98)*, 1998; also LAPACK Working Note 131: sections 3.1 and 3.3.6. <https://www.netlib.org/lapack/lawnspdf/lawn131.pdf> (DOI: <https://doi.org/10.1109/SC.1998.10004>)
[^yotov05]: Kamen Yotov, Xiaoming Li, Gang Ren, María Garzarán, David Padua, Keshav Pingali and Paul Stodghill, "Is Search Really Necessary to Generate High-Performance BLAS?", *Proceedings of the IEEE* 93(2), 2005: the abstract, sections III-B, III-C, IV-A and IV-B, the timing results and sensitivity analyses of section V, and section VI. <https://iss.oden.utexas.edu/Publications/Papers/ieee05.pdf> (DOI: <https://doi.org/10.1109/JPROC.2004.840444>)
[^low16]: Tze Meng Low, Francisco D. Igual, Tyler M. Smith and Enrique S. Quintana-Ortí, "Analytical Modeling Is Enough for High-Performance BLIS", *ACM Transactions on Mathematical Software* 43(2), 2016: sections 1, 4.2, 4.3 and 5. <https://www.cs.utexas.edu/~flame/pubs/TOMS-BLIS-Analytical.pdf> (DOI: <https://doi.org/10.1145/2925987>)
[^opentuner14]: Jason Ansel, Shoaib Kamil, Kalyan Veeramachaneni, Jonathan Ragan-Kelley, Jeffrey Bosboom, Una-May O'Reilly and Saman Amarasinghe, "OpenTuner: An Extensible Framework for Program Autotuning", *Proceedings of the 23rd International Conference on Parallel Architectures and Compilation (PACT)*, 2014: sections 1, 3.2.1 and 3.2.2. <https://commit.csail.mit.edu/papers/2014/ansel-pact14-opentuner.pdf> (DOI: <https://doi.org/10.1145/2628071.2628092>)
[^ansor20]: Lianmin Zheng and colleagues, "Ansor: Generating High-Performance Tensor Programs for Deep Learning", *14th USENIX Symposium on Operating Systems Design and Implementation (OSDI)*, 2020: the abstract and sections 3, 4 and 5. <https://www.usenix.org/conference/osdi20/presentation/zheng>

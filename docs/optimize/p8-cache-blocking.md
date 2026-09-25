# P8. Cache blocking

<p class="page-intro">Tiling a loop nest so that the data it reuses is still in a cache when the reuse comes, choosing the tile size from cache facts, and checking that choice against the two things capacity alone does not see: how the tile's rows land in the cache's sets, and how many pages they span. For Vortex, this is the fourth rung of the matrix-multiplication ladder, and the first one whose best parameter depends on the machine.</p>

<p class="vx-meta" markdown="1">Level: Intermediate · Reading time: about 40 minutes · Builds on: [P2. The memory hierarchy](p2-memory-hierarchy.md), [P7. Loop transformations](p7-loop-transformations.md)</p>

???+ remember "Before you start, remember"

    ??? question "What is the difference between reuse and locality?"

        Reuse belongs to the computation: the same data is used by more than one iteration. Locality is reuse the cache manages to keep: the second use arrives before the data has been evicted. A loop transformation cannot create reuse, only move the uses closer together or further apart in time.

        Introduced in [P7. Loop transformations](p7-loop-transformations.md#the-same-work-in-a-different-order).

    ??? question "When may a band of loops be tiled?"

        When the band is fully permutable: every dependence is lexicographically positive and, within the band, either carried by an outer loop or free of negative entries. Matrix multiplication's one dependence, `(=, =, <)` on `c`, lets all three loops be tiled.

        Introduced in [P7. Loop transformations](p7-loop-transformations.md#strip-mining-and-tiling).

    ??? question "Why can two addresses evict each other while most of the cache is empty?"

        An address may live only in the one set its middle bits choose. If more addresses that map to one set are in use than the set has ways, they evict each other: a conflict miss, not a capacity miss.

        Introduced in [P2. The memory hierarchy](p2-memory-hierarchy.md#sets-and-associativity).

    ??? question "What is a TLB's reach?"

        The number of entries times the page size: the amount of memory whose translations the TLB can hold at once. The owner's M4 Pro uses 16 KiB pages.

        Introduced in [P2. The memory hierarchy](p2-memory-hierarchy.md#the-tlb-caching-translations-not-data).

!!! goals "In this chapter"

    - Measure a loop's reuse distance by hand, and explain why a kernel whose data fits in L2 can still miss L1 on every reuse.
    - Tile the stage 10 kernel so that one block of `b` is reused by every row, and keep its bits identical to the untiled kernel.
    - Turn a cache budget into a block size with a footprint model, and say what the model cannot see.
    - Recognize self-interference and critical strides, and predict them from a matrix's row length and a cache's set count.
    - Explain why copying a block into a contiguous buffer removes self-interference and TLB pressure, and what it costs.

## A loop that reuses more than it keeps

Take three 512 × 512 matrices of `f32`, `a`, `b` and `c`, and the ikj kernel from [P7](p7-loop-transformations.md#making-the-stage-10-nest-perfect): for each row `i`, for each `k`, add `a[i, k]` times row `k` of `b` into row `i` of `c`. Each matrix is 512 × 512 × 4 bytes = 1 MiB. P7 chose this order because it walks `b` and `c` along rows, one cache line after another. It did not ask whether a line, once fetched, is still there when it is needed again.

Follow one element, `b[0, 0]`. It is read at `i = 0, k = 0`, and next at `i = 1, k = 0`. In between, the kernel reads every row of `b`, once. The number of distinct cache lines a program touches between two uses of the same data is that pair's **reuse distance**. In a fully associative cache that evicts the least recently used line, the second use hits exactly when the reuse distance is smaller than the number of lines the cache holds: every line touched in between is more recent, and only the oldest line leaves.

Here is the count by hand, on the owner's Apple M4 Pro, whose cache line is 128 bytes (`sysctl hw.cachelinesize`) and whose performance-core L1 data cache is 131,072 bytes (`sysctl hw.perflevel0.l1dcachesize`), both read on 2026-09-24.

| Between the two uses of | Lines of `b` | Lines of `c` | Lines of `a` | Reuse distance | L1d lines |
| --- | --- | --- | --- | --- | --- |
| `b[0, 0]`, from `i = 0` to `i = 1` | 512 rows × 16 | 16 | 16 | about 8,224 | 1,024 |
| `c[0, 0]`, from `k = 0` to `k = 1` | 16 | 16 | 1 | about 33 | 1,024 |

A row of 512 `f32` values is 2,048 bytes, 16 lines. The reuse of `c` fits with room to spare, so row `i` of `c` stays in L1 while `k` runs. The reuse of `b` is eight times too long: by the time `i` moves on, all of `b` has passed through a cache an eighth its size, and every element of `b` comes from further away, on every row. The kernel's working set, the data it touches over a stretch of time ([P2](p2-memory-hierarchy.md#the-hierarchy-several-sizes-several-speeds)), is the whole of `b` over one row step.

The three matrices together, 3 MiB, fit in the same machine's 16 MiB L2 (`sysctl hw.perflevel0.l2cachesize`, shared by the four cores of a cluster according to `hw.perflevel0.cpusperl2`). So the problem is not that the data is too big. It is that the loop order makes the reuse distance of `b` longer than the fastest cache. [P3](p3-roofline.md#operational-intensity-flops-per-byte-of-dram-traffic) measured the same thing as traffic: the naive schedule re-reads `b` once per row of `c`, while the compulsory floor reads each array once.

Blocking is an old fix, and it has been measured. On the Core 2 in Drepper's paper, blocking a `double` matrix multiplication took it to 17.3% of the original loop's cycles, against 23.4% for a transposed copy of one operand; Drepper sets rounding effects aside when he reorders the additions.[^drepper] In siboehm's 1024 × 1024 `f32` tutorial on an Intel i7-6700, built with `-ffast-math`, adding L1 tiling to the reordered loop took it from 89 ms to 70 ms.[^boehm] Neither number says anything about your machine. What they say is that blocking pays on real hardware, by amounts that depend on the machine and on what the loop already does well.

## Tiling shortens the distance

Tiling, from [P7](p7-loop-transformations.md#strip-mining-and-tiling), strip-mines loops and moves the strip loops outward. The question is which loops, and which order. Lam, Rothberg and Wolf study the blocked form of this kernel that strip-mines `k` and `j` by a **block size** `B` and moves both strip loops outside `i`.[^lrw91] One step of the two outer loops fixes a `B` × `B` block of `b`, and the inner three loops run every row `i` against it:

```text
for k0 in 0, B, 2B, ...            one block of k
  for j0 in 0, B, 2B, ...          one block of j
    for i in 0 .. N                every row
      for k in k0 .. k0 + B
        for j in j0 .. j0 + B
          c[i, j] += a[i, k] * b[k, j]
```

Within one block step, each element of the `b` block is used once for every `i`, `N` times in all. Between two of those uses, the loop touches the rest of the block, `B` elements of row `i` of `c` and `B` elements of row `i` of `a`. Lam, Rothberg and Wolf describe the same condition: the block of `b` and a `B`-long row of `c` must fit in the cache, with `a[i, k]` held in a register.[^lrw91]

<figure class="vx-figure">
<svg viewBox="0 0 700 420" role="img" aria-label="One block of b stays in cache while every row of a and c passes by it">
<rect class="vx-box" x="40" y="220" width="40" height="40"/>
<rect class="vx-box" x="80" y="220" width="40" height="40"/>
<rect class="vx-box" x="120" y="220" width="40" height="40"/>
<rect class="vx-box" x="160" y="220" width="40" height="40"/>
<rect class="vx-box" x="40" y="260" width="40" height="40"/>
<rect class="vx-box" x="80" y="260" width="40" height="40"/>
<rect class="vx-box" x="120" y="260" width="40" height="40"/>
<rect class="vx-box" x="160" y="260" width="40" height="40"/>
<rect class="vx-box" x="40" y="300" width="40" height="40"/>
<rect class="vx-box" x="80" y="300" width="40" height="40"/>
<rect class="vx-box" x="120" y="300" width="40" height="40"/>
<rect class="vx-box" x="160" y="300" width="40" height="40"/>
<rect class="vx-box" x="40" y="340" width="40" height="40"/>
<rect class="vx-box" x="80" y="340" width="40" height="40"/>
<rect class="vx-box" x="120" y="340" width="40" height="40"/>
<rect class="vx-box" x="160" y="340" width="40" height="40"/>
<text class="vx-text" x="40" y="405">a (rows i, columns k)</text>
<rect class="vx-box" x="240" y="30" width="40" height="40"/>
<rect class="vx-box" x="280" y="30" width="40" height="40"/>
<rect class="vx-box" x="320" y="30" width="40" height="40"/>
<rect class="vx-box" x="360" y="30" width="40" height="40"/>
<rect class="vx-box" x="240" y="70" width="40" height="40"/>
<rect class="vx-box" x="280" y="70" width="40" height="40"/>
<rect class="vx-box" x="320" y="70" width="40" height="40"/>
<rect class="vx-box" x="360" y="70" width="40" height="40"/>
<rect class="vx-box" x="240" y="110" width="40" height="40"/>
<rect class="vx-box" x="280" y="110" width="40" height="40"/>
<rect class="vx-box" x="320" y="110" width="40" height="40"/>
<rect class="vx-box" x="360" y="110" width="40" height="40"/>
<rect class="vx-box" x="240" y="150" width="40" height="40"/>
<rect class="vx-box" x="280" y="150" width="40" height="40"/>
<rect class="vx-box" x="320" y="150" width="40" height="40"/>
<rect class="vx-box" x="360" y="150" width="40" height="40"/>
<text class="vx-text" x="410" y="50">b (rows k, columns j)</text>
<rect class="vx-box" x="240" y="220" width="40" height="40"/>
<rect class="vx-box" x="280" y="220" width="40" height="40"/>
<rect class="vx-box" x="320" y="220" width="40" height="40"/>
<rect class="vx-box" x="360" y="220" width="40" height="40"/>
<rect class="vx-box" x="240" y="260" width="40" height="40"/>
<rect class="vx-box" x="280" y="260" width="40" height="40"/>
<rect class="vx-box" x="320" y="260" width="40" height="40"/>
<rect class="vx-box" x="360" y="260" width="40" height="40"/>
<rect class="vx-box" x="240" y="300" width="40" height="40"/>
<rect class="vx-box" x="280" y="300" width="40" height="40"/>
<rect class="vx-box" x="320" y="300" width="40" height="40"/>
<rect class="vx-box" x="360" y="300" width="40" height="40"/>
<rect class="vx-box" x="240" y="340" width="40" height="40"/>
<rect class="vx-box" x="280" y="340" width="40" height="40"/>
<rect class="vx-box" x="320" y="340" width="40" height="40"/>
<rect class="vx-box" x="360" y="340" width="40" height="40"/>
<text class="vx-text" x="240" y="405">c (rows i, columns j)</text>
<text class="vx-text-muted" x="100.0" y="212" text-anchor="middle">k0</text>
<text class="vx-text-muted" x="340.0" y="22" text-anchor="middle">j0</text>
<text class="vx-text-muted" x="232" y="94.0" text-anchor="end">k0</text>
<rect class="vx-box-strong" x="320" y="70" width="40" height="40"/>
<g class="vx-seq" style="--vx-i: 0; --vx-n: 8">
<rect class="vx-box-accent" x="80" y="220.0" width="40" height="20.0"/>
<rect class="vx-box-accent" x="320" y="220.0" width="40" height="20.0"/>
</g>
<g class="vx-seq" style="--vx-i: 1; --vx-n: 8">
<rect class="vx-box-accent" x="80" y="240.0" width="40" height="20.0"/>
<rect class="vx-box-accent" x="320" y="240.0" width="40" height="20.0"/>
</g>
<g class="vx-seq" style="--vx-i: 2; --vx-n: 8">
<rect class="vx-box-accent" x="80" y="260.0" width="40" height="20.0"/>
<rect class="vx-box-accent" x="320" y="260.0" width="40" height="20.0"/>
</g>
<g class="vx-seq" style="--vx-i: 3; --vx-n: 8">
<rect class="vx-box-accent" x="80" y="280.0" width="40" height="20.0"/>
<rect class="vx-box-accent" x="320" y="280.0" width="40" height="20.0"/>
</g>
<g class="vx-seq" style="--vx-i: 4; --vx-n: 8">
<rect class="vx-box-accent" x="80" y="300.0" width="40" height="20.0"/>
<rect class="vx-box-accent" x="320" y="300.0" width="40" height="20.0"/>
</g>
<g class="vx-seq" style="--vx-i: 5; --vx-n: 8">
<rect class="vx-box-accent" x="80" y="320.0" width="40" height="20.0"/>
<rect class="vx-box-accent" x="320" y="320.0" width="40" height="20.0"/>
</g>
<g class="vx-seq" style="--vx-i: 6; --vx-n: 8">
<rect class="vx-box-accent" x="80" y="340.0" width="40" height="20.0"/>
<rect class="vx-box-accent" x="320" y="340.0" width="40" height="20.0"/>
</g>
<g class="vx-seq" style="--vx-i: 7; --vx-n: 8">
<rect class="vx-box-accent" x="80" y="360.0" width="40" height="20.0"/>
<rect class="vx-box-accent" x="320" y="360.0" width="40" height="20.0"/>
</g>
<rect class="vx-box-strong" x="450" y="250" width="14" height="14"/>
<text class="vx-text" x="472" y="261">block of b (k0, j0): B &#215; B,</text>
<text class="vx-text" x="472" y="278">kept while i runs 0 to N &#8722; 1</text>
<rect class="vx-box-accent" x="450" y="300" width="14" height="14"/>
<text class="vx-text" x="472" y="311">row i of a and of c, B elements each:</text>
<text class="vx-text" x="472" y="328">used once per block, then passed by</text>
</svg>
<figcaption>Figure 1. The blocked loop <code>for k0, for j0, for i, for k, for j</code>, one block step. The block of <code>b</code> at <code>(k0, j0)</code> stays put while <code>i</code> sweeps every row; each row reads <code>B</code> elements of <code>a</code> and updates <code>B</code> elements of <code>c</code>, then the next row takes its place. Between two uses of an element of <code>b</code> lie only one row step: about <code>B &#215; B + 2B</code> elements, not the whole matrix.</figcaption>
</figure>

Count the new reuse distance for `B = 16` at `N = 512`. A block row of 16 `f32` values is 64 bytes, inside one 128-byte line, so the block of `b` spans 16 lines, and the segments of `a` and `c` one line each: about 18 lines, against 8,224 before. The reuse now fits in L1 many times over.

The price is paid elsewhere: row `i` of `c` is now visited once per block of `k`, so `c` is re-read `N / B` times, where the ikj kernel read it once. Lam, Rothberg and Wolf count the total: with no interference in the cache, the blocked loop reads about 2N³/B + N² words from memory, against 2N³ + N² for the unblocked loop in the worst case, a cache too small to hold even one row.[^lrw91] Doubling `B` halves the dominant term, so a larger block is better, up to the point where the block stops fitting.

## Keeping the order, keeping the bits

Here is the stage 10 kernel from [P7](p7-loop-transformations.md#making-the-stage-10-nest-perfect), after its step 4, blocked by hand with `B = 16`:

```vortex
// items: valid
fn multiply(a: &[f32; 64, 64], b: &[f32; 64, 64], c: &mut [f32; 64, 64]) {
    for row in 0..64 {
        for column in 0..64 {
            c[row, column] = 0.0;
        }
    }
    for k_block in 0..4 {
        let k_start = k_block * 16;
        for column_block in 0..4 {
            let column_start = column_block * 16;
            for row in 0..64 {
                for k in k_start..k_start + 16 {
                    for column in column_start..column_start + 16 {
                        c[row, column] += a[row, k] * b[k, column];
                    }
                }
            }
        }
    }
}
```

Two changes separate it from P7's step 4. The zeroing moved out of the `row` loop, one more fission, so that the multiply-add loops form a perfect nest of three. Then `k` and `column` were strip-mined and their strip loops moved outside `row`. Both are legal for the reason P7 gives: the band is fully permutable, since the only dependence is on `c[row, column]` along `k`.[^wl91]

Legal is not yet identical. [P7's table](p7-loop-transformations.md#putting-them-in-order) names the one way tiling can change Vortex's bits: accumulation blocks run out of order. Each element of `c` receives its products from every `k_block` in turn, so `k_block` must run from low to high, as the untiled `k` did. The order of `column_block` is free, because each column block owns a disjoint set of elements of `c`. A tile loop over a reduction inherits the reduction's direction; a tile loop over independent outputs does not. The example checks both, and also a block size that does not divide the matrix, whose last block on each axis is shorter: an **edge tile**.

--8<-- "includes/examples/optimize/p8-cache-blocking/tiled_matmul.cpp.md"

In the backward case every element of `c` sums the same products, in a different order, and floating-point addition is not associative, so the rounded results differ ([decision 56](../decisions/numbers.md#d56)).

Vortex asks one more thing of a tiling pass, the same thing it asks of interchange. Tiling reorders iterations, so if a bounds check in the nest could fail, the tiled program would reach a different failing iteration first and report a different error line, which [O1](o1-optimizer-contract.md#vortexs-list) counts as a change in behavior. The same holds for a `print` inside the nest. In the kernel above every index comes from a `for` over `0..64` into an extent of 64, so the range facts of [O8](o8-loops.md) prove that no check can fail. A pass that cannot prove it must refuse.

??? check "A reader tiles only `row` and `column`, leaving `k` as one untiled loop inside each tile. Does the order of the tile loops matter for the bits?"

    No. Both new tile loops run over disjoint blocks of `c`, and every element still receives all of its products from the one untiled `k` loop, in increasing order. Only a tile loop over `k`, the loop that carries the accumulation, has an order to keep.

## Choosing a block size from cache facts

The simplest model counts the blocked loop's working set and sets it at or under a cache's capacity. For the loop above, that is the block of `b` and a row segment of `c`: `B² + B` elements. A looser count keeps a `B` × `B` tile of each of `a`, `b` and `c`, `3B²` elements, which holds whatever loop order runs inside the tile. Solve either for the largest `B` under a budget.

A worked example: the M4 Pro's L1d holds 131,072 bytes, 32,768 `f32` values. For `B² + B ≤ 32,768`, `B = 180` gives 32,580 and `B = 181` gives 32,942, so the model allows 180. The three-tile count allows 104. The same arithmetic, for the 4 KiB toy cache used later in this chapter and for the M4 Pro's L2:

--8<-- "includes/examples/optimize/p8-cache-blocking/working_set.cpp.md"

Lam, Rothberg and Wolf quote the classic result behind this model: with a local memory of `C` words that software controls fully, the best block for matrix multiplication is roughly `√C`, and the same holds for a fully associative cache with least-recently-used replacement.[^lrw91] Real caches are neither. Two corrections from practice point the same way.

Lam, Rothberg and Wolf find that, across matrix sizes, the best fixed block for their direct-mapped caches used a small fraction of the cache, typically under 10%, and that trying to use the whole cache was wrong.[^lrw91]

Goto and van de Geijn keep the block their inner kernel reuses from L1 below half of that cache, because set associativity and the replacement policy limit how much of the cache one block can occupy.[^goto08]

Both models see capacity only. They know nothing of the cache's sets, of the matrix's row length, of the hardware prefetcher, or of another core's traffic through a shared L2. They give an upper bound to test, not an answer.

The L2 row of the table allows blocks over a thousand elements on a side, larger than the whole 512 × 512 example. One block size cannot serve two cache levels at once: a block chosen for L2 does not fit L1. [P12](p12-fast-gemm.md) nests one level of blocking for each cache and a third for the registers.

??? check "A reader doubles N without changing the cache. Does the model's largest B change?"

    No. `B² + B` depends only on `B`. A larger matrix needs more blocks, so the outer loops run longer, but the working set of one block step is the same, and so is the largest `B` that fits a given budget. Whether that `B` is still a good choice depends on the row length, which the next section takes up.

## Fitting is not enough: self-interference

Lam, Rothberg and Wolf divide the misses a blocked loop suffers beyond the unavoidable ones into two kinds.[^lrw91] **Cross-interference** is between different arrays: a line of `c` evicts a line of the `b` block. **Self-interference** is within one array: two rows of the same block of `b` land in the same set and evict each other. Self-interference is the kind that depends on the matrix's row length, and it is the one that makes a block that fits the capacity model thrash.

Start with the smallest case, a direct-mapped toy cache of 8 lines of 4 elements, where an element's line is `(address / 4) mod 8`. A tile of 8 rows, `stride` elements apart, is checked at four strides:

--8<-- "includes/examples/optimize/p8-cache-blocking/self_interference.cpp.md"

At stride 32 every row starts at a multiple of the cache's whole size, 32 elements, so every row maps to line 0: 8 rows share one line, each evicting the one before, although the tile is a quarter of the cache. Stride 33 is one element longer and spreads the rows over only two lines. Stride 20 gives every row its own line.

<figure class="vx-figure">
<svg viewBox="0 0 760 320" role="img" aria-label="How eight rows land on the toy cache's eight lines, at three strides" aria-describedby="p8-f2-desc">
<title id="p8-f2-title">How eight rows land on the toy cache's eight lines, at three strides</title>
<desc id="p8-f2-desc">A bar chart with three groups of eight bars, one group per stride: 32, 33 and 20. Each bar is one of the toy cache's eight lines, and its height is how many of the eight tile rows land on that line. At stride 32 all eight rows pile onto line zero, one tall bar and seven empty gaps. At stride 33 they split evenly between lines zero and one, two bars of height four. At stride 20 every row lands on its own line, eight short bars of height one, at or below the dashed line marking one row per line.</desc>
<text class="vx-text" x="40" y="20">Rows landing on each line, out of 8, at three strides</text>
<line class="vx-line" x1="50" y1="260" x2="660" y2="260"/>
<line class="vx-line" x1="50" y1="244" x2="660" y2="244" stroke-dasharray="4 4"/>
<text class="vx-text-muted" x="505" y="238">1 row per line: no collision</text>
<rect class="vx-box-bad" x="60" y="132" width="18" height="128"/>
<rect class="vx-box-bad" x="268" y="196" width="18" height="64"/>
<rect class="vx-box-bad" x="289" y="196" width="18" height="64"/>
<rect class="vx-box" x="476" y="244" width="18" height="16"/>
<rect class="vx-box" x="497" y="244" width="18" height="16"/>
<rect class="vx-box" x="518" y="244" width="18" height="16"/>
<rect class="vx-box" x="539" y="244" width="18" height="16"/>
<rect class="vx-box" x="560" y="244" width="18" height="16"/>
<rect class="vx-box" x="581" y="244" width="18" height="16"/>
<rect class="vx-box" x="602" y="244" width="18" height="16"/>
<rect class="vx-box" x="623" y="244" width="18" height="16"/>
<text class="vx-mono" x="69" y="126" text-anchor="middle">8</text>
<text class="vx-mono" x="277" y="190" text-anchor="middle">4</text>
<text class="vx-mono" x="298" y="190" text-anchor="middle">4</text>
<g class="vx-text-muted">
<text x="69" y="274" text-anchor="middle">0</text>
<text x="90" y="274" text-anchor="middle">1</text>
<text x="111" y="274" text-anchor="middle">2</text>
<text x="132" y="274" text-anchor="middle">3</text>
<text x="153" y="274" text-anchor="middle">4</text>
<text x="174" y="274" text-anchor="middle">5</text>
<text x="195" y="274" text-anchor="middle">6</text>
<text x="216" y="274" text-anchor="middle">7</text>
<text x="277" y="274" text-anchor="middle">0</text>
<text x="298" y="274" text-anchor="middle">1</text>
<text x="319" y="274" text-anchor="middle">2</text>
<text x="340" y="274" text-anchor="middle">3</text>
<text x="361" y="274" text-anchor="middle">4</text>
<text x="382" y="274" text-anchor="middle">5</text>
<text x="403" y="274" text-anchor="middle">6</text>
<text x="424" y="274" text-anchor="middle">7</text>
<text x="485" y="274" text-anchor="middle">0</text>
<text x="506" y="274" text-anchor="middle">1</text>
<text x="527" y="274" text-anchor="middle">2</text>
<text x="548" y="274" text-anchor="middle">3</text>
<text x="569" y="274" text-anchor="middle">4</text>
<text x="590" y="274" text-anchor="middle">5</text>
<text x="611" y="274" text-anchor="middle">6</text>
<text x="632" y="274" text-anchor="middle">7</text>
</g>
<text class="vx-text" x="144" y="296" text-anchor="middle">stride 32</text>
<text class="vx-text" x="352" y="296" text-anchor="middle">stride 33</text>
<text class="vx-text" x="560" y="296" text-anchor="middle">stride 20</text>
<text class="vx-text-muted" x="20" y="200" text-anchor="middle" transform="rotate(-90 20 200)">rows on this line</text>
</svg>
<figcaption>Figure 2. The three strides from <code>self_interference.cpp</code>, and how the toy cache's 8 lines share 8 rows at each one. Stride 32, a multiple of the whole toy cache, sends every row to line 0. Stride 20 sends each row to a line of its own.</figcaption>
</figure>

A set-associative cache changes the arithmetic in one place. An address chooses a set, not a line, so the rows that collide are those whose addresses differ by a multiple of the number of sets times the line size: the size of one **way**, the capacity divided by the associativity. Up to one row per way can share a set before they evict each other. A **critical stride** is a row length that is a multiple, or a near multiple, of that way size, so that many rows of a block fall into few sets.

Drepper measured one on a real machine. On his test machine, reading list elements spaced a multiple of 4,096 bytes apart cost about 10 cycles per element once more than eight elements were in use, against about 3 cycles when they fit in L1d; from that he read off an 8-way, 32 KiB L1d, whose way is 4,096 bytes.[^drepper-sets]

Lam, Rothberg and Wolf show the same shape for blocked matrix multiplication: the largest block without self-interference, which they call the **critical blocking factor**, changes sharply with small changes in the matrix size, and on a direct-mapped cache the interference is worst when the matrix dimension is a multiple of the cache size.[^lrw91] Associativity helps without curing it: in their model, with a block size chosen for each matrix size, a 4-way cache lowered the average miss rate by over 30% and halved its spread across matrix sizes, and they conclude that neither associativity nor longer lines removes the variation.[^lrw91]

??? check "The toy cache of the next example has 64 sets of 2 ways and 32-byte lines. At N = 128 `f32` values per row, how large can a block of `b` be, in rows, before its own rows start evicting each other?"

    One way is 64 × 32 = 2,048 bytes, and a row is 512 bytes, so rows `k` and `k + 4` fall into the same sets. A block of `B` rows puts about `B / 4` rows on each group of sets, and 2 ways hold 2 of them. So `B = 8` fits and `B = 12` does not, whatever the capacity model says.

## A sweep against the model

The capacity model said `B ≤ 31` for a 4 KiB cache. The example below checks it with a **cache simulator**: a program that plays the kernel's addresses through a model of a cache and counts misses. It is not the M4, but its output is the same on every machine, and every miss can be traced. It runs the untiled ikj kernel and the blocked loop at three sizes: `N = 128`, whose 512-byte rows are a critical stride for the toy cache; `N = 129`, rows of 516 bytes, four of which come to 16 bytes past a way; and `N = 136`, rows of 544 bytes, four of which come to four lines past a way.

--8<-- "includes/examples/optimize/p8-cache-blocking/miss_sweep.cpp.md"

Read the `N = 136` column first. The untiled kernel misses about once per 8 multiply-adds: one new line of `b` for every 8 `f32` values. Blocking with `B` from 16 to 24 cuts that to about a seventh. At `B = 32`, the size the capacity model calls the largest that fits, the misses double, and at `B = 48`, whose block alone is over twice the cache, blocking stops helping. The best blocks use between a quarter and three fifths of the cache, well short of all of it.

Now the `N = 128` column. No block size brings the misses below 94, and `B = 12` is worse than not tiling at all: exactly the collision the last check question counted. The three matrices also start at multiples of the way size, so the rows of `a` and `c` a row step touches land on the block's sets as well, cross-interference on top of self-interference. `N = 129` is almost as bad, because a near multiple of the way size collides too. The capacity model gave the same answer for all three sizes.

<figure class="vx-figure">
<svg viewBox="0 0 720 360" role="img" aria-label="Simulated misses against block size B for three cases: N 128 tiled, N 136 tiled, N 128 copied, with the capacity model&#39;s limit at B 31">
<text class="vx-text" x="80" y="22">Misses per 1,000 multiply-adds, toy cache (4 KiB, 2-way), from miss_sweep.cpp</text>
<line class="vx-line" x1="80" y1="300.0" x2="630" y2="300.0"/>
<line class="vx-line" x1="80" y1="300.0" x2="80" y2="60.0"/>
<text class="vx-text-muted" x="72" y="304.0" text-anchor="end">0</text>
<text class="vx-text-muted" x="72" y="244.0" text-anchor="end">50</text>
<text class="vx-text-muted" x="72" y="184.0" text-anchor="end">100</text>
<text class="vx-text-muted" x="72" y="124.0" text-anchor="end">150</text>
<text class="vx-text-muted" x="72" y="64.0" text-anchor="end">200</text>
<text class="vx-text-muted" x="168" y="318.0" text-anchor="middle">8</text>
<text class="vx-text-muted" x="212" y="318.0" text-anchor="middle">12</text>
<text class="vx-text-muted" x="256" y="318.0" text-anchor="middle">16</text>
<text class="vx-text-muted" x="344" y="318.0" text-anchor="middle">24</text>
<text class="vx-text-muted" x="432" y="318.0" text-anchor="middle">32</text>
<text class="vx-text-muted" x="608" y="318.0" text-anchor="middle">48</text>
<text class="vx-text-muted" x="355" y="338.0" text-anchor="middle">block size B</text>
<line class="vx-line" x1="80" y1="144" x2="630" y2="144" stroke-dasharray="2 4"/>
<text class="vx-text-muted" x="498" y="138">untiled</text>
<line class="vx-line" x1="421" y1="300.0" x2="421" y2="60.0" stroke-dasharray="6 4"/>
<text class="vx-text-muted" x="427" y="66">model: B &#8804; 31 fits</text>
<polyline class="vx-line" points="168,187 212,71 256,127 344,137 432,137 608,139"/>
<rect class="vx-box-bad" x="163" y="182" width="10" height="10"/>
<rect class="vx-box-bad" x="207" y="66" width="10" height="10"/>
<rect class="vx-box-bad" x="251" y="122" width="10" height="10"/>
<rect class="vx-box-bad" x="339" y="132" width="10" height="10"/>
<rect class="vx-box-bad" x="427" y="132" width="10" height="10"/>
<rect class="vx-box-bad" x="603" y="134" width="10" height="10"/>
<polyline class="vx-line" points="168,262 212,263 256,278 344,277 432,254 608,146"/>
<circle class="vx-dot" cx="168" cy="262" r="5"/>
<circle class="vx-dot" cx="212" cy="263" r="5"/>
<circle class="vx-dot" cx="256" cy="278" r="5"/>
<circle class="vx-dot" cx="344" cy="277" r="5"/>
<circle class="vx-dot" cx="432" cy="254" r="5"/>
<circle class="vx-dot" cx="608" cy="146" r="5"/>
<polyline class="vx-line" points="168,262 212,264 256,280 344,280 432,262 608,148"/>
<rect class="vx-box-strong" x="163" y="257" width="10" height="10"/>
<rect class="vx-box-strong" x="207" y="259" width="10" height="10"/>
<rect class="vx-box-strong" x="251" y="275" width="10" height="10"/>
<rect class="vx-box-strong" x="339" y="275" width="10" height="10"/>
<rect class="vx-box-strong" x="427" y="257" width="10" height="10"/>
<rect class="vx-box-strong" x="603" y="143" width="10" height="10"/>
<rect class="vx-box-bad" x="471" y="55" width="10" height="10"/>
<text class="vx-text" x="488" y="64">N = 128, tiled</text>
<circle class="vx-dot" cx="476" cy="80" r="5"/>
<text class="vx-text" x="488" y="84">N = 136, tiled</text>
<rect class="vx-box-strong" x="471" y="95" width="10" height="10"/>
<text class="vx-text" x="488" y="104">N = 128, copied</text>
</svg>
<figcaption>Figure 3. The table from <code>miss_sweep.cpp</code> drawn as three curves. At N = 136 (circles) the misses fall to about a seventh of the untiled count between B = 16 and B = 24, rise at B = 32, just past the capacity model&#8217;s limit, and return to the untiled count at B = 48. At N = 128 (dashed squares) tiling never gets far below the untiled count, and at B = 12 it is worse. Copying the block (solid squares) brings N = 128 back to the N = 136 curve.</figcaption>
</figure>

## Copying the block, and the TLB

The `copied` columns do one more thing: before each block step, they copy the `B` × `B` block of `b` into a small contiguous buffer and run the inner loops on the copy. The buffer's row length is `B`, chosen by the code, not `N`, chosen by the program, so the block's rows sit next to each other and cannot collide with one another until the buffer outgrows the cache. At `N = 128`, `B = 16` drops from 144 misses to 17, the same as at `N = 136`.

This is Lam, Rothberg and Wolf's **copy optimization**. Copying removes self-interference within the block altogether, and the copy's cost is small when the copied data is reused many times; in their model, with the block copied, the miss count barely moves as the block grows from a quarter of the cache to all of it.[^lrw91] Their closing advice is to pair blocking with copying whenever it applies, and otherwise to choose the largest block without self-interference.[^lrw91] The simulator shows the limit as well: at `B = 48` the copied block is larger than the cache, and copying no longer helps.

Goto and van de Geijn give copying, which they call **packing**, a second job. A block of a larger matrix is not contiguous, so addressing it needs many more TLB entries than its size requires; packing it into a contiguous buffer lets it be addressed with the fewest entries.[^goto08]

A worked case on the M4 Pro, with its 16 KiB pages (`sysctl hw.pagesize`, 2026-09-24): in a 4096 × 4096 `f32` matrix each row is 16,384 bytes, exactly one page, so a 64 × 64 block touches 64 pages. Packed, the same block is 64 × 64 × 4 = 16,384 bytes: a single page, if the buffer is aligned to one. The block size is then bounded by the TLB's reach as well as by the cache ([P2](p2-memory-hierarchy.md#the-tlb-caching-translations-not-data)). [P12](p12-fast-gemm.md) builds its whole kernel around packed blocks.

??? check "Why might copying the row segment of `c` into a buffer too be a poor trade, even though it would remove more interference?"

    Each element of the `b` block is reused `N` times once copied, so the copy's cost is spread over `N` uses. Each element of a `c` segment is reused only `B` times before the loop moves on, and it must be copied back afterwards. Lam, Rothberg and Wolf find that the second copy's overhead can outweigh the misses it saves.[^lrw91]

## What compilers do with tile sizes

[P7](p7-loop-transformations.md#what-llvm-does-with-these) found no tiling pass in LLVM's default pipelines. Polly, LLVM's polyhedral optimizer ([P9](p9-polyhedral-model.md)), is where LLVM's tiling lives: its home page says it performs "classical loop transformations, especially tiling and loop fusion".[^polly]

MLIR's `affine-loop-tile` pass takes either explicit tile sizes or a cache size in KiB and chooses sizes itself. In LLVM 18 its model divides the band's memory footprint by the cache size and uses the `n`-th root of that excess factor, for an `n`-deep band, as the tile size, adjusted down to a divisor of each trip count; the source comments call the model approximate, with a TODO to improve it.[^mlir-tiling] Here it tiles the same multiplication at two sizes for a 32 KiB cache:

--8<-- "includes/examples/optimize/p8-cache-blocking/tile_for_cache.mlir.md"

The steps of the outer loops are the chosen sizes: 1, 1 and 2 for the 64 × 64 case, and 2, 2 and 4 for 256 × 256. Run on the owner's machine with `mlir-opt` 18.1.8 on 2026-09-24, the 256 × 256 case gets 1, 1 and 2 for a 512 KiB cache and 4, 4 and 4 for an 8 KiB cache: under this model the tiles grow as the cache shrinks, the opposite of what a capacity model predicts. A tile-size model is a program like any other, and a sweep is how you find out what it does.

## For Vortex

!!! vortex "Exercise"

    **Build** loop tiling for perfect nests your compiler already handles in [P7](p7-loop-transformations.md#for-vortex): constant bounds, affine subscripts, and a band that the dependence test from [P6](p6-dependence-analysis.md) finds fully permutable.

    1. A tiling transformation for one band: strip-mine the chosen loops by given sizes and move their strip loops outward in a given order, with edge tiles when a size does not divide a trip count. Every tile loop over a loop that carries a floating-point accumulation runs in that loop's direction.
    2. The same gate as interchange: no check in the band can fail (use the range facts from [O8](o8-loops.md), or refuse), and the band calls no `print`.
    3. Tile sizes from the command line first. Then a chooser: the footprint of one tile step for the band's own arrays and loop order, set at or under a cache budget that also comes from the command line, never a size written into the compiler.
    4. A critical-stride warning: given a line size and a way size on the command line, a missed remark when rows of a tile would fall into the same sets, naming the array and its row length.
    5. A remark for every decision: the band, the sizes and the budget used, or the reason for refusing, such as "not tiled: `print` in the nest".

    **Not yet:** copying or packing ([P12](p12-fast-gemm.md)), a second level of tiles for a second cache ([P12](p12-fast-gemm.md)), a search over sizes instead of a model ([P15](p15-choosing-parameters.md)), threads over tiles ([P13](p13-multithreading.md)), and bands with bounds that are not constant.

    **Proof that it works:**

    - Differential tests: byte-identical output, tiled against untiled, for the stage 10 kernel at several sizes, and for a 3 × 5 matrix times a 5 × 7 one tiled by 4, whose every axis has an edge tile.
    - Golden refusal tests: a nest that prints, and a nest whose index can run out of bounds, are left alone with a remark; the out-of-bounds program reports the same error line with tiling on and off.
    - A golden test that the tile loop over `k` in the stage 10 kernel runs in increasing order, and a unit test of your chooser against `working_set.cpp`: for budgets of 4,096 and 131,072 bytes and the blocked ikj loop, it must allow 31 and 180.
    - A measurement of the untiled and tiled kernels across a sweep of sizes, following the method below and [P1](p1-measure-first.md), with the machine, compiler version and date.

## Measuring the effect

No speedup is claimed here: measure your own, following [P1](p1-measure-first.md).

1. Write the ikj kernel and the blocked kernel in C++, built with `-O2 -ffp-contract=off`, and check that they produce identical bits before timing anything, as `tiled_matmul.cpp` does.
2. Sweep `B` from 8 to 256 at a matrix size far larger than L1, such as N = 1024, and again at N = 1025 and N = 1040.
3. Find your L1d's way size before reading the results: time a walk over a few elements spaced `d` bytes apart for growing `d`, as Drepper did, and look for the spacing at which the time jumps once there are more elements than ways.[^drepper-sets] `sysctl` reports the size and the line, not the associativity.
4. Time many separate runs and report the median with a confidence interval, never the best run. Compute GFLOP/s as 2N³ / t for a time t in nanoseconds.

| Kernel | N | B | Median time | 95% interval | GFLOP/s |
| --- | --- | --- | --- | --- | --- |
| ikj, untiled | | | | | |
| blocked | | | | | |
| blocked | | | | | |
| blocked, block of `b` copied | | | | | |

Record the machine, compiler version, flags and date with the table. If the best `B` differs between N = 1024 and N = 1040, you have found a critical stride.

## Key ideas

!!! recap "Questions you can now answer"

    - **What is a reuse distance, and when does a reuse hit?** The number of distinct lines touched between two uses; in a fully associative LRU cache the reuse hits when that number is smaller than the cache's line count.
    - **Why does the ikj kernel at N = 512 miss L1 on `b` although its data fits in L2?** Each element of `b` is reused only after all of `b` has passed through L1, a reuse distance eight times the L1d on the M4 Pro.
    - **What does the blocked loop keep in cache?** One `B` × `B` block of `b`, reused by every row, plus a `B`-long segment of `c`; `a[i, k]` stays in a register.
    - **Which tile loop has an order to keep?** The one over `k`, which carries the accumulation into `c`; the tile loops over rows and columns may run in any order.
    - **Why is the capacity model an upper bound and not an answer?** It sees only capacity; rows whose spacing is a near multiple of the way size collide in a few sets and evict each other while most of the cache is empty.
    - **What does copying a block buy?** A row length chosen by the code, so no self-interference within the block, and a contiguous block that needs the fewest TLB entries; it costs one copy per block, worth it when the block is reused many times.

## Where this comes back

!!! next "You will use this again in"

    - [P9. The polyhedral model](p9-polyhedral-model.md): *tiling as a schedule*, *fully permutable band*
    - [P12. Anatomy of a fast GEMM](p12-fast-gemm.md): *packing*, *one level of blocking per cache*, *TLB reach*
    - [P13. Multithreading](p13-multithreading.md): *independent tiles*, *shared L2*
    - [P15. Choosing parameters: models or search](p15-choosing-parameters.md): *a model's choice against a sweep*
    - [M6. Loops: affine and scf](../mlir/m6-affine-and-scf.md): *affine-loop-tile*
    - [G10. The GPU matmul ladder](../gpu/g10-matmul-ladder.md): *a block kept in fast memory while rows stream past*

## Sources and further reading

Read Lam, Rothberg and Wolf first: sections 1 to 3 give the blocked loop, the interference model and the critical blocking factor, and section 6 the copy optimization, in eleven pages. Then read section 6.2.1 of Drepper for a blocked kernel measured on real hardware, and section 4.2 of Goto and van de Geijn for packing and the TLB, which [P12](p12-fast-gemm.md) takes further.

[^lrw91]: Monica S. Lam, Edward E. Rothberg and Michael E. Wolf, "The Cache Performance and Optimizations of Blocked Algorithms", *Proceedings of the Fourth International Conference on Architectural Support for Programming Languages and Operating Systems (ASPLOS IV)*, 1991: section 1.1 (the blocked loop and its memory traffic), 1.2 (the square-root rule for local memory), 2.1 (cross- and self-interference), 3 (self-interference, the critical blocking factor, the small fraction of the cache used), 5.1 (set associativity), 6 (copying) and 7 (conclusions). <https://doi.org/10.1145/106972.106981> (free copy: <https://suif.stanford.edu/papers/lam-asplos91.pdf>)
[^wl91]: Michael E. Wolf and Monica S. Lam, "A Data Locality Optimizing Algorithm", *Proceedings of the ACM SIGPLAN 1991 Conference on Programming Language Design and Implementation (PLDI)*, 1991, sections 1 to 4. <https://doi.org/10.1145/113445.113449> (free copy: <https://suif.stanford.edu/papers/wolf91a.pdf>)
[^drepper]: Ulrich Drepper, "What Every Programmer Should Know About Memory", version 1.0, 2007, section 6.2.1, Table 6.2 and footnote 28. <https://www.akkadia.org/drepper/cpumemory.pdf>
[^drepper-sets]: Ulrich Drepper, "What Every Programmer Should Know About Memory", version 1.0, 2007, section 6.2.1, Figure 6.5 and the text after it. <https://www.akkadia.org/drepper/cpumemory.pdf>
[^boehm]: Simon Boehm, "Fast Multidimensional Matrix Multiplication on CPU from Scratch", 2022: the results table and the sections on compiler flags and tiling. <https://siboehm.com/articles/22/Fast-MMM-on-CPU>
[^goto08]: Kazushige Goto and Robert A. van de Geijn, "Anatomy of High-Performance Matrix Multiplication", *ACM Transactions on Mathematical Software* 34(3), 2008: sections 4.2.2 and 4.2.3 (the TLB and packing) and 6.3 (choosing kc). <https://doi.org/10.1145/1356052.1356053> (free copy: <https://www.cs.utexas.edu/~flame/pubs/GotoTOMS_revision.pdf>)
[^polly]: The Polly Project, home page, read on 2026-09-24. <https://polly.llvm.org/>
[^mlir-tiling]: LLVM Project, `LoopTiling.cpp`, release/18.x branch: `LoopTiling::getTileSizes` and `adjustToDivisorsOfTripCounts`. <https://github.com/llvm/llvm-project/blob/release/18.x/mlir/lib/Dialect/Affine/Transforms/LoopTiling.cpp>

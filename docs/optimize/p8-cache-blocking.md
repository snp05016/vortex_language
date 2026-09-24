# P8. Cache blocking

<p class="page-intro">Tiling a loop nest so the data it reuses stays in a cache instead of falling out of it before the reuse happens, and choosing a tile size from cache facts instead of guessing.</p>

<p class="vx-meta" markdown="1">Level: Intermediate · Reading time: about 25 minutes · Builds on: [P2. The memory hierarchy](p2-memory-hierarchy.md), [P7. Loop transformations](p7-loop-transformations.md)</p>

???+ remember "Before you start, remember"

    ??? question "What turns reuse into locality?"

        Reuse belongs to the computation: the same data used by more than one iteration. Locality is reuse the cache manages to keep: the second use arrives before the cache has thrown the data out. A transformation cannot create or destroy reuse, only move uses closer together or further apart in time.

        Introduced in [P7. Loop transformations](p7-loop-transformations.md#the-same-work-in-a-different-order).

    ??? question "When may a band of loops be tiled?"

        When the band is fully permutable: every dependence inside it is lexicographically positive, and each one is either already carried by a loop outside the band or free of negative entries. Matrix multiplication's one dependence, `(=, =, <)` on `c`, qualifies for all three loops.

        Introduced in [P7. Loop transformations](p7-loop-transformations.md#strip-mining-and-tiling).

    ??? question "What would make a tiled matrix multiplication print different bits than the naive one?"

        Running its accumulation blocks out of order: if the blocks along `k` do not run from low to high, an element of `c` sums the same products in a different order, and floating-point addition is not associative.

        Introduced in [P7. Loop transformations](p7-loop-transformations.md#putting-them-in-order).

    ??? question "How many bytes does one cache line hold on the owner's M4 Pro, and how many `f32` values is that?"

        128 bytes, reported by `sysctl hw.cachelinesize`: room for 32 `f32` values.

        Introduced in [P7. Loop transformations](p7-loop-transformations.md#the-same-work-in-a-different-order).

!!! goals "In this chapter"

    - Explain why a loop nest that reuses more data than a cache can hold still misses, even when every individual access is legal and in order.
    - Turn a cache budget into a tile size with a footprint formula, and check the formula against a sweep.
    - Recognize self-interference: why a tile that is small enough for a cache can still thrash it, and what a compiler can do about the stride that causes it.
    - Choose an order for nested tiles that keeps Vortex's matrix multiplication bit-identical to the naive kernel.
    - State what a tile-size choice still owes the reader: a measurement, not a claim.

## A loop that reuses more than it keeps

Take three 512 × 512 matrices of `f32`, `a`, `b` and `c`, and the ikj kernel from [P7](p7-loop-transformations.md#making-the-stage-10-nest-perfect): for each row `i`, for each `k`, add `a[i, k]` times the whole row `b[k, :]` into the whole row `c[i, :]`. Each matrix is 512 × 512 × 4 bytes = 1 MiB.

Follow one element of `b` through the loop. `b[0, 0]` is read once while `i` is 0 and `k` is 0. It is not read again until `i` becomes 1, and by then the kernel has walked every other `k` for `i = 0`, touching a whole row of `b` and a whole row of `c` at each one: on the order of hundreds of thousands of other reads and writes lie between the two uses of `b[0, 0]`. That gap, measured in intervening accesses, is the pair's **reuse distance**. A cache turns reuse into locality only when the reused data survives for the whole distance, and 1 MiB does not survive in a cache with a 128 KiB budget: `sysctl hw.perflevel0.l1dcachesize` reports exactly that for the P-core L1d on the owner's Apple M4 Pro, checked on 2026-09-24. The whole matrix `b` is far larger than the cache well before `i` reaches 1, so `b[0, 0]` is gone by the time it is needed again, and the kernel fetches it from a slower level every single time: a working set that outlives its own cache.

The three matrices together, 3 MiB, fit the same machine's L2 (16 MiB, shared by four P-cores, from `sysctl hw.perflevel0.l2cachesize hw.perflevel0.cpusperl2`, same date). So it is not that the problem is too big. It is that the reuse distance, at this loop order, is longer than the smaller, faster cache can bridge, even though the total data the loop ever touches fits the larger, slower one. [P7](p7-loop-transformations.md) chose the order that walks memory unit-stride; this chapter changes how much of that order runs before doubling back, so that the data revisited is still where it was left.

## Tiling shortens the distance

**Tiling**, introduced in [P7](p7-loop-transformations.md#strip-mining-and-tiling) as strip-mining combined with permutation, is the transformation that does this: it turns one long sweep of the reduction dimension into several short ones, and moves the same amount of surrounding work inside each short sweep before advancing. For matrix multiplication, tiling `i`, `j` and `k` by `T` replaces the 512-long sweep of `k` for one row with `⌈512 / T⌉` sweeps of length `T`, each paired with a `T`-row, `T`-column block of the output.

<figure class="vx-figure">
<svg viewBox="0 0 760 340" role="img" aria-label="A C tile held resident while A and B tiles stream past it" aria-describedby="p8-f1-desc">
<title id="p8-f1-title">A C tile held resident while A and B tiles stream past it</title>
<desc id="p8-f1-desc">Three 3 by 3 grids of tiles: A, indexed by i and k, at the lower left; B, indexed by k and j, at the upper right; C, indexed by i and j, at the lower right, directly under B and beside A. The (i0, j0) tile of C is outlined and stays highlighted throughout. For k_t equal to 0, then 1, then 2 in turn, the (i0, k_t) tile of A and the (k_t, j0) tile of B light up together, then hand off to the next k_t. Two arrows show data flowing from the current A tile and the current B tile into the resident C tile.</desc>
<defs><marker id="p8-f1-head" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker></defs>
<text class="vx-text" x="40" y="18">One tile of C stays put; the tiles that feed it stream through</text>
<text class="vx-text" x="40" y="190">A (i &#215; k)</text>
<text class="vx-text" x="220" y="28">B (k &#215; j)</text>
<text class="vx-text" x="220" y="190">C (i &#215; j)</text>
<text class="vx-text-muted" x="61" y="196" text-anchor="middle">k0</text>
<text class="vx-text-muted" x="103" y="196" text-anchor="middle">k1</text>
<text class="vx-text-muted" x="145" y="196" text-anchor="middle">k2</text>
<text class="vx-text-muted" x="34" y="226" text-anchor="end">i0</text>
<text class="vx-text-muted" x="34" y="268" text-anchor="end">i1</text>
<text class="vx-text-muted" x="34" y="310" text-anchor="end">i2</text>
<text class="vx-text-muted" x="214" y="66" text-anchor="end">k0</text>
<text class="vx-text-muted" x="214" y="108" text-anchor="end">k1</text>
<text class="vx-text-muted" x="214" y="150" text-anchor="end">k2</text>
<text class="vx-text-muted" x="241" y="36" text-anchor="middle">j0</text>
<text class="vx-text-muted" x="283" y="36" text-anchor="middle">j1</text>
<text class="vx-text-muted" x="325" y="36" text-anchor="middle">j2</text>
<rect class="vx-box" x="40" y="200" width="42" height="42"/>
<rect class="vx-box" x="82" y="200" width="42" height="42"/>
<rect class="vx-box" x="124" y="200" width="42" height="42"/>
<rect class="vx-box" x="40" y="242" width="42" height="42"/>
<rect class="vx-box" x="82" y="242" width="42" height="42"/>
<rect class="vx-box" x="124" y="242" width="42" height="42"/>
<rect class="vx-box" x="40" y="284" width="42" height="42"/>
<rect class="vx-box" x="82" y="284" width="42" height="42"/>
<rect class="vx-box" x="124" y="284" width="42" height="42"/>
<rect class="vx-box" x="220" y="40" width="42" height="42"/>
<rect class="vx-box" x="262" y="40" width="42" height="42"/>
<rect class="vx-box" x="304" y="40" width="42" height="42"/>
<rect class="vx-box" x="220" y="82" width="42" height="42"/>
<rect class="vx-box" x="262" y="82" width="42" height="42"/>
<rect class="vx-box" x="304" y="82" width="42" height="42"/>
<rect class="vx-box" x="220" y="124" width="42" height="42"/>
<rect class="vx-box" x="262" y="124" width="42" height="42"/>
<rect class="vx-box" x="304" y="124" width="42" height="42"/>
<rect class="vx-box" x="220" y="200" width="42" height="42"/>
<rect class="vx-box" x="262" y="200" width="42" height="42"/>
<rect class="vx-box" x="304" y="200" width="42" height="42"/>
<rect class="vx-box" x="220" y="242" width="42" height="42"/>
<rect class="vx-box" x="262" y="242" width="42" height="42"/>
<rect class="vx-box" x="304" y="242" width="42" height="42"/>
<rect class="vx-box" x="220" y="284" width="42" height="42"/>
<rect class="vx-box" x="262" y="284" width="42" height="42"/>
<rect class="vx-box" x="304" y="284" width="42" height="42"/>
<line class="vx-line" x1="166" y1="221" x2="216" y2="221" marker-end="url(#p8-f1-head)"/>
<line class="vx-line" x1="241" y1="166" x2="241" y2="196" marker-end="url(#p8-f1-head)"/>
<rect class="vx-box-strong" x="220" y="200" width="42" height="42"/>
<g class="vx-seq" style="--vx-i: 0; --vx-n: 3">
<rect class="vx-box-accent" x="40" y="200" width="42" height="42"/>
<rect class="vx-box-accent" x="220" y="40" width="42" height="42"/>
</g>
<g class="vx-seq" style="--vx-i: 1; --vx-n: 3">
<rect class="vx-box-accent" x="82" y="200" width="42" height="42"/>
<rect class="vx-box-accent" x="220" y="82" width="42" height="42"/>
</g>
<g class="vx-seq" style="--vx-i: 2; --vx-n: 3">
<rect class="vx-box-accent" x="124" y="200" width="42" height="42"/>
<rect class="vx-box-accent" x="220" y="124" width="42" height="42"/>
</g>
<rect class="vx-box-strong" x="420" y="70" width="14" height="14"/>
<text class="vx-text" x="440" y="81">C tile (i0, j0): resident for the whole k sweep</text>
<rect class="vx-box-accent" x="420" y="110" width="14" height="14"/>
<text class="vx-text" x="440" y="121">A tile (i0, k_t) and B tile (k_t, j0):</text>
<text class="vx-text" x="440" y="137">stream through, one k_t at a time</text>
</svg>
<figcaption>Figure 1. Tiling the stage 10 kernel at the granularity of macro-tiles. The (i0, j0) tile of <code>c</code> is written once and read back every step, so it is worth keeping resident. The matching tiles of <code>a</code> and <code>b</code> change every step of the outer <code>k</code>-tile loop, so only one of each needs to be resident at a time.</figcaption>
</figure>

The tile of `c` at `(i0, j0)` is read and written on every one of the three `k`-tile steps in the figure, so it is worth the cache's while to keep it. The matching tiles of `a` and `b` change every step, so the cache only ever needs the current one. Within one step, that step's work is exactly what the register-blocked kernel from [P7](p7-loop-transformations.md#unroll-and-jam-and-register-blocks) does at a smaller grain: this chapter's tiles are sized for a cache, P7's `mr` × `nr` blocks for a register file, and [P12](p12-fast-gemm.md) nests both.

??? check "The three matrices in the example above are 512 × 512. Would tiling change anything at N = 32, where the whole problem is 3 × 32 × 32 × 4 = 12,288 bytes?"

    No. 12,288 bytes is under the 128 KiB L1d budget on its own, so every element's reuse distance already fits comfortably inside the fastest cache. Tiling shortens a reuse distance that is otherwise too long; it has nothing to shorten here.

## Choosing a size from cache facts

Tiling matrix multiplication touches a `T`-row, `T`-column tile of `c`, a `T`-row, `T`-column tile of `a`, and a `T`-row, `T`-column tile of `b`: three `T` × `T` blocks of `f32`, `12 T²` bytes. Set that at or under a budget and solve for `T`:

$$
12\,T^2 \le \text{budget} \quad\Longrightarrow\quad T \le \sqrt{\text{budget} / 12}
$$

Checked against the same L1d budget as above, 131,072 bytes, the bound gives `T ≤ 104.5`: at `T = 104` the three tiles cost 12 × 104² = 129,792 bytes, 99.0% of the budget; at `T = 105` they cost 132,300 bytes, over it. The model is a starting point, not the last word, because it assumes the tile is the only thing in the cache: nothing shares it with the tile in this accounting, not the loop's own stack slots, not another core's traffic through a shared L2, not whatever the hardware prefetcher decided to bring in early. The example below builds the formula, then sweeps a range of tile sizes and checks each one's footprint directly, the way a reader should check any model against the thing it models.

--8<-- "includes/examples/optimize/p8-cache-blocking/working_set.cpp.md"

The same formula, read against the L2 budget (16,777,216 bytes), allows a tile past a thousand rows and columns on a side: a single level of tiling cannot use both budgets at once, because a `T` chosen for L2 is already too large for L1. [P12](p12-fast-gemm.md) nests two tiles, one sized for each level, and a third for the registers.

[P7](p7-loop-transformations.md#what-llvm-does-with-these) found no pass named for tiling in LLVM's mainline pipeline. Polly, an out-of-tree LLVM component built on the polyhedral model ([P9](p9-polyhedral-model.md)), is where that transformation lives instead: its own project page states that it performs "classical loop transformations, especially tiling and loop fusion", with "native support for handling full/partial tile separation" when it generates code for a tiled loop's edge cases.[^polly]

??? check "A reader doubles every matrix dimension without changing the tile size T. Does the footprint formula's answer for T change?"

    No. `12 T²` depends only on the tile side `T`, not on the matrix dimensions `N`. A larger matrix needs more tiles, and the outer loops run longer, but each tile's footprint, and so the largest `T` a given cache budget allows, stays the same.

## Keeping the order, keeping the bits

[P7](p7-loop-transformations.md#strip-mining-and-tiling) established that tiling a fully permutable band is legal, and that matrix multiplication's `(=, =, <)` dependence qualifies every loop for it. Legal is not yet identical: the same page's summary table names the one way tiling can still change Vortex's bits, "accumulation blocks run out of order." Tiling is strip-mining plus permutation, so it introduces no new dependence, but it does introduce a new loop, over tiles, and that loop must itself respect the old one's direction. The `i`-tiles and `j`-tiles may run in any order, because each covers a disjoint set of `c` elements. The `k`-tiles carry the one dependence there is, so they must run from low to high, exactly like the un-tiled `k` loop did.

Extending [P7's step 4](p7-loop-transformations.md#making-the-stage-10-nest-perfect) with one tile loop over `k` makes this concrete:

```vortex
// items: valid
fn multiply(a: &[f32; 64, 64], b: &[f32; 64, 64], c: &mut [f32; 64, 64]) {
    for row in 0..64 {
        for column in 0..64 {
            c[row, column] = 0.0;
        }
    }
    for k_tile in 0..4 {
        let k_start = k_tile * 16;
        for k in k_start..k_start + 16 {
            for row in 0..64 {
                for column in 0..64 {
                    c[row, column] += a[row, k] * b[k, column];
                }
            }
        }
    }
}
```

`k_tile` runs 0, 1, 2, 3: increasing, as every element of `c` needs. Nothing here yet limits how much of `a`, `b` or `c` is touched inside one `k_tile` step, since `row` and `column` still run over the whole matrix; a full tiling adds bands for them too, strip-mined the same way and moved outside `k_tile`, which the example below checks for a range of sizes including ones that do not divide the matrix evenly.

--8<-- "includes/examples/optimize/p8-cache-blocking/tiled_matmul.cpp.md"

The last line of that example's output is the failure case: the same tiling, with its `k`-tiles visited from high to low. Strip-mining `k` is still legal there; only the new loop's own order is not. Every `c` element ends up with the same set of products, summed in a different order, and the output shows what [P7](p7-loop-transformations.md#what-a-vortex-transformation-must-also-keep) already argued in general: a Vortex compiler that tiles a reduction must prove, or otherwise guarantee, that the reduction's own tiles run in the reduction's own order.

??? check "A reader tiles only i and j, leaving k as one untiled sweep. Does the k order question in this section still apply?"

    No, or rather it is settled before it arises: with k untiled there is only one k loop, and it already runs from low to high as written. The new question only comes from adding a loop over k-tiles; tiling i and j alone adds loops over disjoint blocks of c, which may run in any order.

## Fitting is not enough: self-interference

Suppose a tile clears the footprint formula's bound with room to spare, and the kernel still misses more than expected. Lam, Rothberg and Wolf name the reason: a real cache is not one pool a tile either fits or does not.[^lrw91] It is built from a fixed number of **lines**, and an address does not choose freely among them: which line an address can occupy is decided by a handful of its middle bits, so any two addresses that share those bits compete for the same lines, no matter how much of the rest of the cache sits empty. Lam, Rothberg and Wolf call this **self-interference**: it depends on tile size and on the matrix's own stride, and choosing a tile size from "use the whole cache" reasoning, total capacity alone, is the wrong rule.[^lrw91]

A direct-mapped cache, one line per index, makes the effect easiest to see, because two addresses that share an index cannot both stay: the second evicts the first. Most real caches soften this by giving each index several lines, called **ways** (an *n*-way set-associative cache can hold *n* colliding addresses before it must evict one), but the addresses that compete are decided by the same arithmetic either way; a set-associative cache buys headroom, not immunity.

The toy cache below has 8 lines of 4 elements each, direct-mapped for clarity, so an address's line is `(address / 4) mod 8`. A tile of 8 rows, `stride` elements apart, is checked at three strides:

--8<-- "includes/examples/optimize/p8-cache-blocking/self_interference.cpp.md"

At stride 32, every row's address is a multiple of the toy cache's whole size, so every row maps to line 0: 8 rows sharing one line, each new row evicting the row before it, even though the whole 8-row tile is a quarter of the cache's capacity. One element added to the stride, 33, already spreads the same 8 rows across two lines instead of one.

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

Real strides are not toy numbers, but the arithmetic is the same, and Drepper's warning about matrix code carries the same shape: a stride that is a multiple, or a near-multiple, of the cache's own size is the one to watch, because it is exactly the stride that makes many rows share few lines.[^drepper] A **critical stride** is a stride that does this. A row-major matrix's row length is the stride between two of its rows ([decision 43](../decisions/arrays.md#d43)), so a matrix whose row length happens to be a multiple of the cache's size is worth checking for interference before trusting a tile's footprint alone.

Two responses do not require guessing at the hardware's associativity. One is in this chapter's own scope: choose tile sizes and loop bounds that avoid known-bad strides, or verify with a sweep, as the exercise below asks. The other belongs to [P12](p12-fast-gemm.md): copy, or **pack**, the tile into a small contiguous buffer before working on it. A packed buffer's stride is whatever the packing code chooses, not whatever the source matrix happened to have, and it also turns a scattered walk of a strided tile into a handful of long, prefetcher-friendly reads. Goto and van de Geijn build their whole design around this: packing avoids both self-interference and translation lookaside buffer (TLB) misses, at the cost of the copy itself.[^goto08] On the owner's M4 Pro, `sysctl hw.pagesize` reports a 16 KiB page (checked 2026-09-24), which sets how far apart two addresses can be before they risk needing a second TLB entry: the same kind of arithmetic as a cache line, at a coarser grain.

??? check "A matrix has 1,024 columns of f32 (4,096 bytes per row) and the toy cache's line size in this section is 4 elements (16 bytes). Is 1,024 columns a critical stride for that toy cache's 8 lines?"

    Yes. 4,096 / 16 = 256 lines' worth of stride, and 256 is a multiple of 8, so every row of the matrix maps to the same one of the 8 lines: the worst case in the figure above, at a realistic size instead of a toy one.

## For Vortex

!!! vortex "Exercise"

    **Build** a tile-size chooser and a tiling pass for nests your compiler already recognizes as legal to tile ([P7](p7-loop-transformations.md#strip-mining-and-tiling)'s fully-permutable-band test), for square tiles over constant-bound, affine-subscript loops.

    1. A footprint formula for the nest being tiled, taking the number of arrays touched per tile and their element size, and a place to plug in a cache budget: read the budget from the host at compile time, the way [P2](p2-memory-hierarchy.md) covers, not as a constant written into the compiler.
    2. A chooser that solves the formula for the largest tile at or under the budget, the way `working_set.cpp` does by search, and reports its answer and the budget it used in a remark.
    3. A tiling transformation for one band at a time: strip-mine each loop in the band by the chosen size, move the strip loops outside, and keep every reduction's tile loop in the reduction's own direction, refusing (with a remark) any nest where that cannot be guaranteed.
    4. Edge tiles: a matrix whose extent does not divide the tile size evenly needs a final, shorter tile on each axis, not a crash or a silently dropped row or column.

    **Not yet:** packing ([P12](p12-fast-gemm.md)), a second tile size for a second cache level ([P12](p12-fast-gemm.md)), and a search over tile sizes instead of a formula ([P15](p15-choosing-parameters.md)).

    **Proof that it works:**

    - Differential tests: byte-identical output, tiled against untiled, for the stage 10 kernel at several tile sizes, including at least one that does not divide the matrix dimension.
    - A golden test that a k-tile loop is only ever emitted in increasing order, and that a hand-constructed "decreasing" variant is rejected by whatever check your pass relies on to guarantee this (or is never reachable through the pass's own code paths, if the pass only ever emits one order).
    - A footprint check: for a chosen tile size and a stated budget, assert the computed footprint is at or under the budget, for at least one budget where the naive T (the whole matrix) would not be.
    - A measurement of the tiled and untiled kernels across a small sweep of tile sizes, following [P1](p1-measure-first.md), with the machine, compiler version and date, looking for the kind of spike a critical stride would cause.

## Measuring the effect

No speedup is claimed here: measure your own, following [P1](p1-measure-first.md).

1. Build the naive and tiled C++ kernels with `-O2 -ffp-contract=off`, and confirm they produce identical bits before timing anything, as `tiled_matmul.cpp` does.
2. Sweep the tile size from 8 to 256, at a matrix size the tile does not divide evenly, and again at one it does.
3. Run the same sweep at two matrix sizes with row lengths one element apart (N and N + 1, as in the self-interference section), and look for a spike at the size whose row length is a critical stride for your cache.
4. Time many separate runs and report the median with a confidence interval, never the best run.

| Tile size | N | Median time | 95% interval | GFLOP/s |
| --- | --- | --- | --- | --- |
| | | | | |
| | | | | |

Record the machine, compiler version, flags and date with the table.

## Key ideas

!!! recap "Questions you can now answer"

    - **What does tiling change that plain loop reordering does not?** How much of a reduction runs before the loop returns to data it already used, so that the return finds the data still in cache.
    - **Where does a tile-size formula come from?** Count the bytes a tile of each array touched costs, in terms of the tile's side, and solve for the largest side at or under a cache budget.
    - **Why is fitting the footprint not enough?** A cache is built from a fixed number of lines; addresses that share a line's index compete for it regardless of how much of the rest of the cache is empty. This is self-interference.
    - **What is a critical stride?** A stride, such as a matrix's row length, that is a multiple (or near-multiple) of the cache's size, so that many rows collide on the same few lines.
    - **What must a k-tile loop keep, that an i-tile or j-tile loop need not?** Increasing order: it carries the one dependence the nest has, so it must run in the direction the un-tiled loop did.
    - **What fixes self-interference without guessing the hardware's associativity?** Avoiding known-bad strides and sizes, or copying (packing) the tile into a buffer whose stride the compiler chooses.

## Where this comes back

!!! next "You will use this again in"

    - [P9. The polyhedral model](p9-polyhedral-model.md): *tile*, *fully permutable band*, *schedule*
    - [P12. Anatomy of a fast GEMM](p12-fast-gemm.md): *tile-size formula*, *packing*, *two cache levels*
    - [P13. Multithreading](p13-multithreading.md): *tile*, parallelizing over independent tiles
    - [P15. Choosing parameters: models or search](p15-choosing-parameters.md): *tile-size formula*, comparing a model's choice with a sweep
    - [M6. Loops: affine and scf](../mlir/m6-affine-and-scf.md): *tile*, *fully permutable band*
    - [G10. The GPU matmul ladder](../gpu/g10-matmul-ladder.md): *tile*, shared-memory tiling as the GPU analog of a cache tile

## Sources and further reading

For the cache-conflict argument in depth, read Lam, Rothberg and Wolf; for the packing that avoids it, read Goto and van de Geijn.

[^lrw91]: Monica S. Lam, Edward E. Rothberg and Michael E. Wolf, "The Cache Performance and Optimizations of Blocked Algorithms", *Proceedings of the Fourth International Conference on Architectural Support for Programming Languages and Operating Systems (ASPLOS)*, 1991, section 3. <https://doi.org/10.1145/106972.106981> (free copy: <https://suif.stanford.edu/papers/lam-asplos91.pdf>)
[^drepper]: Ulrich Drepper, "What Every Programmer Should Know About Memory", version 1.0, 2007, section 6.2.1. <https://www.akkadia.org/drepper/cpumemory.pdf>
[^goto08]: Kazushige Goto and Robert A. van de Geijn, "Anatomy of High-Performance Matrix Multiplication", *ACM Transactions on Mathematical Software* 34(3), 2008. <https://doi.org/10.1145/1356052.1356053> (free copy: <https://www.cs.utexas.edu/~flame/pubs/GotoTOMS_revision.pdf>)
[^polly]: The Polly Project, home page: "Polly performs classical loop transformations, especially tiling and loop fusion, to improve data-locality", and its "native support for handling full/partial tile separation" during code generation. <https://polly.llvm.org/>

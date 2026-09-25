# G10. The GPU matmul ladder

<p class="page-intro">A matrix product on a GPU goes from about one percent of the vendor library's speed to within a few percent of it in nine rungs, and every rung answers one hardware fact: how many times each value is read, and from which memory. This chapter climbs the ladder by counting loads, shows which rungs a Vortex compiler may take on its own because they never change a bit of the answer, and marks the rungs that would.</p>

<p class="vx-meta" markdown="1">Level: Advanced · Reading time: about 55 minutes · Builds on: [G4. Memory performance: coalescing and bank conflicts](g4-memory-performance.md), [G5. Occupancy and latency hiding](g5-occupancy.md)</p>

???+ remember "Before you start, remember"

    ??? question "What does one warp-wide load cost in global memory?"

        The number of distinct aligned 32-byte sectors its 32 addresses touch. Neighbouring lanes reading neighbouring `f32` values need 4 sectors; lanes 32 bytes or more apart need 32.

        Introduced in [G4. Memory performance: coalescing and bank conflicts](g4-memory-performance.md).

    ??? question "What is shared memory, and when may a thread read a value another thread stored there?"

        A small on-chip scratchpad that every thread of one block can read and write. A thread may read another thread's value only after both have passed a barrier that orders the store before the load.

        Introduced in [G3. The GPU memory hierarchy](g3-memory-hierarchy.md).

    ??? question "When do the lanes of a warp conflict in shared memory?"

        When two lanes want different words in the same bank. A stride of `s` four-byte words across the lanes costs gcd(s, 32) passes, so a column of a 32 × 32 `f32` tile costs 32 and a column of a 32 × 33 tile costs 1.

        Introduced in [G4. Memory performance: coalescing and bank conflicts](g4-memory-performance.md).

    ??? question "Why can a kernel that keeps more values in registers run fewer warps at once?"

        The register file is shared by every thread resident on a core. The more registers each thread needs, the fewer threads fit, and the fewer warps are left to hide a stalled warp's latency.

        Introduced in [G5. Occupancy and latency hiding](g5-occupancy.md).

    ??? question "May a Vortex compiler change the order in which `sum += a[row, k] * b[k, column]` adds its terms, or fuse the multiply and the add?"

        No. Every floating-point operation is one IEEE 754 operation, rounded once, in the order the program writes it. Contraction into a fused multiply-add, reassociation and reordering are all forbidden in v0.1.

        Introduced in [Numbers, literals and casts, decision 56](../decisions/numbers.md#d56).

!!! goals "In this chapter"

    - Count, by hand, the loads from global memory and from shared memory that a 64 × 64 × 64 product needs at rungs 1, 3 and 5, and predict the counts for other tile sizes.
    - Explain each rung of the ladder by the one hardware fact it answers, from coalescing through block, thread and warp tiles to autotuning.
    - Compute the arithmetic intensity of a tile at any level of the hierarchy with one formula.
    - Distinguish the schedule changes that keep every bit of the answer from the ones that change it: splitting the `k` loop, tensor cores and fast-math compiler defaults.
    - Specify a tile planner for Vortex that checks a choice of tile sizes against a target's limits, without generating GPU code.

## The kernel this chapter tunes

Every rung of this chapter computes the same function, the matrix product from [stage 10](../compiler/guide/stage-10-matrix-multiplication.md#the-program-the-milestone-asks-for), at the 64 × 64 size [G4](g4-memory-performance.md#which-index-runs-across-the-warp) used:

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

It does 64 × 64 × 64 = 262,144 multiply-adds, 524,288 floating-point operations (FLOPs) when the multiply and the add count separately, and it needs only 8,192 distinct input values, 32 KiB. Each input value is used 64 times. The whole chapter is about where those 64 uses read the value from.

A **ladder**, in this chapter, is a sequence of GPU implementations of one function in which each rung keeps the arithmetic and changes only the **schedule**: which thread computes which output, in what order, and through which memory each value passes on the way. Simon Boehm built and measured such a ladder for single-precision matrix multiplication in December 2022, from a first working CUDA kernel to one at 93.7 percent of NVIDIA's cuBLAS library, and published every kernel.[^boehm] This chapter follows his rungs and adds the question he did not need to ask: which rungs keep the answer bit for bit.

Two differences between his kernels and `multiply` matter later. His kernels compute the full library operation `C = αAB + βC` on 4092 × 4092 matrices, and nvcc compiled their inner loops to fused multiply-add instructions (`fma.rn.f32` in the PTX he shows for his third kernel).[^boehm] A Vortex kernel may not fuse ([decision 56](../decisions/numbers.md#d56)); the section [what the ladder never changes](#what-the-ladder-never-changes) returns to what that costs.

**Rung 0** comes before any GPU code: a CPU reference and a comparison. Because every rung up to rung 9 keeps each output's operations and their order, the comparison for those rungs is bit for bit, not within a tolerance. A tolerance would hide exactly the bugs this chapter warns about.

## The ladder at a glance

The table names the fact each rung answers, the kernel of Boehm's worklog that implements it, and his measurement, from the summary table of his worklog: an NVIDIA RTX A6000, 4092 × 4092 `f32` matrices, no tensor cores, cuBLAS at 23,250 GFLOP/s.[^boehm] These are one person's numbers on one GPU. Read them for the shape of the ladder, and reproduce the shape on your own hardware ([measuring it](#measuring-it)).

| Rung | One hardware fact | Boehm's kernel | GFLOP/s (% of cuBLAS) |
|---|---|---|---|
| 0 | A reference answer exists before any GPU code does | - | - |
| 1 | Naive: one thread per output, every operand from global memory | 1 | 309 (1.3%) |
| 2 | Coalescing: neighbouring lanes read neighbouring addresses | 2 | 1,987 (8.5%) |
| 3 | Block tile: a block reuses a tile it keeps in shared memory | 3 | 2,980 (12.8%) |
| 4 | 1-D thread tile: one thread produces a column of outputs | 4 | 8,475 (36.5%) |
| 5 | 2-D thread tile: an outer product per thread, not a dot product | 5 | 15,972 (68.7%) |
| 6 | Vector loads: 16 bytes per instruction instead of 4 | 6 | 18,237 (78.4%) |
| 7 | Bank conflicts in the heavily reused shared tile | 7, 8 | not in his table |
| 8 | Autotuning: search the tile sizes rungs 3 to 7 introduced | 9 | 19,721 (84.8%) |
| 9 | Warp tile: a tile between the block tile and the thread tile | 10 | 21,779 (93.7%) |

<figure class="vx-figure">
<svg viewBox="0 0 760 340" role="img" aria-label="Bar chart of Boehm's kernels as a percentage of cuBLAS: 1.3, 8.5, 12.8, 36.5, 68.7, 78.4, 84.8 and 93.7 percent for rungs 1, 2, 3, 4, 5, 6, 8 and 9.">
<text class="vx-text" x="20" y="24">Share of cuBLAS reached, per rung (Boehm, RTX A6000, 4092 &#215; 4092 f32)</text>
<text class="vx-text" x="20" y="69">1  naive</text>
<rect class="vx-box" x="230" y="54" width="6.0" height="20"/>
<text class="vx-mono" x="244.0" y="69">1.3%</text>
<text class="vx-text" x="20" y="101">2  coalescing</text>
<rect class="vx-box" x="230" y="86" width="39.1" height="20"/>
<text class="vx-mono" x="277.1" y="101">8.5%</text>
<text class="vx-text" x="20" y="133">3  shared-memory tile</text>
<rect class="vx-box" x="230" y="118" width="58.9" height="20"/>
<text class="vx-mono" x="296.9" y="133">12.8%</text>
<text class="vx-text" x="20" y="165">4  1-D register tile</text>
<rect class="vx-box-accent" x="230" y="150" width="167.9" height="20"/>
<text class="vx-mono" x="405.9" y="165">36.5%</text>
<text class="vx-text" x="20" y="197">5  2-D register tile</text>
<rect class="vx-box-accent" x="230" y="182" width="316.0" height="20"/>
<text class="vx-mono" x="554.0" y="197">68.7%</text>
<text class="vx-text" x="20" y="229">6  vector loads</text>
<rect class="vx-box" x="230" y="214" width="360.6" height="20"/>
<text class="vx-mono" x="598.6" y="229">78.4%</text>
<text class="vx-text" x="20" y="261">8  autotuned</text>
<rect class="vx-box" x="230" y="246" width="390.1" height="20"/>
<text class="vx-mono" x="628.1" y="261">84.8%</text>
<text class="vx-text" x="20" y="293">9  warp tile</text>
<rect class="vx-box" x="230" y="278" width="431.0" height="20"/>
<text class="vx-mono" x="669.0" y="293">93.7%</text>
<line class="vx-line" x1="690" y1="50" x2="690" y2="306" stroke-dasharray="4 4"/>
<text class="vx-text-muted" x="690" y="324" text-anchor="end">cuBLAS = 100%</text>
</svg>
<figcaption>Figure 1. The ladder's shape, drawn from Boehm's summary table. Coalescing is the largest single ratio (6.4 times), but the two highlighted rungs, the thread tiles, add the most throughput: from 2,980 to 15,972 GFLOP/s.</figcaption>
</figure>

Rungs 10 to 13 (double buffering, tensor cores, Hopper's asynchronous copies, epilogue fusion) go past where Boehm's table stops. This chapter describes what each changes and gives no throughput for them.

## Rung 1: one thread per output

The simplest GPU version of `multiply` runs the two outer loops in parallel. Thread `(row, column)` runs the whole `k` loop by itself, reading `a[row, k]` and `b[k, column]` from global memory at every step, and writes one element of `c`.

Count its loads. Each of the 4,096 threads reads 2 values per step for 64 steps: 128 loads per output, **524,288 loads** in all, 2 MiB, to deliver 32 KiB of distinct data. Every input value is fetched from global memory 64 times, once for each output that uses it. At Boehm's size the same arithmetic gives 548 GB of traffic if no cache helped, against a minimum of 268 MB for reading the inputs and writing the result once.[^boehm]

Caches catch part of this reuse by themselves. The rungs that follow stop relying on them and put each value where it will be reused. Rung 1 reached 309 GFLOP/s.[^boehm] Boehm's thread numbering put `row` on the lanes of a warp, the mapping [G4](g4-memory-performance.md#which-index-runs-across-the-warp) counts at 32 sectors for every load of `a`.

## Rung 2: coalescing

Rung 2 is [G4](g4-memory-performance.md#which-index-runs-across-the-warp)'s result applied: put `column` on the lanes, so the 32 lanes of a warp read one `a[row, k]` (a broadcast, 1 sector) and 32 neighbouring values of `b` (4 sectors), and write 32 neighbouring values of `c`. The arithmetic and the load count are unchanged; only the number of sectors per request falls. Boehm measured 1,987 GFLOP/s, and his profiler showed global-memory throughput rising from 15 GB/s to 110 GB/s.[^boehm] The loads are now cheap. There are still 524,288 of them.

## Rung 3: the block tile

The 64 outputs in one row of `c` all read the same row of `a`, and the 64 outputs in one column all read the same column of `b`. A **block tile** makes that sharing explicit. The threads of one block together own a `BM` × `BN` rectangle of `c`. They walk along `k` in slices of width `BK`: at each step they copy a `BM` × `BK` slice of `a` and a `BK` × `BN` slice of `b` from global memory into shared memory, wait at a barrier, compute every output's contribution for those `BK` values of `k` from the shared copies, and wait at a second barrier before the next copy overwrites the slices.

<figure class="vx-figure">
<svg viewBox="0 0 760 400" role="img" aria-label="One block tile of c, 32 by 32 outputs, is computed from a 32-row strip of a and a 32-column strip of b. The strips are consumed 8 columns of a and 8 rows of b at a time, each pair copied into shared memory before the block uses it; the slices move along k in 8 steps.">
<text class="vx-text" x="340" y="22" text-anchor="middle">b (k &#215; column)</text>
<text class="vx-text" x="140" y="202" text-anchor="middle">a (row &#215; k)</text>
<text class="vx-text" x="340" y="390" text-anchor="middle">c: block tile 32 &#215; 32</text>
<rect class="vx-box" x="60" y="210" width="160" height="160"/>
<rect class="vx-box" x="260" y="30" width="160" height="160"/>
<rect class="vx-box" x="260" y="210" width="160" height="160"/>
<rect class="vx-box-strong" x="60" y="210" width="160" height="80"/>
<rect class="vx-box-strong" x="340" y="30" width="80" height="160"/>
<rect class="vx-box-strong" x="340" y="210" width="80" height="80"/>
<g class="vx-seq" style="--vx-i: 0; --vx-n: 8">
<rect class="vx-box-accent" x="60" y="210" width="20" height="80"/>
<rect class="vx-box-accent" x="340" y="30" width="80" height="20"/>
</g>
<g class="vx-seq" style="--vx-i: 1; --vx-n: 8">
<rect class="vx-box-accent" x="80" y="210" width="20" height="80"/>
<rect class="vx-box-accent" x="340" y="50" width="80" height="20"/>
</g>
<g class="vx-seq" style="--vx-i: 2; --vx-n: 8">
<rect class="vx-box-accent" x="100" y="210" width="20" height="80"/>
<rect class="vx-box-accent" x="340" y="70" width="80" height="20"/>
</g>
<g class="vx-seq" style="--vx-i: 3; --vx-n: 8">
<rect class="vx-box-accent" x="120" y="210" width="20" height="80"/>
<rect class="vx-box-accent" x="340" y="90" width="80" height="20"/>
</g>
<g class="vx-seq" style="--vx-i: 4; --vx-n: 8">
<rect class="vx-box-accent" x="140" y="210" width="20" height="80"/>
<rect class="vx-box-accent" x="340" y="110" width="80" height="20"/>
</g>
<g class="vx-seq" style="--vx-i: 5; --vx-n: 8">
<rect class="vx-box-accent" x="160" y="210" width="20" height="80"/>
<rect class="vx-box-accent" x="340" y="130" width="80" height="20"/>
</g>
<g class="vx-seq" style="--vx-i: 6; --vx-n: 8">
<rect class="vx-box-accent" x="180" y="210" width="20" height="80"/>
<rect class="vx-box-accent" x="340" y="150" width="80" height="20"/>
</g>
<g class="vx-seq" style="--vx-i: 7; --vx-n: 8">
<rect class="vx-box-accent" x="200" y="210" width="20" height="80"/>
<rect class="vx-box-accent" x="340" y="170" width="80" height="20"/>
</g>
<text class="vx-text-accent" x="480" y="330">8 steps along k, one slice pair per step</text>
<rect class="vx-box" x="480" y="120" width="260" height="120" rx="4"/>
<text class="vx-text" x="492" y="144">shared memory, per step</text>
<text class="vx-mono" x="492" y="170">a slice  32 &#215; 8</text>
<text class="vx-mono" x="492" y="192">b slice   8 &#215; 32</text>
<text class="vx-text-muted" x="492" y="220">512 loads, 2,048 bytes</text>
<text class="vx-text-muted" x="480" y="280">each step: load, barrier, 8,192</text>
<text class="vx-text-muted" x="480" y="298">multiply-adds, barrier</text>
</svg>
<figcaption>Figure 2. One block of rung 3 computing a 32 &#215; 32 tile of <code>c</code> with <code>BK</code> = 8. It reads only the strips of <code>a</code> and <code>b</code> its outputs need, one slice pair per step (the bright slices, animated), and keeps each output's running sum in a register across the 8 steps.</figcaption>
</figure>

Count the loads for `BM` = `BN` = 32 and `BK` = 8. There are 4 blocks and 8 steps, and each step loads 32 × 8 + 8 × 32 = 512 values: 4 × 8 × 512 = **16,384 loads from global memory**, 32 times fewer than rung 1. In general a block tile loads `M·N·K·(1/BN + 1/BM)` values: each value of `a` is read once for every block tile in its row of blocks, `N / BN` times instead of `N` times. [G3](g3-memory-hierarchy.md) made the same count for 16 × 16 blocks and got 32,768.

The reads have not disappeared. Each output still reads 2 values per step of `k`, now from shared memory: 524,288 **shared-memory loads**, the same count rung 1 made from global memory. Rung 3 moved the traffic one level closer to the arithmetic.

Boehm's third kernel, with 32 × 32 tiles and `BK` = 32, used 8 KiB of shared memory per block and reached 2,980 GFLOP/s.[^boehm] That is only about 50 percent faster than rung 2, and he gives a reason: rung 2 already had good L1 cache hit rates, so part of the reuse was already being found by the cache.[^boehm]

??? check "With the same 64 × 64 × 64 product, how many loads from global memory does a 64 × 64 block tile need, and how many does a block tile of 16 rows by 32 columns need?"

    The formula `M·N·K·(1/BN + 1/BM)` with `M = N = K = 64` gives 262,144 × (1/64 + 1/64) = 8,192 for 64 × 64 (one block, which reads each input exactly once) and 262,144 × (1/32 + 1/16) = 24,576 for 16 × 32: the short side sets how often `b` is reread. The 64 × 64 tile halves the traffic of the 32 × 32 one again; what stops it growing without limit is shared-memory capacity and the number of blocks left to keep every core busy.

## Rungs 4 and 5: the thread tile

After rung 3 the bottleneck is shared memory itself. In Boehm's third kernel, every multiply-add in the inner loop needed two shared-memory loads, and the profiler showed warps stalled waiting for the queue that shared-memory instructions go through.[^boehm] A load from shared memory is much cheaper than one from global memory, but it is still an instruction, and there were two of them per useful one.

The fix repeats the block tile's trick one level down. Give each thread a small **thread tile** of `TM` × `TN` outputs, whose running sums, the **accumulators**, live in its registers. At each step of `k` the thread loads `TM` values of `a` and `TN` values of `b` from shared memory into registers and multiplies every `a` value by every `b` value, `TM · TN` multiply-adds. Multiplying every element of a column by every element of a row is an **outer product**; rung 3's threads computed a dot product instead, one pair of values at a time. Rung 4 grows the tile in one direction only (`TN` = 1); rung 5 grows it in both.

<figure class="vx-figure">
<svg viewBox="0 0 760 300" role="img" aria-label="Left: a thread with one output reads one value of a and one of b per step of k and does one multiply-add. Right: a thread with a 4 by 4 tile reads a column of 4 values of a and a row of 4 values of b per step and updates all 16 accumulators, one per pair.">
<text class="vx-text" x="20" y="24">one step of k, one thread</text>
<text class="vx-text-muted" x="120" y="56" text-anchor="middle">1 &#215; 1 tile (rung 3)</text>
<rect class="vx-box-accent" x="60" y="130" width="28" height="28"/>
<rect class="vx-box-accent" x="110" y="80" width="28" height="28"/>
<rect class="vx-box-strong" x="110" y="130" width="28" height="28"/>
<text class="vx-mono" x="74" y="178" text-anchor="middle">a</text>
<text class="vx-mono" x="160" y="99">b</text>
<text class="vx-text" x="40" y="230">2 loads</text>
<text class="vx-text" x="40" y="252">1 multiply-add</text>
<text class="vx-text-muted" x="500" y="56" text-anchor="middle">4 &#215; 4 tile (rung 5)</text>
<rect class="vx-box-accent" x="390" y="120" width="28" height="28"/>
<rect class="vx-box-accent" x="440" y="70" width="28" height="28"/>
<rect class="vx-box-strong" x="440" y="120" width="28" height="28"/>
<rect class="vx-box-strong" x="474" y="120" width="28" height="28"/>
<rect class="vx-box-strong" x="508" y="120" width="28" height="28"/>
<rect class="vx-box-strong" x="542" y="120" width="28" height="28"/>
<rect class="vx-box-accent" x="390" y="154" width="28" height="28"/>
<rect class="vx-box-accent" x="474" y="70" width="28" height="28"/>
<rect class="vx-box-strong" x="440" y="154" width="28" height="28"/>
<rect class="vx-box-strong" x="474" y="154" width="28" height="28"/>
<rect class="vx-box-strong" x="508" y="154" width="28" height="28"/>
<rect class="vx-box-strong" x="542" y="154" width="28" height="28"/>
<rect class="vx-box-accent" x="390" y="188" width="28" height="28"/>
<rect class="vx-box-accent" x="508" y="70" width="28" height="28"/>
<rect class="vx-box-strong" x="440" y="188" width="28" height="28"/>
<rect class="vx-box-strong" x="474" y="188" width="28" height="28"/>
<rect class="vx-box-strong" x="508" y="188" width="28" height="28"/>
<rect class="vx-box-strong" x="542" y="188" width="28" height="28"/>
<rect class="vx-box-accent" x="390" y="222" width="28" height="28"/>
<rect class="vx-box-accent" x="542" y="70" width="28" height="28"/>
<rect class="vx-box-strong" x="440" y="222" width="28" height="28"/>
<rect class="vx-box-strong" x="474" y="222" width="28" height="28"/>
<rect class="vx-box-strong" x="508" y="222" width="28" height="28"/>
<rect class="vx-box-strong" x="542" y="222" width="28" height="28"/>
<text class="vx-mono" x="404" y="272" text-anchor="middle">a</text>
<text class="vx-mono" x="582" y="89">b</text>
<text class="vx-text" x="600" y="170">8 loads</text>
<text class="vx-text" x="600" y="192">16 multiply-adds</text>
<text class="vx-text-muted" x="600" y="220">each a value meets</text>
<text class="vx-text-muted" x="600" y="238">every b value once</text>
<line class="vx-line" x1="300" y1="50" x2="300" y2="280"/>
</svg>
<figcaption>Figure 3. One step of <code>k</code> for one thread. With a 1 &#215; 1 tile, each multiply-add needs its own two loads. With a 4 &#215; 4 tile, 8 loads from shared memory feed 16 multiply-adds, because each loaded value is used once for every output in its row or column of the tile.</figcaption>
</figure>

Count again, keeping the 32 × 32 × 8 block tile and adding a 4 × 4 thread tile. The block now needs (32 / 4) × (32 / 4) = 64 threads, 2 warps. Each of the 256 threads in the whole product loads 8 values per step for 64 steps: **131,072 shared-memory loads**, a quarter of rung 3's, with the global-memory count unchanged at 16,384. The next example runs all three schedules on the CPU, counts every load by the memory it comes from, and compares the results bit for bit.

--8<-- "includes/examples/gpu/g10-matmul-ladder/load_count.cpp.md"

Boehm's fourth kernel, 8 outputs per thread in a column, reached 8,475 GFLOP/s; his fifth, an 8 × 8 thread tile, reached 15,972.[^boehm] For the fifth he counted K/64 global-memory loads and K/4 shared-memory loads per output, which is what the formulas above give for his tile sizes.[^boehm]

The price is registers. An 8 × 8 tile holds 64 accumulators per thread, plus the 16 values loaded at each step. CUTLASS's documentation says the accumulators of such kernels typically take at least half a thread's register budget, so these kernels run with lower occupancy than most, and rely on other ways to hide latency.[^cutlass] That is the trade [G5](g5-occupancy.md) weighs: fewer warps, each with more independent multiply-adds in flight.

## One formula at every level

Both tiles trade the same thing: more values held in a faster memory, in exchange for reading each value from the slower memory fewer times. **Arithmetic intensity**, the FLOPs a computation performs per byte it moves, states the trade as one number.[^p13] For an `m` × `n` tile, one step of `k` reads `m` values of `a` and `n` of `b`, `4(m + n)` bytes of `f32`, and performs `2mn` FLOPs, one multiply and one add per output, counted separately because [decision 56](../decisions/numbers.md#d56) keeps them separate. For a square `t` × `t` tile:

$$\text{intensity}(t) = \frac{2t^2}{4 \cdot 2t} = \frac{t}{4} \ \text{FLOPs per byte}$$

The formula does not say which two memories the tile sits between. A 32 × 32 block tile reads from global memory at 8 FLOPs per byte, and a 4 × 4 thread tile reads from shared memory at 1 FLOP per byte. Check both against the load counts: 524,288 FLOPs over 16,384 loads of 4 bytes is 8, and over 131,072 loads is 1. The roofline model compares that number with the machine's ratio of peak arithmetic to peak bandwidth ([G1](g1-throughput-machines.md#the-same-ceiling-the-roofline-bound-already-described)), and widening a tile moves a kernel's point to the right, toward the compute-bound side, without changing what it computes.[^p13]

--8<-- "includes/examples/gpu/g10-matmul-ladder/tile_intensity.cpp.md"

A square tile beats a thin one: 8 × 4 reaches 1.33 and 8 × 1 only 0.44. Intensity is `mn / (2(m + n))`, and for a fixed number of outputs `mn` it is largest when `m = n`. Boehm makes the same argument for why his fifth kernel computes a square of outputs per thread rather than a column.[^boehm] CUTLASS organizes its GEMM as exactly this hierarchy of tiles, one per level of memory and of parallelism: a thread block tile loaded from global memory, a warp tile read from shared memory, and a thread tile in registers.[^cutlass]

??? check "A thread computes an 8 × 8 tile inside a 64 × 64 block tile, on the 64 × 64 × 64 product. How many shared-memory loads does the whole product make, and what is the thread tile's intensity?"

    There are 4,096 / 64 = 64 threads, each loading 8 + 8 = 16 values per step for 64 steps: 64 × 16 × 64 = 65,536 loads, half of the 4 × 4 tile's 131,072. In general the count is `M·N·K·(1/TM + 1/TN)`, and the block tile does not enter it. The intensity is 8 / 4 = 2 FLOPs per byte. The number of multiply-adds is still 262,144; only who reads what, and from where, changed.

## Tiling is a schedule change, not new arithmetic

Block and thread tiles are loop transformations, and nothing about them is specific to GPUs ([P7](../optimize/p7-loop-transformations.md) and [P8](../optimize/p8-cache-blocking.md) apply the same idea to CPU caches, and [P12](../optimize/p12-fast-gemm.md) builds the CPU version of this ladder). A compiler whose IR has a matrix-multiply operation can apply them as rewrites of that operation. MLIR's transform dialect has one, `transform.structured.tile_using_for`, which takes a matched operation and tile sizes and produces a loop nest around a smaller copy of the operation.[^mlir-transform] Applying it twice nests the thread tile inside the block tile, because the second tiling matches the smaller operation the first one left behind.

--8<-- "includes/examples/gpu/g10-matmul-ladder/matmul_tile.mlir.md"

Read the output from the outside in. The first three loops step over `M`, `N` and `K` by 16, 16 and 8: the block tile and its walk along `k`. The inner three step over what is left by 4, 4 and 1: the thread tile. The innermost operation is a 4 × 1 by 1 × 4 matmul, one step of Figure 3's outer product.

What the tiling does not decide is where each loop's iterations run. These are sequential `scf.for` loops. The same dialect has `tile_using_forall`, which produces parallel loops that can carry a mapping, and operations that map such loops to GPU blocks and threads (`transform.gpu.map_forall_to_blocks` and `transform.gpu.map_nested_forall_to_threads`); copying the slices into shared memory is yet another step.[^mlir-transform] [M9](../mlir/m9-transform-dialect.md) and [M10](../mlir/m10-mlir-for-gpus.md) take the path from here to a GPU kernel.

## Rungs 6 and 7: wider loads, and the bank conflicts tiling creates

Once a thread reads several neighbouring values, a 16-byte vector load can bring four `f32` values in one instruction instead of four, provided the address is 16-byte aligned; [G4](g4-memory-performance.md#wider-loads-and-who-may-issue-them) covers the rule and who may issue such loads. Boehm's sixth kernel does two things. It stores the `a` slice transposed in shared memory, so that a thread's `TM` values of `a` are neighbours and can be read with one vector load, which he reports gained about 3 percent. And it vectorizes the global-memory loads with `float4`, reaching 18,237 GFLOP/s.[^boehm]

The thread tile reads each shared slice many times, through the access patterns [G4](g4-memory-performance.md#shared-memory-and-its-banks) showed can make lanes of one warp collide on a bank. Rung 7 removes those conflicts. Boehm wrote two kernels for it, his seventh and eighth: they eliminated the conflicts but were slower overall, so his summary table skips them, and he lists conflict-free layouts as unfinished work.[^boehm] A rung whose idea is right can still lose to its own costs, which is why the ladder is measured, not assumed.

G4 also showed why padding, the fix it used for the transpose, fights with rung 6: a row length that is odd in words avoids conflicts, and a row length that keeps every row 16-byte aligned is a multiple of 4 words, and no number is both. The way out is a **swizzled layout**, which keeps rows unpadded and permutes 16-byte pieces within each row from one row to the next. NVIDIA's tensor copies can write shared memory this way, and the Programming Guide's transpose example uses a 128-byte swizzle mode to remove an eight-way conflict.[^pg-swizzle]

## Rung 8: autotuning

By rung 7 the kernel has five free parameters, `BM`, `BN`, `BK`, `TM` and `TN`, and they trade shared-memory use, register use, the number of blocks and intensity against each other in ways that depend on the chip. **Autotuning** runs every candidate that fits and keeps the fastest. Boehm's ninth kernel does this and reached 19,721 GFLOP/s.[^boehm] His note on portability is the lesson: the configuration best on his A6000, run on an A100, was 6 percent slower than the A100's own best, which used `BM` = `BN` = 64, `BK` = 16 and `TM` = `TN` = 4.[^boehm]

[P15](../optimize/p15-choosing-parameters.md) covers the choice between a model and a search. A model can reject what cannot fit, such as a tile whose slices exceed the shared-memory limit from [G3](g3-memory-hierarchy.md#a-blocks-shared-scratchpad). Choosing among what fits is the "benchmarking or auto-tuning" the [philosophy page](../philosophy.md#performance-philosophy) allows, on the condition that it "must not change the observable meaning of a program". Every candidate here passes that test, because every tile size keeps each output's additions in the same order.

## Rung 9: the warp tile

A thread tile gives each thread its own outputs, but the program still treats the block as one flat set of threads. A **warp tile** adds the level in between: each warp of the block claims a `WM` × `WN` rectangle of the block's outputs, and its 32 lanes divide that rectangle among themselves, as rungs 4 and 5 divided the block's rectangle among threads.

Boehm gives three reasons the warp is worth naming. Warps are what the core's schedulers issue, four per core on his A6000; bank conflicts happen only between lanes of the same warp; and tighter thread tiles give more locality in the register cache of recent GPUs.[^boehm] CUTLASS asks for the same thing: a large warp tile for reuse inside the warp, and shared-memory reads without bank conflicts.[^cutlass] Boehm's tenth kernel adds the warp tile and, after tuning, reached 21,779 GFLOP/s, 93.7 percent of cuBLAS.[^boehm]

<figure class="vx-figure">
<svg viewBox="0 0 760 360" role="img" aria-label="Left: a block tile of 64 by 64 outputs divided into a 2 by 2 grid of warp tiles of 32 by 32. Right: warp 0's tile enlarged, divided into 4 rows and 8 columns of lane tiles, each 8 outputs tall and 4 wide, 32 lanes in all.">
<text class="vx-text" x="20" y="24">block tile 64 &#215; 64: 4 warps</text>
<rect class="vx-box-accent" x="40" y="60" width="120" height="120"/>
<text class="vx-text" x="100" y="116" text-anchor="middle">warp 0</text>
<text class="vx-text-muted" x="100" y="136" text-anchor="middle">32 &#215; 32</text>
<rect class="vx-box" x="160" y="60" width="120" height="120"/>
<text class="vx-text" x="220" y="116" text-anchor="middle">warp 1</text>
<text class="vx-text-muted" x="220" y="136" text-anchor="middle">32 &#215; 32</text>
<rect class="vx-box" x="40" y="180" width="120" height="120"/>
<text class="vx-text" x="100" y="236" text-anchor="middle">warp 2</text>
<text class="vx-text-muted" x="100" y="256" text-anchor="middle">32 &#215; 32</text>
<rect class="vx-box" x="160" y="180" width="120" height="120"/>
<text class="vx-text" x="220" y="236" text-anchor="middle">warp 3</text>
<text class="vx-text-muted" x="220" y="256" text-anchor="middle">32 &#215; 32</text>
<line class="vx-line" x1="160" y1="60" x2="400" y2="60" stroke-dasharray="4 4"/>
<line class="vx-line" x1="160" y1="180" x2="400" y2="300" stroke-dasharray="4 4"/>
<text class="vx-text" x="400" y="24">warp 0: 32 lanes, each 8 &#215; 4</text>
<rect class="vx-box-strong" x="400" y="60" width="36" height="56"/>
<text class="vx-mono" x="418" y="94" text-anchor="middle">0</text>
<rect class="vx-box-strong" x="440" y="60" width="36" height="56"/>
<text class="vx-mono" x="458" y="94" text-anchor="middle">1</text>
<rect class="vx-box-strong" x="480" y="60" width="36" height="56"/>
<text class="vx-mono" x="498" y="94" text-anchor="middle">2</text>
<rect class="vx-box-strong" x="520" y="60" width="36" height="56"/>
<text class="vx-mono" x="538" y="94" text-anchor="middle">3</text>
<rect class="vx-box-strong" x="560" y="60" width="36" height="56"/>
<text class="vx-mono" x="578" y="94" text-anchor="middle">4</text>
<rect class="vx-box-strong" x="600" y="60" width="36" height="56"/>
<text class="vx-mono" x="618" y="94" text-anchor="middle">5</text>
<rect class="vx-box-strong" x="640" y="60" width="36" height="56"/>
<text class="vx-mono" x="658" y="94" text-anchor="middle">6</text>
<rect class="vx-box-strong" x="680" y="60" width="36" height="56"/>
<text class="vx-mono" x="698" y="94" text-anchor="middle">7</text>
<rect class="vx-box-strong" x="400" y="120" width="36" height="56"/>
<text class="vx-mono" x="418" y="154" text-anchor="middle">8</text>
<rect class="vx-box-strong" x="440" y="120" width="36" height="56"/>
<text class="vx-mono" x="458" y="154" text-anchor="middle">9</text>
<rect class="vx-box-strong" x="480" y="120" width="36" height="56"/>
<text class="vx-mono" x="498" y="154" text-anchor="middle">10</text>
<rect class="vx-box-strong" x="520" y="120" width="36" height="56"/>
<text class="vx-mono" x="538" y="154" text-anchor="middle">11</text>
<rect class="vx-box-strong" x="560" y="120" width="36" height="56"/>
<text class="vx-mono" x="578" y="154" text-anchor="middle">12</text>
<rect class="vx-box-strong" x="600" y="120" width="36" height="56"/>
<text class="vx-mono" x="618" y="154" text-anchor="middle">13</text>
<rect class="vx-box-strong" x="640" y="120" width="36" height="56"/>
<text class="vx-mono" x="658" y="154" text-anchor="middle">14</text>
<rect class="vx-box-strong" x="680" y="120" width="36" height="56"/>
<text class="vx-mono" x="698" y="154" text-anchor="middle">15</text>
<rect class="vx-box-strong" x="400" y="180" width="36" height="56"/>
<text class="vx-mono" x="418" y="214" text-anchor="middle">16</text>
<rect class="vx-box-strong" x="440" y="180" width="36" height="56"/>
<text class="vx-mono" x="458" y="214" text-anchor="middle">17</text>
<rect class="vx-box-strong" x="480" y="180" width="36" height="56"/>
<text class="vx-mono" x="498" y="214" text-anchor="middle">18</text>
<rect class="vx-box-strong" x="520" y="180" width="36" height="56"/>
<text class="vx-mono" x="538" y="214" text-anchor="middle">19</text>
<rect class="vx-box-strong" x="560" y="180" width="36" height="56"/>
<text class="vx-mono" x="578" y="214" text-anchor="middle">20</text>
<rect class="vx-box-strong" x="600" y="180" width="36" height="56"/>
<text class="vx-mono" x="618" y="214" text-anchor="middle">21</text>
<rect class="vx-box-strong" x="640" y="180" width="36" height="56"/>
<text class="vx-mono" x="658" y="214" text-anchor="middle">22</text>
<rect class="vx-box-strong" x="680" y="180" width="36" height="56"/>
<text class="vx-mono" x="698" y="214" text-anchor="middle">23</text>
<rect class="vx-box-strong" x="400" y="240" width="36" height="56"/>
<text class="vx-mono" x="418" y="274" text-anchor="middle">24</text>
<rect class="vx-box-strong" x="440" y="240" width="36" height="56"/>
<text class="vx-mono" x="458" y="274" text-anchor="middle">25</text>
<rect class="vx-box-strong" x="480" y="240" width="36" height="56"/>
<text class="vx-mono" x="498" y="274" text-anchor="middle">26</text>
<rect class="vx-box-strong" x="520" y="240" width="36" height="56"/>
<text class="vx-mono" x="538" y="274" text-anchor="middle">27</text>
<rect class="vx-box-strong" x="560" y="240" width="36" height="56"/>
<text class="vx-mono" x="578" y="274" text-anchor="middle">28</text>
<rect class="vx-box-strong" x="600" y="240" width="36" height="56"/>
<text class="vx-mono" x="618" y="274" text-anchor="middle">29</text>
<rect class="vx-box-strong" x="640" y="240" width="36" height="56"/>
<text class="vx-mono" x="658" y="274" text-anchor="middle">30</text>
<rect class="vx-box-strong" x="680" y="240" width="36" height="56"/>
<text class="vx-mono" x="698" y="274" text-anchor="middle">31</text>
<text class="vx-text-muted" x="20" y="330">block tile: global memory to shared memory. warp tile: shared memory to the warp&#8217;s registers.</text>
<text class="vx-text-muted" x="20" y="350">lane tile: the registers of one thread, updated by outer products.</text>
</svg>
<figcaption>Figure 4. Three levels of tiling, each a rectangle divided among the level below it: a 64 &#215; 64 block tile divided into four 32 &#215; 32 warp tiles, and one warp tile divided among its 32 lanes as 4 rows and 8 columns of 8 &#215; 4 lane tiles. <code>warp_partition.cpp</code> checks this arrangement.</figcaption>
</figure>

A warp tile changes which thread owns which output, and a wrong split is a correctness bug, not a slow kernel. If two lanes owned the same output, two threads would write the same element of `c`; if a gap were left, some element would never be written. The last C++ example walks the nested index arithmetic of Figure 4's split and records the owner of every output.

--8<-- "includes/examples/gpu/g10-matmul-ladder/warp_partition.cpp.md"

??? check "A warp tile of 32 × 16 is split into 8 × 4 lane tiles. What does `warp_partition.cpp` report, and why is that a problem even though no output is owned twice?"

    32 × 16 holds (32 / 8) × (16 / 4) = 16 lane tiles, so the warp has 16 thread tiles, not 32. With `warp_n` changed to 16, the program stops with the message that warp 0 has 16 thread tiles. Half of the warp's lanes would have no outputs: they issue every instruction with the others and do nothing, the idle-lane waste [G2](g2-simt.md) described for a block that is not a multiple of 32 threads. It is safe, but it wastes half the warp.

## Splitting k: the tiling that changes the answer

Every tile so far splits `M` and `N` among threads, and walks `K` in order. A block tile does cut `K` into slices of `BK`, but one thread carries each accumulator across all the slices, so each output still adds its 64 products in increasing `k`. That is why Figure 2's register keeps its value from step to step.

A small product with a long `K` leaves too few output tiles to occupy a large GPU. The remedy is to split `K` itself. CUTLASS's **split-K** gives different blocks different ranges of `k` for the same output tile and adds their partial results in a second kernel; **sliced-K** does the same among the warps of one block.[^cutlass] Boehm found cuBLAS doing this at size 256: a matmul kernel followed by a reduction kernel.[^boehm]

Splitting `k` changes the answer. With two halves, an output becomes (first 32 products summed) + (last 32 products summed): the same terms, grouped differently, and in floating point a different grouping can round differently. This is the parallel sum of [G6](g6-synchronization.md) in another form. Under [decision 56](../decisions/numbers.md#d56) a Vortex compiler may not split `k` unless the programmer opts in to reassociation, which v0.1 does not offer. The ladder's free rungs are the ones that tile `M` and `N`.

## Beyond rung 9

**Double buffering** (rung 10) allocates two copies of each shared slice, so that the block loads the next slices while it computes on the current ones. CUTLASS double-buffers at two levels, the shared-memory tiles and the register fragments each warp reads from them.[^cutlass] On NVIDIA GPUs of compute capability 8.0 and later, the asynchronous copy (LDGSTS) moves data from global to shared memory without passing through registers, which is what makes the overlap cheap to issue.[^pg-async] The arithmetic is untouched: this is still a schedule change.

**Tensor cores** (rung 11) are different in kind. A tensor core instruction computes a small matrix product for a whole warp: CUDA's warp matrix functions compute `D = A*B + C` on tiles such as 16 × 16 × 16, on mixed-precision data, for compute capability 7.0 and later.[^pg-wmma][^tc-blog] The original Volta tensor cores took 16-bit inputs and accumulated in 32 bits.[^tc-blog] For `f32` data, the inputs must first be converted to TF32, a format with the range of `f32` and at least 10 bits of precision instead of 23.[^pg-wmma]

Apple's Metal Shading Language has SIMD-group matrix types, including `simdgroup_float8x8`, and a `simdgroup_multiply_accumulate` defined as `d = a * b + c`; the specification now points readers to its newer tensor operations instead.[^msl] Narrowing the inputs changes the answer. So does handing each dot product's additions to one instruction: the definitions above give the result as `A*B + C` and do not promise the program's order of additions.

**Hopper's asynchrony** (rung 12) goes further. The Hopper tuning guide describes the Tensor Memory Accelerator, a copy engine that moves whole tiles between global and shared memory so that one thread can start a large transfer while the block continues.[^hopper] CUTLASS's Hopper kernels split a block into a producer warp group that loads tiles with it and consumer warp groups that run the tensor-core math.[^cutlass]

**Epilogue fusion** (rung 13) applies elementwise work, such as scaling or clamping, to the finished tile before it is stored; CUTLASS's epilogue also reshuffles the tile through shared memory so the stores coalesce.[^cutlass] Applied after the sums are complete, as separate rounded operations, it keeps the bits; [G12](g12-flashattention.md) is about fusion, and [G11](g11-matrix-units.md) about tensor cores and what opting in would mean.

One more knob belongs to the schedule: the order in which blocks are launched over the output. CUTLASS maps consecutive blocks to nearby tiles so that they share data in the last-level cache.[^cutlass] Boehm tried such a remapping and saw no gain, with his L2 hit rate already about 80 percent.[^boehm] It is another negative result worth keeping.

## What the ladder never changes

For fixed inputs, does rung 9 print the same bits as rung 1? [G4](g4-memory-performance.md#what-does-not-change-the-bits) answered for coalescing and staging: new thread mappings, copies through shared memory, padding and vector loads move values without touching an operation. Thread and warp tiles extend the answer. Rung 5's outer product updates 16 accumulators per step, but each accumulator, the one that becomes one element of `c`, still starts at `0.0` and adds its products in increasing `k`. Warp tiling only decides which lane owns which accumulator, and autotuning only picks among such schedules. `load_count.cpp` confirms it for rungs 1, 3 and 5: the bits are equal.

That guarantee holds for the source's operations, and the compilers underneath a GPU kernel do not start from it. nvcc fused Boehm's multiply and add into one `fma.rn.f32`.[^boehm] Apple's Metal compiler defaults to its `fast` math mode, which allows reassociation and contraction across statements, and even its `safe` mode allows contraction within a statement; turning contraction off takes `-ffp-contract=off` or a pragma.[^msl-math] A Vortex GPU back end must set these explicitly, as [decision 56](../decisions/numbers.md#d56) already requires for a C compiler. When we compiled `load_count.cpp` with `-ffp-contract=fast` instead (Apple clang 21, Apple M4 Pro, 2026-09-24), both of its comparisons printed `no`.

Keeping multiplies and adds separate has a price. Boehm's estimate of the best possible runtime counts each fused multiply-add as two FLOPs against the A6000's advertised 30 TFLOP/s, and expects a matmul made of fused instructions to come close to that figure.[^boehm] If a separate multiply and add each issue no faster than one fused instruction, a kernel that may not fuse issues twice the arithmetic instructions for the same work, and its compute ceiling is at most half that peak. Whether Vortex should offer fusion as an opt-in, and how a program would ask for it, is a language question for [P11](../optimize/p11-floating-point.md) and [G11](g11-matrix-units.md).

Every rung also needs, silently, that no store to `c` changes `a` or `b` while other blocks still read them. A C compiler given three pointers must assume they might overlap. A Vortex compiler knows they cannot: [decision 25](../decisions/references.md#d25) forbids the variable lent as `&mut c` from appearing in any other argument of the same call, so the check is made once, at the call, before any tiling decision.

??? check "Rung 1 and rung 9 run on the same inputs, with no tensor cores and no split-K. Should their outputs match bit for bit, and which compiler defaults could break that?"

    Yes, if the back end keeps the source's operations: every rung from 1 to 10 changes only which thread holds a value and when it is read, and each output's products are still rounded, then added in increasing `k` from `0.0`. Contraction breaks it: if the compiler fuses the multiply and the add in one rung's loop and not in another's, or in different places, the roundings differ. Metal's default fast math mode would also allow reassociation.

## Measuring it

No GPU timings are claimed here. To reproduce the ladder's shape on your own machine:

1. Write rungs 1 to 6 by hand, in Metal Shading Language on an Apple GPU or in CUDA on a rented NVIDIA GPU, all computing the same `N` × `N` product, with contraction and fast math turned off.
2. Check every rung against the CPU reference bit for bit before timing it. Compare a vendor library, such as Metal Performance Shaders or cuBLAS, with a tolerance instead: unless its documentation promises your order of operations, it may use another.
3. Time many runs and report the median and its spread, following [P1](../optimize/p1-measure-first.md). Use an `N` large enough that the inputs do not fit in the last-level cache.
4. Compute GFLOP/s as 2N³ divided by the median time, and the fraction of the library's rate.
5. For each rung, read the one counter its fact predicts should improve ([G14](g14-measuring-gpu-code.md) names the tools): sectors per request for rung 2, global-memory bytes for rung 3, shared-memory instructions per FLOP for rungs 4 and 5, bank conflicts for rung 7.

| Rung | Tile sizes | Median time | GFLOP/s | % of library | Bits equal rung 1 | Counter |
| --- | --- | --- | --- | --- | --- | --- |
| 1 naive | | | | | | |
| 2 coalesced | | | | | | |
| 3 block tile | | | | | | |
| 4 1-D thread tile | | | | | | |
| 5 2-D thread tile | | | | | | |
| 6 vector loads | | | | | | |

Record the machine, the operating system or driver version, the compiler and its flags, and the date with the table.

## For Vortex

!!! vortex "Exercise"

    **Build** a tile planner for matrix-product loop nests in your compiler: given a nest with fixed shapes, a target description and a choice of block, warp and thread tile sizes, it reports what the choice costs and whether it is allowed, without generating any GPU code.

    1. Extend the target description from [G4](g4-memory-performance.md#for-vortex)'s exercise with a shared-memory limit per block, a maximum number of threads per block and a warp width, filled from a source you can cite or a measurement you record, and marked unknown otherwise.
    2. For a block tile (`BM`, `BN`, `BK`) and a thread tile (`TM`, `TN`), report: the loads from global memory and from shared memory for the whole nest, the intensity of the block tile and of the thread tile, the shared-memory bytes of one pair of slices, the threads per block, and a lower bound on registers per thread (accumulators plus the values loaded per step).
    3. Reject, with a one-line reason, a choice whose tile sizes do not divide the shapes, whose slices exceed the shared-memory limit, or whose threads per block exceed the maximum. Report, without rejecting, a thread count that leaves idle lanes in its last warp.
    4. Given a warp tile (`WM`, `WN`) as well, check that the warp tiles partition the block tile and the thread tiles partition each warp tile into exactly one warp's worth of lanes.
    5. For every accepted choice, print a remark in the form the [sixth principle](../philosophy.md#6-explain-performance-decisions) asks for.

    **Not yet:** generating GPU code for any rung ([M12](../mlir/m12-vortex-gpu-path.md) weighs the paths); choosing tile sizes automatically ([P15](../optimize/p15-choosing-parameters.md)); double buffering, tensor cores or fusion; any split of the `k` loop; occupancy limits from registers ([G5](g5-occupancy.md)).

    **Proof that it works:**

    - Loads: for the 64 × 64 × 64 `multiply` with block tile 32 × 32 × 8, the planner reports 16,384 global-memory loads, and 131,072 shared-memory loads with thread tile 4 × 4, matching `load_count.cpp`. With a 1 × 1 thread tile it reports 524,288 shared-memory loads.
    - Intensity: for square tiles of edge 1 to 128, and for 8 × 1, 8 × 4 and 128 × 64, it matches `tile_intensity.cpp` to two decimals.
    - Limits: on a 256 × 256 × 256 product with a 32,768-byte shared-memory limit (the Apple figure from [G3](g3-memory-hierarchy.md#a-blocks-shared-scratchpad)), block tile 128 × 128 × 32 is accepted at exactly 32,768 bytes, 128 × 128 × 64 is rejected at 65,536 bytes, and a block tile of 48 × 48 is rejected on the 64 × 64 × 64 product because 48 does not divide 64.
    - Lanes: block tile 40 × 32 with thread tile 4 × 4 on an 80 × 64 × 64 product is accepted, with a warning: its 80 threads fill 3 warps and leave 16 lanes idle.
    - Partition: block 64 × 64, warp 32 × 32, thread 8 × 4 passes with 4 warps and 4,096 outputs owned once, as `warp_partition.cpp` reports; warp 32 × 16 with thread 8 × 4 is rejected because a warp gets 16 thread tiles, not 32.
    - Remark: for block tile 32 × 32 × 8 and thread tile 4 × 4, a line such as "block tile 32 x 32 x 8, thread tile 4 x 4: 2,048 bytes of shared memory, 64 threads, 8.00 FLOPs/byte from global memory, 1.00 from shared memory".

## Key ideas

!!! recap "Questions you can now answer"

    - **What does the naive kernel waste?** Reuse: each of the 64 uses of an input value fetches it from global memory again, 524,288 loads for 8,192 distinct values.
    - **What does a block tile change, and what does it leave?** It cuts global-memory loads to `M·N·K·(1/BN + 1/BM)`, but each output still makes 2 shared-memory loads per multiply-add.
    - **Why does a thread tile compute an outer product?** Each of its `TM + TN` loaded values is used once for every output in its row or column, so `TM · TN` multiply-adds cost `TM + TN` loads.
    - **Why does one formula, `t / 4` FLOPs per byte, fit block and thread tiles?** Both are the same reuse at different memory levels: between global and shared memory, and between shared memory and registers.
    - **Where does a warp tile sit, and what must a compiler check about it?** Between the block tile and the thread tile, matching the unit the hardware issues; the split must give every output exactly one owner and every lane a tile.
    - **Which rungs keep every bit, and which do not?** Rungs 1 to 10 and autotuning keep each output's operations and order. Split-K, tensor cores, and compiler defaults that contract or reassociate change the answer and need the programmer's permission.

## Where this comes back

!!! next "You will use this again in"

    - [G11. Matrix units](g11-matrix-units.md): *tensor cores*, *reduced-precision inputs*, *warp-level matrix instruction*
    - [G12. Fusion case study: FlashAttention](g12-flashattention.md): *epilogue*, *tile*, *shared-memory staging*
    - [G13. Tile languages](g13-tile-languages.md): *block tile*, *thread mapping*, *autotuning*
    - [G14. Measuring GPU code](g14-measuring-gpu-code.md): *one counter per rung*, *reproducing a published ladder*
    - [M9. Schedules as IR: the transform dialect](../mlir/m9-transform-dialect.md): *tiling as a rewrite*, *nested tiling*
    - [M10. MLIR for GPUs](../mlir/m10-mlir-for-gpus.md): *mapping tiled loops to blocks and threads*
    - [M12. Designing Vortex's GPU path](../mlir/m12-vortex-gpu-path.md): *tile planner*, *contraction off in the back end*
    - [P15. Choosing parameters: models or search](../optimize/p15-choosing-parameters.md): *autotuning*, *tile-size search*
    - [P16. Capstone: the ladder, measured](../optimize/p16-capstone.md): *arithmetic intensity*, *a ladder measured rung by rung*

## Sources and further reading

Read Boehm's worklog kernel by kernel with this chapter's load counts beside it, then CUTLASS's efficient-GEMM page for the whole hierarchy in one place.

[^boehm]: Simon Boehm, "How to Optimize a CUDA Matmul Kernel for cuBLAS-like Performance: a Worklog", December 2022: the summary table; kernels 1 to 6, 9 and 10; the notes on kernels 7 and 8, on cuBLAS's split-K kernels and on thread swizzling. Code: <https://github.com/siboehm/SGEMM_CUDA>. <https://siboehm.com/articles/22/CUDA-MMM>
[^cutlass]: NVIDIA, "CUTLASS: Efficient GEMM in CUDA": the sections on the threadblock, warp and thread-level GEMM, the epilogue, pipelining, threadblock rasterization, parallelized reductions (split-K and sliced-K) and Hopper warp specialization. <https://docs.nvidia.com/cutlass/latest/media/docs/cpp/efficient_gemm.html>
[^p13]: Samuel Williams, Andrew Waterman and David Patterson, "Roofline: An Insightful Visual Performance Model for Multicore Architectures", Communications of the ACM 52(4):65-76, 2009. <https://people.eecs.berkeley.edu/~kubitron/cs252/handouts/papers/RooflineVyNoYellow.pdf>
[^mlir-transform]: MLIR Project, "Transform Dialect": `transform.structured.tile_using_for`, `transform.structured.tile_using_forall`, `transform.gpu.map_forall_to_blocks` and `transform.gpu.map_nested_forall_to_threads`. <https://mlir.llvm.org/docs/Dialects/Transform/>
[^pg-swizzle]: NVIDIA, "CUDA Programming Guide", v13.4.2, section 4.12.2.2.5, "Shared-Memory Bank Swizzling". <https://docs.nvidia.com/cuda/cuda-programming-guide/04-special-topics/async-copies.html>
[^pg-async]: NVIDIA, "CUDA Programming Guide", v13.4.2, section 4.12.1, "Using LDGSTS", and section 4.12.1.2, "Prefetching Data". <https://docs.nvidia.com/cuda/cuda-programming-guide/04-special-topics/async-copies.html>
[^pg-wmma]: NVIDIA, "CUDA Programming Guide", v13.4.2, section 5.4.11, "Warp Matrix Functions". <https://docs.nvidia.com/cuda/cuda-programming-guide/05-appendices/cpp-language-extensions.html#warp-matrix-functions>
[^tc-blog]: Jeremy Appleyard and Scott Yokim, "Programming Tensor Cores in CUDA 9", NVIDIA Technical Blog, 17 October 2017. <https://developer.nvidia.com/blog/programming-tensor-cores-cuda-9/>
[^msl]: Apple, "Metal Shading Language Specification", version 4.1, section 2.4, "SIMD-group Matrix Data Types", and section 6.8, "SIMD-Group Matrix Functions". <https://developer.apple.com/metal/Metal-Shading-Language-Specification.pdf>
[^msl-math]: Apple, "Metal Shading Language Specification", version 4.1, section 1.6.3, "Math Intrinsics Compiler Options": `-fmetal-math-mode`, its `fast` default, and `-ffp-contract=off`. <https://developer.apple.com/metal/Metal-Shading-Language-Specification.pdf>
[^hopper]: NVIDIA, "Hopper Tuning Guide", section 1.4.1.2, "Tensor Memory Accelerator". <https://docs.nvidia.com/cuda/hopper-tuning-guide/index.html>

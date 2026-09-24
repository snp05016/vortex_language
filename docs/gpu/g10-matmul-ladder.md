# G10. The GPU matmul ladder

<p class="page-intro">From a naive kernel to a warp-tiled one, each rung explained by one hardware fact.</p>

<p class="vx-meta" markdown="1">Level: Advanced · Reading time: about 50 minutes · Builds on: [G4. Memory performance: coalescing and bank conflicts](g4-memory-performance.md), [G5. Occupancy and latency hiding](g5-occupancy.md)</p>

???+ remember "Before you start, remember"

    ??? question "What does one warp-wide load cost in global memory, and which loop index should a Vortex compiler put on the lanes?"

        The number of distinct aligned 32-byte sectors its 32 addresses touch. The lanes should vary the loop variable that is the last subscript of the accesses that run most often, so that neighbouring lanes touch neighbouring addresses.

        Introduced in [G4. Memory performance: coalescing and bank conflicts](g4-memory-performance.md).

    ??? question "What is shared memory, and what must two accesses do before one thread may read a value another thread wrote there?"

        A small, fixed-capacity on-chip scratchpad that every thread of one block can read and write. A thread may read another thread's value only after both have passed a barrier that orders the store before the load.

        Introduced in [G3. The GPU memory hierarchy](g3-memory-hierarchy.md).

    ??? question "What is a warp, and how many threads does one NVIDIA warp or Apple SIMD-group hold?"

        A fixed-size group of threads that a GPU issues one instruction to at a time: 32 on NVIDIA hardware and on Apple GPUs.

        Introduced in [G2. The SIMT execution model](g2-simt.md).

    ??? question "What does the roofline bound say a kernel's attainable rate is capped by?"

        The smaller of two ceilings: the machine's peak compute rate, and its peak memory bandwidth times the kernel's arithmetic intensity, operations performed per byte moved.

        Introduced in [G1. Throughput machines](g1-throughput-machines.md), building on [P3. The roofline model](../optimize/p3-roofline.md).

    ??? question "May a Vortex compiler change the order in which `sum += a[row, k] * b[k, column]` adds its terms?"

        No. Every floating-point operation is one IEEE 754 operation, rounded once, in the order the program writes it; a compiler may not fuse, reassociate or reorder them.

        Introduced in [Numbers, literals and casts, decision 56](../decisions/numbers.md#d56).

!!! goals "In this chapter"

    - Explain why the naive, one-thread-per-output kernel leaves almost all of a GPU's arithmetic throughput unused.
    - Follow the ladder one rung at a time, from shared-memory tiling through register tiling to warp tiling, and name the one hardware fact each rung answers.
    - Compute the arithmetic intensity a tile buys at the block level and at the thread level, and show why the same formula recurs at both.
    - Recognize which rungs are schedule changes that a compiler may apply on its own, and where the ladder crosses into changes that alter a program's numbers.
    - State what a Vortex compiler would need to choose and check a rung's tile sizes, without generating any GPU code.

## The kernel this chapter tunes

Every rung of this chapter computes the same thing: the matrix product from [stage 10](../compiler/guide/stage-10-matrix-multiplication.md), shown in [G4](g4-memory-performance.md#which-index-runs-across-the-warp) at 64 × 64:

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

A **ladder**, in this chapter's sense, is a sequence of GPU implementations of the same function, each one rung above the last, where every rung keeps the arithmetic and changes only how it is scheduled: which thread does which work, and through which memory it passes on the way. Simon Boehm built and measured such a ladder for single-precision matrix multiplication, going from a first working kernel to one within a few percent of NVIDIA's own cuBLAS library, and published every kernel and every number.[^boehm] The table below is this chapter's own account of that ladder's shape, naming the one hardware fact each rung answers and how the idea is spelled in CUDA and in Apple's Metal Shading Language (MSL). The GFLOP/s column is Boehm's, measured on one RTX A6000 multiplying two 4092 × 4092 `f32` matrices without tensor cores, in December 2022.[^boehm] It is one number from one run on one GPU: read it for the shape, the way each rung roughly doubles or triples throughput, and reproduce that shape on your own hardware rather than expecting these exact figures.

| Rung | One hardware fact | GFLOP/s (% of cuBLAS) |
|---|---|---|
| 0 | A CPU reference and a tolerance check exist before any GPU code does | - |
| 1 | Naive: one thread per output, everything read from DRAM | 309 (1.3%) |
| 2 | Coalescing: neighbouring lanes read neighbouring addresses | 1,987 (8.5%) |
| 3 | Shared-memory tiling: a block reuses a tile it keeps on chip | 2,980 (12.8%) |
| 4 | 1D register tiling: one thread produces several outputs | 8,475 (36.5%) |
| 5 | 2D register tiling: an outer product per thread, not a dot product | 15,972 (68.7%) |
| 6 | Vector loads move 16 bytes per instruction instead of 4 | 18,237 (78.4%) |
| 7 | Bank-conflict avoidance in the now heavily reused shared tile | - |
| 8 | Autotuning searches the tile-size choices rungs 3 to 7 opened up | 19,721 (84.8%) |
| 9 | Warp tiling: an explicit tile between the block tile and the thread tile | 21,779 (93.7%) |

Rungs 10 to 13 (double buffering, tensor cores, Hopper's asynchrony, epilogue fusion) continue past where Boehm's own summary table stops; this chapter describes what each changes and gives no throughput number for them, because none was verified for this book. [G11](g11-matrix-units.md) and [G12](g12-flashattention.md) take up rungs 11 to 13 in full.

## Rung 1: naive, one thread per output

The simplest GPU version of `multiply` runs the two outer loops in parallel: thread `(row, column)` runs the whole `k` loop by itself and writes one element of `c`. Every input it reads comes straight from global memory, the CUDA Programming Guide's baseline case for a kernel with no staging at all.[^pg-kernels] Nothing is wrong with its arithmetic; [G4](g4-memory-performance.md#thirty-two-addresses-one-instruction) already worked out what its memory traffic costs, once the thread mapping is chosen so that neighbouring lanes share `row` and vary `column`.

What this rung leaves on the table is reuse. Element `a[row, k]` is read once by the thread that owns `(row, column)` and never again, even though 64 other threads, one per column, need that exact same value for their own `k` step. Boehm's own first kernel, with an even less favourable mapping than G4's, reached 309 GFLOP/s against cuBLAS's several tens of thousands: 1.3 percent.[^boehm] Rungs 2 through 9 do not change what gets computed. They change who holds a value once it has been read, so that reading it once can serve more than one output.

## Rung 2: coalescing

[G4](g4-memory-performance.md#which-index-runs-across-the-warp) already derives this rung in full: choosing which of `row` and `column` runs across a warp's lanes decides whether `a`, `b` and `c` are read and written in aligned, four-sector bursts or in thirty-two scattered ones. Boehm's measured jump from this change alone, 309 to 1,986.5 GFLOP/s, is the largest single step in his whole ladder.[^boehm] It costs nothing extra: the same arithmetic, addressed by lanes in a different order.

## Rung 3: shared-memory tiling, the block tile

Coalescing fixes how one warp's request reaches memory. It does not stop 64 different warps, one per row of the 64 × 64 output, from each rereading the same row of `b`. A **block tile** fixes that: a whole thread block, rather than one warp, cooperatively loads a `BM` × `BK` slice of `a` and a `BK` × `BN` slice of `b` into shared memory once, then every thread in the block computes its share of the `BM` × `BN` output slice from that shared copy, for `BK` steps of `k`, before the block loads the next slice.[^pg-shared] [G3](g3-memory-hierarchy.md#a-blocks-shared-scratchpad) already named this memory and gave its capacity on two chips; [G4](g4-memory-performance.md#shared-memory-and-its-banks) already covers the barrier the load and the compute must cross and the bank conflicts a naive tile layout can create.

The new idea this rung adds is not the memory, which G3 and G4 already covered. It is **reuse**: each element loaded from global memory is read `BK` times from shared memory instead of once from DRAM. Boehm's third kernel, adding only this, reached 2,980 GFLOP/s.[^boehm] The next section makes that reuse numeric.

## Rungs 4 and 5: register tiling, the thread tile

A block tile shares its inputs among the whole block. Inside the block, though, the naive mapping still gives one output to one thread, so each thread reads its slice of the shared tile once per output. The next rung repeats the same trick one level down: instead of one output per thread, give each thread a small `TM` × `TN` tile of outputs, held in registers. At each step of `k`, the thread reads `TM` elements from the shared copy of `a` and `TN` from the shared copy of `b`, and updates `TM` × `TN` accumulators with them, an **outer product** rather than a single dot product. Rung 4 does this with `TN` fixed at 1 (one column, several rows); rung 5 does it in both directions. Boehm measured 8,475 GFLOP/s for the first and 15,972 for the second, the two largest steps in his ladder after coalescing.[^boehm]

Both the block tile and the thread tile trade the same currency: more bytes held on chip, in exchange for reading each byte from a slower place fewer times. `Intensity`, floating-point operations per byte moved, states that trade as one number. For a `t` × `t` tile whose `t` outputs share one element of each operand at every step of `k`, one step does `2 * t * t` operations (one multiply and one add, counted separately, because [decision 56](../decisions/numbers.md#d56) forbids fusing them into one rounding) and reads `4 * t` bytes of `f32` values, `t` from each operand. So:

$$\text{intensity}(t) = \frac{2 t^2}{4 \cdot 2t} = \frac{t}{4} \text{ FLOPs per byte}$$

Reading that formula as "FLOPs per byte" is exactly the quantity the roofline bound compares against a machine's peak bandwidth ([G1](g1-throughput-machines.md#the-same-ceiling-the-roofline-bound-already-described)), which is why widening a tile is a roofline move: it slides a kernel's point rightward, toward the compute-bound side, without changing what it computes.[^p13] The formula does not care which two memory levels the tile sits between. A `BM` × `BN` block tile sitting between global memory and shared memory, and a `TM` × `TN` thread tile sitting between shared memory and registers, are the same reuse trick applied at two different points in the memory hierarchy, and CUTLASS's own account of an efficient GEMM organizes exactly these two tiles, plus the warp tile of rung 9, into one hierarchy for this reason.[^cutlass]

--8<-- "includes/examples/gpu/g10-matmul-ladder/tile_intensity.cpp.md"

<figure class="vx-figure">
<svg viewBox="0 0 800 340" role="img" aria-label="Arithmetic intensity as a tile's edge doubles" aria-describedby="g10-f1-desc">
<title id="g10-f1-title">Arithmetic intensity as a tile's edge doubles</title>
<desc id="g10-f1-desc">A bar chart with eight bars, one per tile edge from 1 to 128, doubling each time. Bar height is the arithmetic intensity in FLOPs per byte, computed by the formula t over 4: 0.25, 0.5, 1, 2, 4, 8, 16 and 32. Each bar is exactly one step taller than the last, because doubling the edge doubles the intensity.</desc>
<text class="vx-text" x="20" y="24">FLOPs per byte moved, for a t &#215; t tile (block tile or thread tile, same formula)</text>
<rect class="vx-box-accent" x="80" y="280" width="60" height="20"/>
<text class="vx-mono" x="110" y="272" text-anchor="middle">0.25</text>
<text class="vx-text-muted" x="110" y="320" text-anchor="middle">1</text>
<rect class="vx-box-accent" x="170" y="260" width="60" height="40"/>
<text class="vx-mono" x="200" y="252" text-anchor="middle">0.5</text>
<text class="vx-text-muted" x="200" y="320" text-anchor="middle">2</text>
<rect class="vx-box-accent" x="260" y="240" width="60" height="60"/>
<text class="vx-mono" x="290" y="232" text-anchor="middle">1</text>
<text class="vx-text-muted" x="290" y="320" text-anchor="middle">4</text>
<rect class="vx-box-accent" x="350" y="220" width="60" height="80"/>
<text class="vx-mono" x="380" y="212" text-anchor="middle">2</text>
<text class="vx-text-muted" x="380" y="320" text-anchor="middle">8</text>
<rect class="vx-box-accent" x="440" y="200" width="60" height="100"/>
<text class="vx-mono" x="470" y="192" text-anchor="middle">4</text>
<text class="vx-text-muted" x="470" y="320" text-anchor="middle">16</text>
<rect class="vx-box-accent" x="530" y="180" width="60" height="120"/>
<text class="vx-mono" x="560" y="172" text-anchor="middle">8</text>
<text class="vx-text-muted" x="560" y="320" text-anchor="middle">32</text>
<rect class="vx-box-accent" x="620" y="160" width="60" height="140"/>
<text class="vx-mono" x="650" y="152" text-anchor="middle">16</text>
<text class="vx-text-muted" x="650" y="320" text-anchor="middle">64</text>
<rect class="vx-box-accent" x="710" y="140" width="60" height="160"/>
<text class="vx-mono" x="740" y="132" text-anchor="middle">32</text>
<text class="vx-text-muted" x="740" y="320" text-anchor="middle">128</text>
<line class="vx-line" x1="60" y1="300" x2="780" y2="300"/>
<text class="vx-text-muted" x="20" y="303">0</text>
<text class="vx-text-muted" x="400" y="335" text-anchor="middle">tile edge t</text>
</svg>
<figcaption>Figure 1. Arithmetic intensity of a t &#215; t tile, computed by <code>tile_intensity.cpp</code>. The bar for edge 1 is rung 1's naive kernel, one output per thread and no reuse: 0.25 FLOPs per byte. The same staircase describes a block tile widening from global memory, and a thread tile widening from shared memory; only which memory the bytes come from changes.</figcaption>
</figure>

??? check "A thread computes a 1 &#215; 1 tile (rung 1) instead of an 8 &#215; 8 one (rung 5). By what factor does widening to 8 &#215; 8 raise its arithmetic intensity, and does it change how many multiplications the kernel performs in total?"

    By a factor of 8: intensity is `t / 4`, so `8 / 4` divided by `1 / 4` is 8. The total work is unchanged. Rung 5 does the same 64 &#215; 64 &#215; 64 multiply-adds as rung 1; it only lets 64 of them, one per output in the tile, share each value read from shared memory, instead of each output re-reading it.

## Tiling is a schedule change, not new arithmetic

Nothing about block or register tiling depends on GPUs specifically; both are loop transformations, and a compiler with a `linalg.matmul`-shaped operation can apply them as rewrites on its intermediate representation rather than as source-level surgery. The transform dialect names such a rewrite a **tiling**, given as target sizes rather than hand-written loops, and applies it to a matched operation, producing the smaller operation nested inside the loops the tiling needs.[^mlir-transform] Applying it twice, once for the block tile and once for the thread tile, nests one tiling's result inside the other automatically, because the second tiling matches whatever operation the first one left behind.

--8<-- "includes/examples/gpu/g10-matmul-ladder/matmul_tile.mlir.md"

Six nested loops and a 4 &#215; 1-by-1 &#215; 4 matmul are what rungs 3 through 5 amount to, once written out in full: the outer three loops are the block tile stepping over `M`, `N` and `K` in strides of 16, 16 and 8; the inner three are the thread tile stepping over what the block tile left, in strides of 4, 4 and 1. Nothing here decides *where* a loop's iterations run, on a CPU core or a GPU thread; that decision is a separate step (`--gpu-map-parallel-loops` maps parallel loops to a grid of blocks and threads, once the loops are marked parallel). Tiling only decides *how much work* one step of an outer loop leaves for its inner loops, which is exactly the block-tile-then-thread-tile structure this chapter has been building by hand. [O8](../optimize/o8-loops.md) covers loop transformations that preserve a loop's meaning in general; this is one instance of that idea, chosen for the tile sizes a target's shared memory and register file allow.

## Rungs 6 and 7: wider loads, and the bank conflicts tiling creates

Two smaller rungs round out register tiling. [G4](g4-memory-performance.md#wider-loads-and-who-may-issue-them) already covers vector loads: once a thread reads several contiguous elements at once, a 16-byte `float4` load moves them in one instruction instead of four, provided the address is 16-byte aligned. Boehm's sixth kernel adds this, plus loading `a`'s tile pre-transposed so its own reads are contiguous too, reaching 18,237 GFLOP/s.[^boehm]

Reusing a shared tile this heavily also reuses it through the exact access pattern [G4](g4-memory-performance.md#shared-memory-and-its-banks) already showed can conflict: a thread's outer product reads a column of the `a` tile and a row of the `b` tile from shared memory on every step of `k`, and if the tile's row length shares a factor with the bank count, many threads collide on one bank. The fix is the same one G4 gives: pad the tile by an element so the conflicting stride becomes coprime with the bank count. Boehm's worklog places this fix in kernels 7 and 8 without reporting a GFLOP/s figure for it on its own, so this chapter reports none either; the effect is the same conflict count G4 already computes, applied to a tile now read many more times per load than rung 3's did.

## Rung 8: autotuning, a search over the ladder's own parameters

By rung 7 the kernel has several free parameters: the block tile's `BM`, `BN` and `BK`, and the thread tile's `TM` and `TN`. Different choices trade shared-memory usage, register usage and arithmetic intensity against each other in ways that depend on the target GPU's exact capacities, not on the algorithm. Boehm's eighth kernel sweeps these parameters and keeps the best combination measured on the target GPU, reaching 19,721 GFLOP/s, 84.8 percent of cuBLAS, from choices no earlier rung tried.[^boehm] [P15](../optimize/p15-choosing-parameters.md) covers this kind of search in general: a cost model can rule out combinations that will not fit (a tile whose shared-memory footprint exceeds the target's budget, from [G3](g3-memory-hierarchy.md#a-blocks-shared-scratchpad)), but choosing among the combinations that do fit is exactly the "benchmarking or auto-tuning" the [philosophy page](../philosophy.md#performance-philosophy) allows a Vortex compiler to use, so long as every candidate computes the same answer.

## Rung 9: warp tiling, a tile between the block and the thread

Register tiling gives each thread its own small tile, but threads still read the shared tile independently of one another. A **warp tile** adds one more level: instead of 32 lanes each reading their own fragments of the shared tile, the whole warp claims a contiguous `WM` × `WN` rectangle of the block's output, and its 32 lanes divide that rectangle among themselves the same way rungs 4 and 5 divided the block's rectangle among threads. CUTLASS's account of an efficient GEMM places the warp tile exactly here, between the block tile computed cooperatively by every thread of a block and the thread tile computed alone by one lane, and names the same benefit rungs 4 and 5 already showed: fewer, larger reads per unit of arithmetic.[^cutlass] Boehm's ninth kernel, adding this, reached 21,779 GFLOP/s, 93.7 percent of cuBLAS.[^boehm]

--8<-- "includes/examples/gpu/g10-matmul-ladder/warp_partition.cpp.md"

<figure class="vx-figure">
<svg viewBox="0 0 800 340" role="img" aria-label="A block tile divided into warp tiles, one warp tile divided into lanes" aria-describedby="g10-f2-desc">
<title id="g10-f2-title">A block tile divided into warp tiles, one warp tile divided into lanes</title>
<desc id="g10-f2-desc">Left, a square representing the block's 64 by 64 output tile, divided into a 2 by 2 grid of 4 equal squares, one per warp. Right, a zoomed 4 row by 8 column grid of 32 small cells, one per lane of the warp highlighted on the left: its 32 by 32 tile divided among the warp's 32 lanes, each lane an 8 by 4 tile of outputs. A dashed line connects the highlighted warp to the zoomed grid.</desc>
<text class="vx-text" x="20" y="24">block tile: 64 &#215; 64 outputs, 4 warps</text>
<rect class="vx-box" x="60" y="60" width="100" height="100"/>
<rect class="vx-box" x="160" y="60" width="100" height="100"/>
<rect class="vx-box" x="60" y="160" width="100" height="100"/>
<rect class="vx-box" x="160" y="160" width="100" height="100"/>
<rect class="vx-box-accent" x="60" y="60" width="100" height="100"/>
<text class="vx-text-muted" x="110" y="114" text-anchor="middle">warp 0</text>
<text class="vx-text-muted" x="110" y="132" text-anchor="middle">32 &#215; 32</text>
<text class="vx-text-muted" x="210" y="114" text-anchor="middle">warp 1</text>
<text class="vx-text-muted" x="110" y="214" text-anchor="middle">warp 2</text>
<text class="vx-text-muted" x="210" y="214" text-anchor="middle">warp 3</text>
<line class="vx-line" x1="160" y1="90" x2="420" y2="90" stroke-dasharray="4 4"/>
<line class="vx-line" x1="160" y1="130" x2="420" y2="210" stroke-dasharray="4 4"/>
<text class="vx-text" x="430" y="24">warp 0's tile: 32 lanes, each an 8 &#215; 4 tile</text>
<rect class="vx-box-accent" x="430" y="90" width="30" height="34"/>
<rect class="vx-box-accent" x="463" y="90" width="30" height="34"/>
<rect class="vx-box-accent" x="496" y="90" width="30" height="34"/>
<rect class="vx-box-accent" x="529" y="90" width="30" height="34"/>
<rect class="vx-box-accent" x="562" y="90" width="30" height="34"/>
<rect class="vx-box-accent" x="595" y="90" width="30" height="34"/>
<rect class="vx-box-accent" x="628" y="90" width="30" height="34"/>
<rect class="vx-box-accent" x="661" y="90" width="30" height="34"/>
<rect class="vx-box-accent" x="430" y="127" width="30" height="34"/>
<rect class="vx-box-accent" x="463" y="127" width="30" height="34"/>
<rect class="vx-box-accent" x="496" y="127" width="30" height="34"/>
<rect class="vx-box-accent" x="529" y="127" width="30" height="34"/>
<rect class="vx-box-accent" x="562" y="127" width="30" height="34"/>
<rect class="vx-box-accent" x="595" y="127" width="30" height="34"/>
<rect class="vx-box-accent" x="628" y="127" width="30" height="34"/>
<rect class="vx-box-accent" x="661" y="127" width="30" height="34"/>
<rect class="vx-box-accent" x="430" y="164" width="30" height="34"/>
<rect class="vx-box-accent" x="463" y="164" width="30" height="34"/>
<rect class="vx-box-accent" x="496" y="164" width="30" height="34"/>
<rect class="vx-box-accent" x="529" y="164" width="30" height="34"/>
<rect class="vx-box-accent" x="562" y="164" width="30" height="34"/>
<rect class="vx-box-accent" x="595" y="164" width="30" height="34"/>
<rect class="vx-box-accent" x="628" y="164" width="30" height="34"/>
<rect class="vx-box-accent" x="661" y="164" width="30" height="34"/>
<rect class="vx-box-accent" x="430" y="201" width="30" height="34"/>
<rect class="vx-box-accent" x="463" y="201" width="30" height="34"/>
<rect class="vx-box-accent" x="496" y="201" width="30" height="34"/>
<rect class="vx-box-accent" x="529" y="201" width="30" height="34"/>
<rect class="vx-box-accent" x="562" y="201" width="30" height="34"/>
<rect class="vx-box-accent" x="595" y="201" width="30" height="34"/>
<rect class="vx-box-accent" x="628" y="201" width="30" height="34"/>
<rect class="vx-box-accent" x="661" y="201" width="30" height="34"/>
<text class="vx-text-muted" x="20" y="290">warp_partition.cpp checks this covers all 4,096 outputs of the block tile once each</text>
</svg>
<figcaption>Figure 2. Three levels of tiling, each a rectangle divided among the level below it: a block tile of 64 &#215; 64 outputs divided into 4 warp tiles of 32 &#215; 32, and one warp tile divided into 32 lane (thread) tiles of 8 &#215; 4, the arrangement <code>warp_partition.cpp</code> checks.</figcaption>
</figure>

??? check "warp_partition.cpp checks that its 4 &#215; 8 arrangement of thread tiles covers a warp's 32 &#215; 32 outputs exactly once. Why does that check matter for correctness, not only for speed?"

    A tiling only changes *which* thread computes *which* output. If two thread tiles overlapped, two threads would write the same element of `c`, a race the language gives no meaning to; if the tiles left a gap, some element of `c` would never be written at all. Confirming a partition, one owner per output, is what makes the split safe to apply without changing the program's result.

## Beyond rung 9: where the free lunch ends

Three further changes are worth naming even without a verified number for any of them.

**Double buffering** (rung 10) keeps two shared-memory tiles instead of one, so that a block loads the *next* tile from global memory while its threads still compute on the *current* one, overlapping the load's latency with arithmetic instead of paying for them one after another. CUDA's asynchronous copy instructions, available from compute capability 8.0, move data from global to shared memory without passing through a register, which is what makes the overlap cheap to issue.[^n4] Like every rung before it, this changes only when work happens, not what it computes.

**Tensor cores** (rung 11) are different in kind. A tensor core computes a small fixed-size matrix product, such as CUDA 9's 16 × 16 × 16 warp matrix multiply-accumulate (WMMA), as one cooperative instruction across a warp rather than as a sequence of scalar multiply-adds.[^n14][^n6] Apple's SIMD-group matrix functions do the analogous thing at 8 × 8 on its own GPUs, from the Apple7 family onward.[^ap1] These instructions often accept or prefer reduced-precision inputs, and the accumulation order inside one instruction is fixed by the hardware, not by the program text; using one is no longer only a scheduling choice; it is a change to the numbers a kernel produces. That is exactly the case the [philosophy page](../philosophy.md#3-do-not-surprise-the-programmer) has in mind when it says an optimization that could change a floating-point result needs the programmer's explicit permission, permission v0.1 does not yet grant. [G11](g11-matrix-units.md) takes up tensor cores and what "opt-in" would need to mean for them.

**Hopper's warpgroup MMA and tensor memory accelerator** (rung 12) push both ideas further, an asynchronous bulk copy engine and a warp-group-wide matrix instruction that overlap even more aggressively, and need compute capability 9.0 or later.[^n10] Renting time on such a GPU, not owning one, is how this book's research reached that fact; [G14](g14-measuring-gpu-code.md) says more about working this way. **Epilogue fusion** (rung 13), folding a bias add or an activation function into the same kernel that produces the matrix product, is a schedule change again, not a numeric one, and it is the seam this ladder hands off to [G12](g12-flashattention.md), where fusing several operations into one kernel is the entire subject.

??? check "Why does using a tensor core need the programmer's permission in a way that shared-memory tiling does not?"

    Shared-memory, register and warp tiling change which thread holds a value and when, never what arithmetic runs on it: [decision 56](../decisions/numbers.md#d56)'s order is preserved throughout. A tensor core instruction performs its internal multiply-adds in an order and often a precision the instruction fixes, not the program text, so switching to one can change the result's bits. The [philosophy page](../philosophy.md#3-do-not-surprise-the-programmer) allows that only when the programmer has asked for it.

## What the ladder never changes

Read the whole ladder again with one question in mind: for a fixed set of inputs, does rung 9's kernel print the same bits as rung 1's? [G4](g4-memory-performance.md#what-does-not-change-the-bits) answers this for coalescing and shared-memory staging: a new thread mapping, a tile copied through shared memory, a padded row nothing reads, a vector load reading the same bytes as four scalar ones, all move values without touching an arithmetic operation. Register tiling and warp tiling extend the same answer. Rung 5's outer product updates `TM * TN` accumulators at each step of `k`, but each individual accumulator, the one that becomes one element of `c`, still adds its `k` products in increasing order from `0.0`, exactly as [decision 56](../decisions/numbers.md#d56) requires; the outer product changes which other accumulators are updated alongside it in the same instruction, not the sequence feeding any one of them. Warp tiling only decides which lane owns which accumulator. Autotuning picks among tile sizes, and every size it can pick preserves the same order, which is what lets the [philosophy page](../philosophy.md#performance-philosophy) allow autotuning at all, on the condition that it "must not change the observable meaning of a program."

The one thing every rung up to and including 10 also needs, silently, is that no store through `c` can be read back through `a` or `b`: two block tiles' worth of writes to `c` race with each other's reads of `a` and `b` only if the arrays might overlap. [Decision 25](../decisions/references.md#d25) rules that out for `multiply`'s signature: the array lent as `&mut c` cannot also appear as `a` or `b` in the same call. A language that let two parameters alias would need this ladder to check for it on every rung; Vortex's `&mut` rule proves it once, at the function's boundary, before any tiling decision is made.

That is also where the free ride stops. Tensor cores fix their own multiply-add order and often narrow the inputs' precision, which is a change to the answer, not to the schedule, and the [philosophy page](../philosophy.md#3-do-not-surprise-the-programmer) treats it accordingly: available, but only opt-in.

??? check "Rung 1 (naive) and rung 9 (warp-tiled) are run on the same inputs, with strict IEEE 754 arithmetic and no tensor cores. Should their outputs match bit for bit?"

    Yes. Every rung from 1 through 10 only changes which thread holds a value and when it is read; none reorders, fuses or narrows an arithmetic operation. Since each output's `k`-loop still adds its terms in the same order in every rung, IEEE 754 guarantees the same rounding at every step, so the final bits match. Tensor cores (rung 11 onward) are the first rung where that guarantee no longer holds.

## For Vortex

!!! vortex "Exercise"

    **Build** a tile planner for the `multiply` kernel: given a target description and a choice of block, thread and warp tile sizes, it reports the arithmetic intensity, the memory used and whether the choice fits, without generating any GPU code.

    1. Extend the target description [G4](g4-memory-performance.md#for-vortex)'s exercise asked for with a shared-memory (or threadgroup-memory) budget per block and a register budget per thread, filled from a chip you can cite or measure and marked unknown otherwise.
    2. Given a block tile (`BM`, `BN`, `BK`) and a thread tile (`TM`, `TN`), report: the arithmetic intensity of the block tile and of the thread tile, using this chapter's formula; the shared-memory bytes the block tile needs for both operands; and the number of threads per block the thread tile implies (`BM / TM` times `BN / TN`).
    3. A checker that rejects a choice whose shared-memory footprint exceeds the target's budget, or whose thread count is not a multiple of the target's warp size, with a one-line reason for the rejection.
    4. Extend the checker to a warp tile (`WM`, `WN`): given it alongside the block and thread tiles, confirm, the way `warp_partition.cpp` does, that the warp tiles partition the block tile and the thread tiles partition each warp tile, with no output owned twice or not at all.
    5. For every choice your planner accepts, a one-line remark in the form the [sixth principle](../philosophy.md#6-explain-performance-decisions) asks for, such as "block tile 64 x 64 x 8, thread tile 8 x 4: 8,192 bytes of shared memory, 16.0 FLOPs/byte at the thread level."

    **Not yet:** generating any GPU code for any rung ([M12](../mlir/m12-vortex-gpu-path.md) weighs the paths); choosing tile sizes automatically ([P15](../optimize/p15-choosing-parameters.md)); double buffering, tensor cores or fusion (this chapter's [beyond rung 9](#beyond-rung-9-where-the-free-lunch-ends)); occupancy accounting that trades registers against resident warps ([G5](g5-occupancy.md)).

    **Proof that it works:**

    - For the `t` &#215; `t` tiles this chapter tabulated, the planner's intensity matches `tile_intensity.cpp`'s output exactly, for every `t` in {1, 2, 4, 8, 16, 32, 64, 128}.
    - For the block, warp and thread tiles `warp_partition.cpp` uses (64x64, 32x32, 8x4), the planner's partition check passes and reports 4 warps and 4,096 outputs covered.
    - A shared-memory budget of 32,768 bytes (the Apple4-onward figure from [G3](g3-memory-hierarchy.md)) rejects a 128 &#215; 128 &#215; 8 block tile of `f32` (needing 4 &#215; 8 &#215; (128 + 128) = 8,192... recompute for your own chosen `BK` and confirm by hand which block tiles the budget admits) and accepts a 64 &#215; 64 &#215; 8 one.
    - A thread count check rejects a thread tile that would need 96 threads per block on a target whose warp size is 32 (96 is a multiple of 32, so pick a genuinely bad case, such as one needing 100), and accepts one needing 128.

## Key ideas

!!! recap "Questions you can now answer"

    - **What does the naive kernel leave unused?** Reuse. Every input it reads is read once and thrown away, even though many other threads need the same value.
    - **What is a tile, in the sense this chapter uses it?** A rectangle of outputs, held in a faster memory than the one its inputs started in, so its inputs are read once from the slower memory and reused many times from the faster one.
    - **Why does the same formula, `t / 4` FLOPs per byte, describe both block tiling and register tiling?** Both are the identical reuse trick, a `t` &#215; `t` tile sharing `t` values per step of `k`, applied at two different points in the memory hierarchy: block tiling between global and shared memory, register tiling between shared memory and registers.
    - **Where does a warp tile sit, and what does it add over register tiling alone?** Between the block tile and the thread tile: a warp claims a rectangle of the block's outputs and its 32 lanes divide it, the same partitioning register tiling does one level up, giving the warp's reads of shared memory the same reuse benefit register tiling gives one thread's reads.
    - **Which rungs of the ladder may a Vortex compiler apply without the programmer's permission, and which may it not?** Rungs 1 through 10: they only move values and reassign work, never reorder or fuse an arithmetic operation. Tensor cores and other reduced-precision, fixed-order instructions change the result's bits and need the programmer's explicit permission.
    - **Why does `warp_partition.cpp` check for a partition rather than just running the kernel?** A tiling is safe only if every output is owned by exactly one thread; an overlap is a data race and a gap is a missing result, and both are cheaper to rule out by checking the index arithmetic than by running the kernel and hoping.

## Where this comes back

!!! next "You will use this again in"

    - [G5. Occupancy and latency hiding](g5-occupancy.md): *register tile*, *thread count per block*, *registers per thread*
    - [G11. Matrix units](g11-matrix-units.md): *tensor cores*, *opt-in precision*, *warp-level instruction*
    - [G12. Fusion case study: FlashAttention](g12-flashattention.md): *epilogue fusion*, *tile*, *shared-memory staging*
    - [G13. Tile languages](g13-tile-languages.md): *block tile*, *thread mapping*, *tiling as a compiler transformation*
    - [G14. Measuring GPU code](g14-measuring-gpu-code.md): *per-rung counter*, *reproducing a measurement on different hardware*
    - [M9. Schedules as IR: the transform dialect](../mlir/m9-transform-dialect.md): `tile_using_for`, *nested tiling*
    - [M10. MLIR for GPUs](../mlir/m10-mlir-for-gpus.md): *mapping tiled loops to a grid of threads*
    - [P15. Choosing parameters: models or search](../optimize/p15-choosing-parameters.md): *autotuning*, *tile-size search*
    - [P16. Capstone: the ladder, measured](../optimize/p16-capstone.md): *arithmetic intensity*, *the roofline bound*

## Sources and further reading

Read Boehm's worklog first, kernel by kernel; then CUTLASS's efficient-GEMM page for the block/warp/thread hierarchy in one place.

[^boehm]: Simon Boehm, "How to Optimize a CUDA Matmul Kernel for cuBLAS-like Performance: a Worklog", December 2022: the results table and the sections for kernels 1, 2, 3, 4, 5, 6, 7, 8 and 9. <https://siboehm.com/articles/22/CUDA-MMM>
[^pg-kernels]: NVIDIA, "CUDA Programming Guide", v13.4.2, section 2.3.2, "Thread Hierarchy", and section 2.3.4.1, "Coalesced Global Memory Access". <https://docs.nvidia.com/cuda/cuda-programming-guide/02-basics/writing-cuda-kernels.html>
[^pg-shared]: NVIDIA, "CUDA Programming Guide", v13.4.2, section 2.3.4.2, "Shared Memory Access Patterns". <https://docs.nvidia.com/cuda/cuda-programming-guide/02-basics/writing-cuda-kernels.html#shared-memory-access-patterns>
[^cutlass]: NVIDIA, "CUTLASS: Efficient GEMM in CUDA", the sections describing the thread block tile, warp tile and thread tile hierarchy, and double buffering. <https://docs.nvidia.com/cutlass/latest/media/docs/cpp/efficient_gemm.html>
[^p13]: Williams, Waterman, Patterson, "Roofline: An Insightful Visual Performance Model for Multicore Architectures", Communications of the ACM 52(4):65-76, 2009, the definition of operational (arithmetic) intensity. <https://people.eecs.berkeley.edu/~kubitron/cs252/handouts/papers/RooflineVyNoYellow.pdf>
[^mlir-transform]: MLIR Project, "Transform Dialect", the `transform.structured.tile_using_for` operation. <https://mlir.llvm.org/docs/Dialects/Transform/>
[^n4]: NVIDIA, "CUDA Programming Guide", v13.4.2, section 2.4, "Asynchronous Data Copies", the compute-capability-8.0 requirement and the global-to-shared copy path. <https://docs.nvidia.com/cuda/cuda-programming-guide/04-special-topics/async-copies.html>
[^n6]: NVIDIA, "CUDA Programming Guide", v13.4.2, section 2.5.6, "Warp Matrix Functions" (WMMA). <https://docs.nvidia.com/cuda/cuda-programming-guide/02-basics/writing-cuda-kernels.html#warp-matrix-functions>
[^n14]: Appleyard and Yokim, "Programming Tensor Cores in CUDA 9", NVIDIA Technical Blog, 17 October 2017: the 16 x 16 x 16 WMMA shape and its warp-cooperative execution. <https://developer.nvidia.com/blog/programming-tensor-cores-cuda-9/>
[^ap1]: Apple, "Metal Shading Language Specification", version 4.1, section 6.8, "SIMD-group Matrix Functions" (`simdgroup_float8x8` and related types, supported from the Apple7 GPU family). <https://developer.apple.com/metal/Metal-Shading-Language-Specification.pdf>
[^n10]: NVIDIA, "Hopper Tuning Guide": the compute-capability-9.0 requirement for the tensor memory accelerator and warpgroup MMA. <https://docs.nvidia.com/cuda/hopper-tuning-guide/index.html>

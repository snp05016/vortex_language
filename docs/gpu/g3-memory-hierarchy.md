# G3. The GPU memory hierarchy

<p class="page-intro">A value in a GPU kernel can live in a register that belongs to one thread, a scratchpad that belongs to one block, a cache that belongs to one core, or memory that belongs to the whole device, and each place differs in who can see the value, how much fits and what it costs to reach. This chapter names each level in CUDA, HIP, Metal and MLIR terms, counts what staging data in a block's scratchpad saves, and sets out the facts a Vortex compiler needs before it can place a single GPU value.</p>

<p class="vx-meta" markdown="1">Level: Foundations · Reading time: about 40 minutes · Builds on: [G2. The SIMT execution model](g2-simt.md), [P2. The memory hierarchy](../optimize/p2-memory-hierarchy.md)</p>

???+ remember "Before you start, remember"

    ??? question "Where do the threads of one block run, and which of them form a warp?"

        All threads of a block run on one streaming multiprocessor (SM), NVIDIA's name for a GPU core. Each run of 32 consecutive threads of the block is a warp, which executes one instruction for all its threads together.

        Introduced in [G2. The SIMT execution model](g2-simt.md).

    ??? question "How does a GPU keep its arithmetic units busy while a load waits hundreds of cycles for memory?"

        It keeps many warps resident and switches to one that is ready. The more work in flight, the more latency it can hide.

        Introduced in [G1. Throughput machines](g1-throughput-machines.md).

    ??? question "In what order are the elements of a `[f32; 64, 64]` array stored?"

        Row after row, the last index varying fastest: `m[i, j + 1]` sits next to `m[i, j]`, and `m[i + 1, j]` is 256 bytes away.

        Introduced in [Arrays and shapes, decision 43](../decisions/arrays.md#d43).

    ??? question "Why is a small, fast memory near the processor worth having if the data also lives in a large, slow one?"

        Programs reuse data. A value brought close once and read many times pays the slow trip once. The hierarchy works only as well as the program's reuse.

        Introduced in [P2. The memory hierarchy](../optimize/p2-memory-hierarchy.md).

!!! goals "In this chapter"

    - Name each level of a GPU's memory in CUDA, HIP and Metal terms, and say which thread, block, core or device owns it.
    - Explain why registers and shared memory are scarce, by dividing a core's fixed supply among the threads and blocks resident on it.
    - Count the device-memory reads that staging tiles in shared memory saves in a matrix product, and check a tile against a target's shared-memory limits.
    - Read the address space of a pointer in Metal Shading Language, MLIR's `gpu` dialect and LLVM's GPU back ends, and say which level it names.
    - Describe what unified memory changes on Apple silicon, and what a buffer's storage mode still decides.

## One product, four places to keep a value

Start with the kernel Vortex was built for, from [stage 10](../compiler/guide/stage-10-matrix-multiplication.md#the-program-the-milestone-asks-for):

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

The simplest GPU version gives each `(row, column)` pair its own thread, as in [G2](g2-simt.md): 4,096 threads, each running the whole `k` loop. The arithmetic is settled. What is not settled is where each value lives while the loop runs, and the four values in the loop body have four different sets of readers.

`sum` is read and written by one thread only. `a[row, k]` is read by all 64 threads that share `row`, and `b[k, column]` by all 64 that share `column`. `c[row, column]` is written once, by one thread, and read later by the program that launched the kernel. A GPU offers a place for each of these patterns, and a compiler that knows who reads a value can pick the cheapest place that every reader can reach.

This chapter names those places, from closest to farthest. A **register** is storage private to one thread. **Shared memory** is a scratchpad private to one block but visible to all its threads. **Caches** keep copies of recently used data without being asked. **Device memory** holds everything else, visible to every thread of the kernel and to the host program. Figure 1 shows where each one sits on the chip.

<figure class="vx-figure">
<svg viewBox="0 0 760 440" role="img" aria-label="Where each level of GPU memory sits on the chip, and who owns it" aria-describedby="g3-f1-desc">
<title id="g3-f1-title">Where each level of GPU memory sits on the chip, and who owns it</title>
<desc id="g3-f1-desc">Two streaming multiprocessors side by side, with more implied. Each holds a register file divided into one slice per resident thread, and one on-chip array whose storage is split between shared memory, divided into one region per resident block, and an L1 cache that the hardware manages. Below both SMs, a single L2 cache serves every SM. At the bottom, device memory in DRAM holds global data, constant data and each thread's local memory. A dashed arrow from a register file to device memory marks a spill into local memory.</desc>
<text class="vx-text" x="20" y="24">One GPU: on-chip storage per SM, then storage every SM shares</text>
<rect class="vx-box" x="20" y="40" width="330" height="200"/>
<text class="vx-text" x="34" y="62">SM 0 (Apple: GPU core; AMD: compute unit)</text>
<rect class="vx-box-strong" x="36" y="72" width="298" height="62"/>
<text class="vx-text-muted" x="46" y="90">register file: one slice per resident thread</text>
<rect class="vx-cell-on" x="46" y="100" width="30" height="24"/>
<rect class="vx-box" x="82" y="100" width="30" height="24"/>
<rect class="vx-box" x="118" y="100" width="30" height="24"/>
<rect class="vx-box" x="154" y="100" width="30" height="24"/>
<rect class="vx-box" x="190" y="100" width="30" height="24"/>
<rect class="vx-box" x="226" y="100" width="30" height="24"/>
<text class="vx-text-muted" x="266" y="117">...</text>
<rect class="vx-box-accent" x="36" y="146" width="186" height="80"/>
<text class="vx-text" x="46" y="166">shared memory</text>
<text class="vx-text-muted" x="46" y="184">one region per resident</text>
<text class="vx-text-muted" x="46" y="200">block; the program fills it</text>
<rect class="vx-box" x="222" y="146" width="112" height="80"/>
<text class="vx-text" x="232" y="166">L1 cache</text>
<text class="vx-text-muted" x="232" y="184">the hardware</text>
<text class="vx-text-muted" x="232" y="200">fills it</text>
<text class="vx-text-muted" x="46" y="237">one physical array, split per kernel</text>
<rect class="vx-box" x="410" y="40" width="330" height="200"/>
<text class="vx-text" x="424" y="62">SM 1</text>
<rect class="vx-box-strong" x="426" y="72" width="298" height="62"/>
<text class="vx-text-muted" x="436" y="90">register file</text>
<rect class="vx-box" x="436" y="100" width="30" height="24"/>
<rect class="vx-box" x="472" y="100" width="30" height="24"/>
<rect class="vx-box" x="508" y="100" width="30" height="24"/>
<rect class="vx-box" x="544" y="100" width="30" height="24"/>
<rect class="vx-box" x="580" y="100" width="30" height="24"/>
<rect class="vx-box" x="616" y="100" width="30" height="24"/>
<text class="vx-text-muted" x="656" y="117">...</text>
<rect class="vx-box-accent" x="426" y="146" width="186" height="80"/>
<text class="vx-text" x="436" y="166">shared memory</text>
<rect class="vx-box" x="612" y="146" width="112" height="80"/>
<text class="vx-text" x="622" y="166">L1 cache</text>
<text class="vx-text-muted" x="380" y="145" text-anchor="middle">...</text>
<line class="vx-line" x1="185" y1="240" x2="185" y2="270"/>
<line class="vx-line" x1="575" y1="240" x2="575" y2="270"/>
<rect class="vx-box" x="20" y="270" width="720" height="46"/>
<text class="vx-text" x="34" y="298">L2 cache: one per device, shared by every SM</text>
<line class="vx-line" x1="380" y1="316" x2="380" y2="340"/>
<rect class="vx-box-strong" x="20" y="340" width="720" height="60"/>
<text class="vx-text" x="34" y="364">device memory (DRAM, off the GPU chip)</text>
<text class="vx-text-muted" x="34" y="386">global data, constant data, and each thread's local memory</text>
<line class="vx-line" x1="700" y1="134" x2="700" y2="334" stroke-dasharray="5 4"/>
<polygon class="vx-arrowhead" points="700,340 695,328 705,328"/>
<text class="vx-text-muted" x="690" y="256" text-anchor="end">spill</text>
<text class="vx-text-muted" x="20" y="428">owners: a thread (registers, local memory) · a block (shared memory) · an SM (L1) · the device (L2, device memory)</text>
</svg>
<figcaption>Figure 1. Where each level of memory sits. Registers and shared memory are on the SM, divided among the threads and blocks resident there; on NVIDIA GPUs the same on-chip array holds shared memory and the L1 cache. The L2 cache and device memory are shared by the whole GPU. A thread's local memory, where spilled registers go, lives in device memory despite its name.</figcaption>
</figure>

## Registers: a thread's own values

A **register** holds one value, named directly by an instruction rather than reached through an address. NVIDIA's Programming Guide places registers on the SM, gives them thread scope and a kernel's lifetime, and leaves their use to the compiler.[^pg-spaces] No other thread can read them. `sum` belongs here: one value, used on every iteration, by one thread.

Registers are fast because there are few of them, and there are few of them because each SM has one fixed **register file** that every resident thread draws from. NVIDIA's Hopper Tuning Guide gives the numbers for the H100: 64K 32-bit registers per SM, at most 255 registers per thread, and at most 64 warps resident on one SM.[^hopper-sm] Divide one by another and the trade appears. To keep all 64 warps (2,048 threads) resident, each thread may use at most 65,536 / 2,048 = 32 registers. A kernel whose threads each need 255 fits only 256 threads, 8 warps.

Fewer warps means less work to switch to while a load waits ([G1](g1-throughput-machines.md)). The Programming Guide names the other side of the trade: capping registers per thread, through `nvcc`'s `-maxrregcount` option, can let more blocks run on an SM at once, but may force more values out of registers.[^pg-spaces] [G5](g5-occupancy.md) turns this into a calculation.

AMD splits the register file in two. **Vector registers** (VGPRs) hold a separate value for each thread of a warp; **scalar registers** (SGPRs) hold one value that every thread of the warp shares, such as a loop bound or a base address. On AMD's CDNA GPUs, AMD's HIP documentation gives 256 to 512 KiB of vector registers per compute unit and 12.5 KiB of scalar registers.[^amd-regs] A compiler that proves a value **uniform**, the same in every thread of a warp ([G2](g2-simt.md#uniform-values-and-branches-that-disappear)), can keep it once in a scalar register instead of 64 times in vector registers.

??? check "An H100 kernel runs blocks of 256 threads, and each thread uses 64 registers. Counting registers alone, how many blocks fit on one SM, and how many warps is that?"

    Four blocks. One block needs 256 × 64 = 16,384 registers, and 65,536 / 16,384 = 4. That is 1,024 threads, 32 warps, half of the 64 an SM can hold. Other limits may cut the count further; G5 counts them all.

## Local memory: where spilled values go

When a thread needs more live values than its registers hold, the compiler **spills**: it writes some values to memory and reads them back later. CUDA calls the memory that receives them **local memory**. The name describes its scope, one thread, not its place: local memory lives in device memory, with the same latency and bandwidth as any other device access.[^pg-local]

Spills are one of three reasons the Programming Guide gives for a variable landing in local memory. The other two are large structures or arrays that would use too many registers, and arrays indexed by a value the compiler cannot determine to be constant.[^pg-local] The last reason matters to a compiler writer. Registers have names, not addresses, so `r[k]` with `k` known only at run time cannot be a register. Unroll the loop over `k` and every index becomes a constant, and the array can live in registers again.

Local memory is laid out so that consecutive 32-bit words belong to consecutive threads. When every thread of a warp touches the same element of its private array, the warp's 32 accesses are neighbours and coalesce fully.[^pg-local] [G4](g4-memory-performance.md) explains why neighbouring addresses are the cheap case.

<a id="a-blocks-shared-scratchpad"></a>

## Shared memory: a block's scratchpad

The threads of one block can also cooperate through memory that none of them owns alone. NVIDIA calls it **shared memory**: an on-chip memory that every thread of a block can read and write, which persists for the kernel's run and which the Programming Guide describes as a user-managed scratchpad, with higher bandwidth and lower latency than global memory.[^pg-shared] AMD's HIP calls the same idea the **local data share** (LDS), on-chip scratchpad memory for the threads of one workgroup.[^amd-lds] Metal calls it **threadgroup memory**, and its specification says sharing data there is faster on most devices than sharing it through device memory.[^msl-as]

Three properties follow from shared memory being on the SM. First, a block sees only its own region: two blocks computing different parts of `c` cannot read each other's shared memory. (Hopper adds an exception, **distributed shared memory**, through which the blocks of a thread-block cluster can reach one another's regions.[^pg-dsmem]) Second, its contents do not outlast the work that uses them: CUDA keeps them for the kernel's run, and Metal for the lifetime of the threadgroup.[^pg-spaces][^msl-as] Third, nothing moves in or out by itself: the kernel copies data from device memory into shared memory with ordinary loads and stores.

Because any thread may read what another thread wrote, the order of those accesses matters. A thread may read a value another thread stored in shared memory only after both have passed a **barrier**, a point that no thread of the block passes until every thread has reached it.[^pg-shared] [G6](g6-synchronization.md) builds on barriers.

A kernel asks for shared memory in one of two ways. A **static allocation** has a size fixed at compile time: a `__shared__` array in CUDA, a `threadgroup` array in Metal. A **dynamic allocation** takes its size from the launch, as a third argument inside CUDA's `<<<...>>>`.[^pg-shared] The distinction matters on the H100. A static allocation is limited to 48 KB, and a block that wants more must use a dynamic allocation and opt in explicitly.[^hopper-l1]

On NVIDIA GPUs shared memory is not a separate array. It uses the same physical storage as the SM's L1 cache, so a kernel that uses more shared memory leaves less L1, and a kernel that uses none gives the whole array to L1.[^pg-shared]

## Why shared memory pays: reuse, counted by hand

Shrink the product to 4 × 4 matrices, computed by blocks of 2 × 2 threads, and follow the block that computes `c[0..2, 0..2]` (Figure 2).

In the one-thread-per-output version, thread `(0, 0)` reads row 0 of `a` and column 0 of `b`: 8 reads from device memory. Its neighbour `(0, 1)` reads row 0 of `a` again and column 1 of `b`. The block's four threads make 32 reads, but only 16 distinct values exist among them: rows 0 and 1 of `a` and columns 0 and 1 of `b`. Every value is read twice, once by each thread that needs it.

The tiled version walks `k` in **phases** of 2. In phase 0, each of the four threads copies one element of `a[0..2, 0..2]` and one of `b[0..2, 0..2]` into two 2 × 2 **tiles** in shared memory; after a barrier, each thread reads two values from each tile. Phase 1 does the same for `k` = 2 and 3. The block makes 2 phases × 8 = 16 device reads, each value once. The 32 reads that the arithmetic needs still happen, but they now reach shared memory.

<figure class="vx-figure">
<svg viewBox="0 0 760 330" role="img" aria-label="The values one 2 by 2 block reads in a 4 by 4 matrix product, and the two phases in which a tiled kernel stages them" aria-describedby="g3-f2-desc">
<title id="g3-f2-title">The values one 2 by 2 block reads in a 4 by 4 matrix product, and the two phases in which a tiled kernel stages them</title>
<desc id="g3-f2-desc">Three 4 by 4 grids: a, b and c. In c, the top-left 2 by 2 block of outputs is outlined: one output per thread. In a, rows 0 and 1 are shaded, columns 0 and 1 for phase 0 and columns 2 and 3 for phase 1. In b, columns 0 and 1 are shaded, rows 0 and 1 for phase 0 and rows 2 and 3 for phase 1. Each shaded value is needed by two of the block's four threads. Without shared memory the block reads 32 values from device memory; with two phases of tiles it reads each of the 16 values once.</desc>
<text class="vx-text" x="20" y="24">One block of 2 × 2 threads computes c[0..2, 0..2] of a 4 × 4 product</text>
<text class="vx-mono" x="120" y="60" text-anchor="middle">a</text>
<text class="vx-mono" x="380" y="60" text-anchor="middle">b</text>
<text class="vx-mono" x="640" y="60" text-anchor="middle">c</text>
<rect class="vx-cell-on" x="40" y="70" width="40" height="40"/>
<rect class="vx-cell-on" x="80" y="70" width="40" height="40"/>
<rect class="vx-box-accent" x="120" y="70" width="40" height="40"/>
<rect class="vx-box-accent" x="160" y="70" width="40" height="40"/>
<rect class="vx-cell-on" x="40" y="110" width="40" height="40"/>
<rect class="vx-cell-on" x="80" y="110" width="40" height="40"/>
<rect class="vx-box-accent" x="120" y="110" width="40" height="40"/>
<rect class="vx-box-accent" x="160" y="110" width="40" height="40"/>
<rect class="vx-box" x="40" y="150" width="40" height="40"/>
<rect class="vx-box" x="80" y="150" width="40" height="40"/>
<rect class="vx-box" x="120" y="150" width="40" height="40"/>
<rect class="vx-box" x="160" y="150" width="40" height="40"/>
<rect class="vx-box" x="40" y="190" width="40" height="40"/>
<rect class="vx-box" x="80" y="190" width="40" height="40"/>
<rect class="vx-box" x="120" y="190" width="40" height="40"/>
<rect class="vx-box" x="160" y="190" width="40" height="40"/>
<rect class="vx-cell-on" x="300" y="70" width="40" height="40"/>
<rect class="vx-cell-on" x="340" y="70" width="40" height="40"/>
<rect class="vx-box" x="380" y="70" width="40" height="40"/>
<rect class="vx-box" x="420" y="70" width="40" height="40"/>
<rect class="vx-cell-on" x="300" y="110" width="40" height="40"/>
<rect class="vx-cell-on" x="340" y="110" width="40" height="40"/>
<rect class="vx-box" x="380" y="110" width="40" height="40"/>
<rect class="vx-box" x="420" y="110" width="40" height="40"/>
<rect class="vx-box-accent" x="300" y="150" width="40" height="40"/>
<rect class="vx-box-accent" x="340" y="150" width="40" height="40"/>
<rect class="vx-box" x="380" y="150" width="40" height="40"/>
<rect class="vx-box" x="420" y="150" width="40" height="40"/>
<rect class="vx-box-accent" x="300" y="190" width="40" height="40"/>
<rect class="vx-box-accent" x="340" y="190" width="40" height="40"/>
<rect class="vx-box" x="380" y="190" width="40" height="40"/>
<rect class="vx-box" x="420" y="190" width="40" height="40"/>
<rect class="vx-box-strong" x="560" y="70" width="40" height="40"/>
<rect class="vx-box-strong" x="600" y="70" width="40" height="40"/>
<rect class="vx-box" x="640" y="70" width="40" height="40"/>
<rect class="vx-box" x="680" y="70" width="40" height="40"/>
<rect class="vx-box-strong" x="560" y="110" width="40" height="40"/>
<rect class="vx-box-strong" x="600" y="110" width="40" height="40"/>
<rect class="vx-box" x="640" y="110" width="40" height="40"/>
<rect class="vx-box" x="680" y="110" width="40" height="40"/>
<rect class="vx-box" x="560" y="150" width="40" height="40"/>
<rect class="vx-box" x="600" y="150" width="40" height="40"/>
<rect class="vx-box" x="640" y="150" width="40" height="40"/>
<rect class="vx-box" x="680" y="150" width="40" height="40"/>
<rect class="vx-box" x="560" y="190" width="40" height="40"/>
<rect class="vx-box" x="600" y="190" width="40" height="40"/>
<rect class="vx-box" x="640" y="190" width="40" height="40"/>
<rect class="vx-box" x="680" y="190" width="40" height="40"/>
<text class="vx-text-muted" x="20" y="250">tile of phase 0 (k = 0, 1)</text>
<rect class="vx-cell-on" x="200" y="238" width="16" height="16"/>
<text class="vx-text-muted" x="240" y="250">tile of phase 1 (k = 2, 3)</text>
<rect class="vx-box-accent" x="420" y="238" width="16" height="16"/>
<text class="vx-text-muted" x="460" y="250">outputs of this block</text>
<rect class="vx-box-strong" x="620" y="238" width="16" height="16"/>
<text class="vx-text" x="20" y="286">no shared memory: 4 threads × (4 + 4) = 32 device reads of 16 values</text>
<text class="vx-text" x="20" y="312">with tiles: 2 phases × (4 + 4) = 16 device reads; the 32 uses read shared memory</text>
</svg>
<figcaption>Figure 2. The reads of one 2 × 2 block. Each shaded value of <code>a</code> and <code>b</code> is needed by two threads of the block. Read straight from device memory, it is fetched twice; staged in a shared tile during its phase, it is fetched once and read twice on chip.</figcaption>
</figure>

The count generalizes. For an N × N product in blocks of T × T threads, each thread of the plain version reads 2N values, 2N³ in all. The tiled version has N / T phases, in each of which every thread copies 2 values, so each thread reads 2N / T values from device memory, 2N³ / T in all. The saving is a factor of T, and it costs two T × T tiles, 2T² × 4 bytes of shared memory per block. This is the "shared-memory tiling" that the [philosophy](../philosophy.md#performance-philosophy) lists among the GPU optimizations Vortex should eventually support.

The first example runs both schedules on the CPU and counts. It also checks that the results agree bit for bit: both add each output's products in increasing `k`, starting from `0.0`, so staging changes where values come from and nothing else.

--8<-- "includes/examples/gpu/g3-memory-hierarchy/tile_reuse.cpp.md"

For the 64 × 64 product, the plain version makes 524,288 device reads. Blocks of 16 × 16 cut that to 32,768, using 2 KB of shared memory per block. The shared-memory reads stay at 524,288 in every row: tiling does not remove reads, it moves them to a closer level.

Two cautions. A cache may already catch some of the plain version's repeated reads, so the count of device reads is an upper bound on its traffic, not a measurement ([G4](g4-memory-performance.md) makes the same point). And the saving grows with T only while the tiles fit, which is the next section.

??? check "Finish the table for the 64 × 64 product with blocks of 32 × 32 threads: how many device reads, and how many bytes of shared memory per block?"

    16,384 device reads: 2 × 64³ / 32. Each block holds two 32 × 32 tiles of `f32`, 2 × 32 × 32 × 4 = 8,192 bytes. The factor over the plain version is 32, the tile side.

## How much fits: budgets per block and per core

Shared memory has two limits: how much one block may ask for, and how much one SM holds for all the blocks resident on it. On the H100, NVIDIA gives 228 KB of shared memory per SM and 227 KB per block; the difference is 1 KB that CUDA reserves for each block.[^hopper-sm][^hopper-l1] The SM can also hold at most 32 blocks at once.[^hopper-sm]

Apple publishes the per-block limit only. The Metal Feature Set Tables give 32 KB of threadgroup memory per threadgroup for the Apple4 GPU family and later, with threadgroup memory lengths aligned to 16 bytes.[^fst] On the owner's M4 Pro, `MTLDevice.maxThreadgroupMemoryLength` reads 32,768 (macOS 27.0, 2026-09-23). Apple says each GPU core has its own threadgroup memory,[^wwdc22] but does not say how much, so how many threadgroups share one core's threadgroup memory is unknown.

The second example checks the tile pairs of the previous section against both targets. For the H100 it also asks which kind of allocation the tiles need and how many blocks fit on one SM counting shared memory alone.

--8<-- "includes/examples/gpu/g3-memory-hierarchy/block_budgets.cpp.md"

Figure 3 draws two rows of that table. With T = 32, each block costs 8 KB of tiles plus its 1 KB reserve, and 25 blocks fill all but 3 KB of the SM. With T = 64 only 6 fit, and 30 KB are left over, too little for a seventh. Every row fits Apple's per-threadgroup limit up to T = 64, which fills it exactly.

<figure class="vx-figure">
<svg viewBox="0 0 760 305" role="img" aria-label="How an H100 SM's 228 KB of shared memory divides among blocks for two tile sizes, and the per-block limits of two targets" aria-describedby="g3-f3-desc">
<title id="g3-f3-title">How an H100 SM's 228 KB of shared memory divides among blocks for two tile sizes, and the per-block limits of two targets</title>
<desc id="g3-f3-desc">Two horizontal bars, each 228 KB long, stand for one H100 SM's shared memory. The first is divided into 25 segments, each an 8 KB pair of 32 by 32 tiles followed by a 1 KB reserve, with 3 KB left over. The second is divided into 6 segments of 32 KB tiles plus 1 KB reserve, with 30 KB left over. Below, on the same scale, two short bars compare the per-block limits: 227 KB on the H100 and 32 KB on Apple GPUs from the Apple4 family on.</desc>
<text class="vx-text" x="30" y="24">One H100 SM: 228 KB of shared memory, divided among resident blocks</text>
<text class="vx-text-muted" x="30" y="54">T = 32: 8 KB of tiles + 1 KB reserved per block → 25 blocks, 3 KB unused</text>
<text class="vx-text-muted" x="30" y="142">T = 64: 32 KB of tiles + 1 KB reserved per block → 6 blocks, 30 KB unused</text>
<rect class="vx-cell-on" x="30.0" y="62" width="24.6" height="34"/>
<rect class="vx-box-accent" x="54.6" y="62" width="3.1" height="34"/>
<rect class="vx-cell-on" x="57.6" y="62" width="24.6" height="34"/>
<rect class="vx-box-accent" x="82.2" y="62" width="3.1" height="34"/>
<rect class="vx-cell-on" x="85.3" y="62" width="24.6" height="34"/>
<rect class="vx-box-accent" x="109.8" y="62" width="3.1" height="34"/>
<rect class="vx-cell-on" x="112.9" y="62" width="24.6" height="34"/>
<rect class="vx-box-accent" x="137.5" y="62" width="3.1" height="34"/>
<rect class="vx-cell-on" x="140.5" y="62" width="24.6" height="34"/>
<rect class="vx-box-accent" x="165.1" y="62" width="3.1" height="34"/>
<rect class="vx-cell-on" x="168.2" y="62" width="24.6" height="34"/>
<rect class="vx-box-accent" x="192.7" y="62" width="3.1" height="34"/>
<rect class="vx-cell-on" x="195.8" y="62" width="24.6" height="34"/>
<rect class="vx-box-accent" x="220.4" y="62" width="3.1" height="34"/>
<rect class="vx-cell-on" x="223.4" y="62" width="24.6" height="34"/>
<rect class="vx-box-accent" x="248.0" y="62" width="3.1" height="34"/>
<rect class="vx-cell-on" x="251.1" y="62" width="24.6" height="34"/>
<rect class="vx-box-accent" x="275.6" y="62" width="3.1" height="34"/>
<rect class="vx-cell-on" x="278.7" y="62" width="24.6" height="34"/>
<rect class="vx-box-accent" x="303.2" y="62" width="3.1" height="34"/>
<rect class="vx-cell-on" x="306.3" y="62" width="24.6" height="34"/>
<rect class="vx-box-accent" x="330.9" y="62" width="3.1" height="34"/>
<rect class="vx-cell-on" x="333.9" y="62" width="24.6" height="34"/>
<rect class="vx-box-accent" x="358.5" y="62" width="3.1" height="34"/>
<rect class="vx-cell-on" x="361.6" y="62" width="24.6" height="34"/>
<rect class="vx-box-accent" x="386.1" y="62" width="3.1" height="34"/>
<rect class="vx-cell-on" x="389.2" y="62" width="24.6" height="34"/>
<rect class="vx-box-accent" x="413.8" y="62" width="3.1" height="34"/>
<rect class="vx-cell-on" x="416.8" y="62" width="24.6" height="34"/>
<rect class="vx-box-accent" x="441.4" y="62" width="3.1" height="34"/>
<rect class="vx-cell-on" x="444.5" y="62" width="24.6" height="34"/>
<rect class="vx-box-accent" x="469.0" y="62" width="3.1" height="34"/>
<rect class="vx-cell-on" x="472.1" y="62" width="24.6" height="34"/>
<rect class="vx-box-accent" x="496.7" y="62" width="3.1" height="34"/>
<rect class="vx-cell-on" x="499.7" y="62" width="24.6" height="34"/>
<rect class="vx-box-accent" x="524.3" y="62" width="3.1" height="34"/>
<rect class="vx-cell-on" x="527.4" y="62" width="24.6" height="34"/>
<rect class="vx-box-accent" x="551.9" y="62" width="3.1" height="34"/>
<rect class="vx-cell-on" x="555.0" y="62" width="24.6" height="34"/>
<rect class="vx-box-accent" x="579.6" y="62" width="3.1" height="34"/>
<rect class="vx-cell-on" x="582.6" y="62" width="24.6" height="34"/>
<rect class="vx-box-accent" x="607.2" y="62" width="3.1" height="34"/>
<rect class="vx-cell-on" x="610.3" y="62" width="24.6" height="34"/>
<rect class="vx-box-accent" x="634.8" y="62" width="3.1" height="34"/>
<rect class="vx-cell-on" x="637.9" y="62" width="24.6" height="34"/>
<rect class="vx-box-accent" x="662.5" y="62" width="3.1" height="34"/>
<rect class="vx-cell-on" x="665.5" y="62" width="24.6" height="34"/>
<rect class="vx-box-accent" x="690.1" y="62" width="3.1" height="34"/>
<rect class="vx-cell-on" x="693.2" y="62" width="24.6" height="34"/>
<rect class="vx-box-accent" x="717.7" y="62" width="3.1" height="34"/>
<rect class="vx-box" x="720.8" y="62" width="9.2" height="34"/>
<rect class="vx-cell-on" x="30.0" y="150" width="98.2" height="34"/>
<rect class="vx-box-accent" x="128.2" y="150" width="3.1" height="34"/>
<rect class="vx-cell-on" x="131.3" y="150" width="98.2" height="34"/>
<rect class="vx-box-accent" x="229.6" y="150" width="3.1" height="34"/>
<rect class="vx-cell-on" x="232.6" y="150" width="98.2" height="34"/>
<rect class="vx-box-accent" x="330.9" y="150" width="3.1" height="34"/>
<rect class="vx-cell-on" x="333.9" y="150" width="98.2" height="34"/>
<rect class="vx-box-accent" x="432.2" y="150" width="3.1" height="34"/>
<rect class="vx-cell-on" x="435.3" y="150" width="98.2" height="34"/>
<rect class="vx-box-accent" x="533.5" y="150" width="3.1" height="34"/>
<rect class="vx-cell-on" x="536.6" y="150" width="98.2" height="34"/>
<rect class="vx-box-accent" x="634.8" y="150" width="3.1" height="34"/>
<rect class="vx-box" x="637.9" y="150" width="92.1" height="34"/>
<rect class="vx-cell-on" x="30" y="206" width="14" height="14"/>
<text class="vx-text-muted" x="50" y="218">tiles</text>
<rect class="vx-box-accent" x="100" y="206" width="14" height="14"/>
<text class="vx-text-muted" x="120" y="218">1 KB reserved per block</text>
<rect class="vx-box" x="290" y="206" width="14" height="14"/>
<text class="vx-text-muted" x="310" y="218">unused</text>
<text class="vx-text" x="30" y="240">Largest allocation for one block, same scale:</text>
<text class="vx-text-muted" x="30" y="258">H100: 227 KB</text>
<rect class="vx-box-strong" x="30" y="264" width="696.9" height="12"/>
<rect class="vx-box-accent" x="30" y="282" width="98.2" height="12"/>
<text class="vx-text-muted" x="138" y="293">Apple4 and later: 32 KB</text>
</svg>
<figcaption>Figure 3. Shared memory is divided, not multiplied. Bigger tiles save more device reads per block but leave room for fewer blocks, and the reserve and the unused remainder are part of the cost. The per-block limits differ by a factor of about seven between the two targets, so a tile size chosen for one can fail on the other.</figcaption>
</figure>

Shared memory is only one of the limits. With T = 16, a block has 256 threads, and 2,048 threads per SM allow 8 such blocks, far below the 76 that shared memory allows; with T = 64 a block of one thread per output would have 4,096 threads, more than the 1,024 a Metal threadgroup may hold,[^fst] so each thread must compute several outputs ([G10](g10-matmul-ladder.md) builds that kernel). [G5](g5-occupancy.md) combines all the limits into one count.

## Caches: what the hardware keeps for you

Between the SM and device memory sit caches, which hold copies of recently used data without any instruction naming them. The Programming Guide describes two levels on NVIDIA GPUs: an **L1 cache** on each SM, in the same storage as shared memory, and an **L2 cache** shared by all SMs.[^pg-caches] On the H100, the L1, texture cache and shared memory together hold 256 KB per SM, and the L2 holds 50 MB, up from 40 MB on the A100.[^hopper-l1][^hopper-l2]

The split between L1 and shared memory is chosen per kernel. The H100 offers shared-memory capacities from 0 to 228 KB per SM in fixed steps, selected through a carveout attribute, and CUDA's `cudaFuncSetCacheConfig` states a preference that the runtime may ignore.[^hopper-l1][^pg-shared] So a single "L1 size" does not exist; it depends on the kernel's shared-memory use.

AMD's GPUs keep a vector L1 cache in each compute unit, typically 16 KB, which writes through to the L2.[^amd-l1] The L2 is shared by all compute units and is the **coherence point** of the GPU: the one place where every compute unit sees the latest value of an address. The L1 caches are not kept consistent with each other by hardware; a write in one compute unit reaches another through fences, cache invalidation, atomics or the end of a kernel.[^amd-coherence]

Metal states the same limit in its memory model. A write has a **scope of coherence**, the set of threads guaranteed to observe it once properly synchronized: one thread, one threadgroup, or the whole device. By default, device memory has threadgroup coherence, and a buffer marked `coherent(device)` extends that to the whole device.[^msl-coherence] Two blocks cannot pass values to each other through device memory in the middle of a kernel without such care. [G6](g6-synchronization.md) explains the fences and scopes.

**Constant memory** is a small, read-only region of device memory for values every thread reads, typically 64 KB per device on NVIDIA GPUs, declared with `__constant__`.[^pg-constant] On AMD GPUs, constant data read uniformly across a warp is served by a scalar L1 cache shared between compute units.[^amd-sl1]

The difference between a cache and shared memory is control. The kernel decides what shared memory holds and when; a cache decides for itself and may evict a value before its next use. A compiler can count shared-memory traffic from the program text, as the first example did; cache behaviour it can only estimate, and a profiler measures.

## Device memory

At the bottom sits **device memory**, which CUDA calls **global memory**: the memory every thread of a kernel can reach, allocated by host calls such as `cudaMalloc` and persisting until freed.[^pg-global] A kernel returns nothing, so writing results into global memory is the only way to hand them back.[^pg-global] `a`, `b` and `c` of the opening example live here, and every tile starts as a copy of part of them.

On a discrete GPU, device memory is separate from the host's memory, and data reaches it through explicit copies such as `cudaMemcpy`.[^pg-global] AMD's CDNA GPUs use high-bandwidth memory (HBM), DRAM dies stacked vertically behind a wide bus.[^amd-hbm] Local memory and constant memory, above, are regions of device memory too.[^pg-local][^pg-constant]

## Address spaces: the level in the type

A GPU compiler has to know which level a pointer refers to, because each level has its own load and store instructions. Kernel languages therefore make the level part of a pointer's type, called its **address space**. The Metal Shading Language requires one on every pointer or reference argument of a kernel.[^msl-as] Its four compute address spaces are:

- `device`: buffers in device memory, readable and writable;
- `constant`: read-only buffers in device memory;
- `thread`: per-thread storage, invisible to other threads, where a kernel's local variables live;
- `threadgroup`: storage shared by the threads of one threadgroup, for that threadgroup's lifetime.[^msl-as]

CUDA marks the same distinctions with declarations instead of pointer types: `__shared__` and `__constant__` variables, automatic variables for thread-private data, and pointers into global memory.[^pg-spaces] MLIR's `gpu` dialect names four address spaces, `global`, `workgroup`, `private` and `constant`, as attributes on a memref's type.[^mlir-as] LLVM's GPU back ends number them, and the numbers differ by target:

| Level | CUDA | Metal | MLIR `gpu` | LLVM NVPTX | LLVM AMDGPU |
| --- | --- | --- | --- | --- | --- |
| device memory | global | `device` | `global` | 1 | 1 |
| read-only data | `__constant__` | `constant` | `constant` | 4 | 4 |
| block scratchpad | `__shared__` | `threadgroup` | `workgroup` | 3 | 3 (LDS) |
| thread-private memory | local | `thread` | `private` | 5 | 5 (scratch) |
| any of the above | | | | 0 (generic) | 0 (flat) |

Sources: the NVPTX and AMDGPU user guides.[^llvm-nvptx][^llvm-amdgpu] Address space 0 is a **generic** address space: a pointer that may point into any of the others, resolved when the access runs. NVPTX provides intrinsics to convert pointers between the generic and specific spaces.[^llvm-nvptx] A compiler that loses track of where a pointer points falls back to generic accesses; [G9](g9-gpu-compilers-in-llvm.md) shows how LLVM recovers the specific space.

MLIR attaches block and thread buffers to the kernel function itself, as **memory attributions**: a `gpu.func` lists `workgroup` and `private` buffers next to its arguments, and they live exactly as long as one run of the function.[^mlir-attr] The third example uses both. Each thread doubles one value into a `private` buffer, stores it into a `workgroup` tile and, after a barrier, reads the value another thread stored. `mlir-opt` 18 parses and verifies it.

--8<-- "includes/examples/gpu/g3-memory-hierarchy/address_spaces.mlir.md"

The fourth example is the same idea in Metal Shading Language, with all four address spaces in one kernel. A three-point average reads each input three times, so each threadgroup copies its slice of `x`, plus one neighbour on each side, into a `threadgroup` tile once. The weights sit in `constant` memory, and each thread's own input value in `thread` storage. The harness cannot compile Metal yet; the file was compiled at run time through `makeLibrary(source:options:)` on the owner's M4 Pro and checked against a CPU loop (2026-09-24). Its pipeline reported 144 bytes of static threadgroup memory: 34 `f32` values, 136 bytes, rounded up to a multiple of 16.

--8<-- "includes/examples/gpu/g3-memory-hierarchy/smooth.metal.md"

??? check "In the tiled product of Figure 2, which address space does each value need: the tiles, `sum`, `a`, and `c`?"

    The tiles are read by threads other than the one that stored each element, so they must be `workgroup` (Metal: `threadgroup`, CUDA: `__shared__`). `sum` is used by one thread only: `private` (Metal: `thread`), kept in a register. `a` is read by every block and never written by the kernel: device memory, and read-only, so `constant` is a candidate if it fits the small constant region. `c` is written by the kernel and read by the host afterwards: `global` (Metal: `device`).

## Unified memory on Apple silicon

Everything so far applies to any GPU. The bottom level is where Apple silicon differs. A discrete GPU has its own DRAM, and a program copies its inputs across a bus before a kernel runs and its results back after. Apple describes its GPUs as having a **unified memory model**: the CPU and GPU share one system memory.[^storage] On the owner's M4 Pro, `MTLDevice.hasUnifiedMemory` reads `true` (macOS 27.0, 2026-09-23). There is no second DRAM to copy into.

Sharing the memory does not make every buffer visible to both processors. Each Metal resource has a **storage mode**, and the storage mode decides who may access it:[^storage]

| Storage mode | Who may access it | Typical use |
| --- | --- | --- |
| `shared` | CPU and GPU; the default for buffers and textures | data the CPU writes or reads |
| `private` | GPU only | data only GPU passes produce and consume |
| `memoryless` | GPU only, in on-chip tile memory; textures only, never buffers | temporary render targets |

So a result the CPU must read belongs in a `shared` buffer. A `private` buffer lives in the same memory, but the CPU cannot access it,[^storage] so getting its contents to the CPU takes a GPU-side copy into a `shared` buffer first. Figure 4 sets the two arrangements side by side.

<figure class="vx-figure">
<svg viewBox="0 0 760 320" role="img" aria-label="A discrete GPU with its own memory beside Apple silicon's single shared memory with storage modes" aria-describedby="g3-f4-desc">
<title id="g3-f4-title">A discrete GPU with its own memory beside Apple silicon's single shared memory with storage modes</title>
<desc id="g3-f4-desc">Left: a CPU with its system memory and a discrete GPU with its own device memory, joined by a bus; an arrow marks the explicit copy of a buffer from system memory to device memory. Right: one chip holding both CPU and GPU above one system memory. Inside that memory, a shared buffer has lines to both the CPU and the GPU, and a private buffer has a line to the GPU only.</desc>
<text class="vx-text" x="20" y="24">Discrete GPU</text>
<text class="vx-text" x="410" y="24">Apple silicon: unified memory</text>
<rect class="vx-box-strong" x="30" y="50" width="130" height="50"/>
<text class="vx-text" x="95" y="80" text-anchor="middle">CPU</text>
<rect class="vx-box-strong" x="220" y="50" width="130" height="50"/>
<text class="vx-text" x="285" y="80" text-anchor="middle">GPU</text>
<line class="vx-line" x1="160" y1="75" x2="220" y2="75"/>
<text class="vx-text-muted" x="190" y="68" text-anchor="middle">bus</text>
<line class="vx-line" x1="95" y1="100" x2="95" y2="160"/>
<line class="vx-line" x1="285" y1="100" x2="285" y2="160"/>
<rect class="vx-box" x="30" y="160" width="130" height="90"/>
<text class="vx-text" x="95" y="182" text-anchor="middle">system memory</text>
<rect class="vx-box-accent" x="55" y="200" width="80" height="30"/>
<text class="vx-mono" x="95" y="220" text-anchor="middle">a</text>
<rect class="vx-box" x="220" y="160" width="130" height="90"/>
<text class="vx-text" x="285" y="182" text-anchor="middle">device memory</text>
<rect class="vx-box-accent" x="245" y="200" width="80" height="30"/>
<text class="vx-mono" x="285" y="220" text-anchor="middle">copy of a</text>
<line class="vx-line" x1="135" y1="215" x2="239" y2="215"/>
<polygon class="vx-arrowhead" points="245,215 233,210 233,220"/>
<text class="vx-text-muted" x="190" y="275" text-anchor="middle">explicit copy before the kernel runs</text>
<rect class="vx-box" x="420" y="40" width="310" height="76"/>
<text class="vx-text-muted" x="430" y="56">one chip</text>
<rect class="vx-box-strong" x="440" y="62" width="120" height="44"/>
<text class="vx-text" x="500" y="89" text-anchor="middle">CPU</text>
<rect class="vx-box-strong" x="590" y="62" width="120" height="44"/>
<text class="vx-text" x="650" y="89" text-anchor="middle">GPU</text>
<rect class="vx-box" x="420" y="160" width="310" height="90"/>
<text class="vx-text" x="575" y="246" text-anchor="middle">one system memory</text>
<rect class="vx-box-accent" x="440" y="200" width="120" height="30"/>
<text class="vx-mono" x="500" y="220" text-anchor="middle">shared buffer</text>
<rect class="vx-box-strong" x="590" y="200" width="120" height="30"/>
<text class="vx-mono" x="650" y="220" text-anchor="middle">private buffer</text>
<line class="vx-line" x1="500" y1="106" x2="500" y2="200"/>
<line class="vx-line" x1="630" y1="106" x2="530" y2="200"/>
<line class="vx-line" x1="670" y1="106" x2="670" y2="200"/>
<text class="vx-text-muted" x="575" y="272" text-anchor="middle">no copy needed; the storage mode</text>
<text class="vx-text-muted" x="575" y="290" text-anchor="middle">decides who may access each buffer</text>
</svg>
<figcaption>Figure 4. Left, a discrete GPU: the kernel reads a copy of its input in the GPU's own memory. Right, Apple silicon: CPU and GPU share one memory, so no copy is needed, but a <code>private</code> buffer is still reachable by the GPU alone.</figcaption>
</figure>

Vortex already has a rule of the same shape. Under [decision 25](../decisions/references.md#d25), a variable lent as a `&mut` argument may not appear in any other argument of the same call, so the callee is the only party that can change it while the call runs. A storage mode makes a similar promise at a coarser grain, once per buffer: `private` says only the GPU will touch it. When Vortex launches a kernel, its inputs and its `&mut` output need a placement on either kind of machine: on a discrete GPU the placement costs copies, on Apple silicon it costs a choice of storage mode.

??? check "A kernel on the M4 Pro writes a result buffer that the CPU reads next. The buffer was created `private` because the GPU writes it. What goes wrong, and what are the two fixes?"

    The CPU cannot access a `private` buffer, although it lives in the same memory. Either create the buffer `shared`, the default, so the CPU can read it after the kernel finishes, or keep it `private` and have the GPU copy it into a `shared` buffer before the CPU reads.

## Measuring it

No GPU timings are claimed here. To see the difference between device memory and threadgroup memory on your own machine:

1. Write two versions of the three-point average: the fourth example, and one that reads `x[i - 1]`, `x[i]` and `x[i + 1]` straight from device memory.
2. Check both against a CPU loop on every element before timing anything.
3. Time many runs of each and report the median with its spread, following [P1](../optimize/p1-measure-first.md). Use an input far larger than the caches, and grow it until the time per element stops changing.
4. Repeat with a wider stencil, 7 or 15 points, where each input is reused more often. The caches serve the three-point version's repeated reads well, so the gap may be small there; it is the trend with reuse that the experiment shows.
5. In Xcode's GPU tools, read the counters for device-memory traffic ([G14](g14-measuring-gpu-code.md)).

| Stencil width | Version | Elements | Median time | Spread | Time per element |
| --- | --- | --- | --- | --- | --- |
| 3 | device memory | | | | |
| 3 | threadgroup tile | | | | |
| 15 | device memory | | | | |
| 15 | threadgroup tile | | | | |

Record the machine, the operating system version and the date with the table.

## For Vortex

!!! vortex "Exercise"

    **Build** a memory-placement report for loop nests in your compiler's IR. For a nest with constant bounds, a choice of which loops become threads and blocks, and optionally a tile size T for staging, say where each value would live and whether the result fits a target. Generate no GPU code.

    1. A target description holding the facts this chapter used: registers per core and per thread, warps per core, blocks per core, shared memory per core and per block, bytes reserved per block, the static-allocation limit, the allocation granularity, threads per block, whether memory is unified, and the LLVM address-space number for each level. Fill it for the H100 and for your Apple GPU from the sources and measurements cited here, and mark every entry you have neither cited nor measured as unknown.
    2. For each value in the kernel body, a placement (private, workgroup, global, or constant) from who reads it and who writes it, with a one-line reason. State your rule in words in the report's documentation before coding it.
    3. For a tiled schedule, the tile shapes and their bytes, the device reads with and without tiling, computed from the loop bounds and subscripts, and a verdict for each target: fits, fits with a dynamic allocation, or does not fit.
    4. A warning for any thread-private array indexed by a subscript that is not constant after unrolling, since it will live in local memory.

    **Not yet:** generating GPU code ([M12](../mlir/m12-vortex-gpu-path.md) weighs the paths), inserting barriers ([G6](g6-synchronization.md)), coalescing and bank conflicts ([G4](g4-memory-performance.md)), and residency from all limits combined ([G5](g5-occupancy.md)).

    **Proof that it works:**

    - Golden test, the stage 10 kernel at `[f32; 64, 64]`, one thread per output: `sum` private; `a` and `b` global and read-only; `c` global, written once per thread. Device reads: 524,288.
    - The same kernel with 16 × 16 blocks and staging: two workgroup tiles of 1,024 bytes each; 32,768 device reads; fits both targets with a static allocation.
    - Budgets: a 64 × 64 tile pair (32,768 bytes) fits the Apple target exactly; a 128 × 128 pair is rejected for Apple and needs a dynamic allocation on the H100.
    - A private `[f32; 8]` indexed by a loop variable produces the local-memory warning, and the warning disappears when the loop is marked for full unrolling.
    - A kernel in which every thread of a block adds into one total: the report refuses a private placement for the total, because other threads must see it.
    - A differential test: on a few hundred random N and T, the report's device-read counts match a brute-force counter like this chapter's first example.

## Key ideas

!!! recap "Questions you can now answer"

    - **Who owns each level of GPU memory?** Registers and local memory: one thread. Shared memory: one block. L1: one SM. L2 and device memory: the whole device.
    - **Why are registers and shared memory scarce?** Each SM has a fixed supply that every resident thread and block divides; taking more per thread or per block leaves room for fewer of them.
    - **What is local memory, and when does a value land there?** A thread's private slice of device memory, used for spills, large private data and arrays indexed by values not known at compile time.
    - **What does staging tiles in shared memory save?** Device reads, by a factor of the tile side T in the matrix product, at a cost of 2T² × 4 bytes of shared memory per block.
    - **How does a cache differ from shared memory?** The hardware decides what a cache holds; the kernel decides what shared memory holds, so its traffic can be counted from the program.
    - **How does a compiler know which level a pointer refers to?** Its address space, part of its type: `threadgroup` in Metal, `workgroup` in MLIR, 3 in LLVM's NVPTX and AMDGPU back ends.
    - **What does unified memory change on Apple silicon?** No copies between CPU and GPU memory, but a buffer's storage mode still decides whether the CPU may access it.

## Where this comes back

!!! next "You will use this again in"

    - [G4. Memory performance: coalescing and bank conflicts](g4-memory-performance.md): *shared memory*, *tile*, *device memory*
    - [G5. Occupancy and latency hiding](g5-occupancy.md): *register file*, *spill*, *shared-memory budget*
    - [G6. Synchronization, atomics and reductions](g6-synchronization.md): *barrier*, *scope of coherence*
    - [G9. GPU compilers inside LLVM](g9-gpu-compilers-in-llvm.md): *address space*, *generic address space*
    - [G10. The GPU matmul ladder](g10-matmul-ladder.md): *tile*, *phase*, *reuse*
    - [M10. MLIR for GPUs](../mlir/m10-mlir-for-gpus.md): *memory attribution*, *workgroup address space*
    - [M12. Designing Vortex's GPU path](../mlir/m12-vortex-gpu-path.md): *unified memory*, *storage mode*, *target description*

## Sources and further reading

Read the Programming Guide's section on device memory spaces first, then the Hopper Tuning Guide for one chip's real numbers, then the address-space sections of the Metal Shading Language Specification and of LLVM's GPU user guides.

[^pg-spaces]: NVIDIA, "CUDA Programming Guide", v13.4.2, section 2.3.3, "GPU Device Memory Spaces", table 1 and section 2.3.3.3, "Registers". <https://docs.nvidia.com/cuda/cuda-programming-guide/02-basics/writing-cuda-kernels.html#gpu-device-memory-spaces>
[^pg-global]: NVIDIA, "CUDA Programming Guide", v13.4.2, section 2.3.3.1, "Global Memory". <https://docs.nvidia.com/cuda/cuda-programming-guide/02-basics/writing-cuda-kernels.html#global-memory>
[^pg-shared]: NVIDIA, "CUDA Programming Guide", v13.4.2, section 2.3.3.2, "Shared Memory", with its subsections on static and dynamic allocation. <https://docs.nvidia.com/cuda/cuda-programming-guide/02-basics/writing-cuda-kernels.html#shared-memory>
[^pg-local]: NVIDIA, "CUDA Programming Guide", v13.4.2, section 2.3.3.4, "Local Memory". <https://docs.nvidia.com/cuda/cuda-programming-guide/02-basics/writing-cuda-kernels.html#local-memory>
[^pg-constant]: NVIDIA, "CUDA Programming Guide", v13.4.2, section 2.3.3.5, "Constant Memory". <https://docs.nvidia.com/cuda/cuda-programming-guide/02-basics/writing-cuda-kernels.html#constant-memory>
[^pg-caches]: NVIDIA, "CUDA Programming Guide", v13.4.2, section 2.3.3.6, "Caches". <https://docs.nvidia.com/cuda/cuda-programming-guide/02-basics/writing-cuda-kernels.html#caches>
[^pg-dsmem]: NVIDIA, "CUDA Programming Guide", v13.4.2, section 2.3.3.8, "Distributed Shared Memory". <https://docs.nvidia.com/cuda/cuda-programming-guide/02-basics/writing-cuda-kernels.html#distributed-shared-memory>
[^hopper-sm]: NVIDIA, "NVIDIA Hopper Tuning Guide", section 1.4.1.1, "Occupancy". <https://docs.nvidia.com/cuda/hopper-tuning-guide/index.html#occupancy>
[^hopper-l1]: NVIDIA, "NVIDIA Hopper Tuning Guide", section 1.4.2.4, "Unified Shared Memory/L1/Texture Cache". <https://docs.nvidia.com/cuda/hopper-tuning-guide/index.html#unified-shared-memory-l1-texture-cache>
[^hopper-l2]: NVIDIA, "NVIDIA Hopper Tuning Guide", section 1.4.2.2, "Increased L2 Capacity". <https://docs.nvidia.com/cuda/hopper-tuning-guide/index.html#increased-l2-capacity>
[^amd-regs]: AMD, "Hardware implementation", HIP 7.15.0 documentation, sections "Vector arithmetic logic unit (VALU)" and "Scalar arithmetic logic unit (SALU)". <https://rocm.docs.amd.com/projects/HIP/en/latest/understand/hardware_implementation.html#vector-arithmetic-logic-unit-valu>
[^amd-lds]: AMD, "Hardware implementation", HIP 7.15.0 documentation, section "Local data share (LDS)". <https://rocm.docs.amd.com/projects/HIP/en/latest/understand/hardware_implementation.html#local-data-share-lds>
[^amd-l1]: AMD, "Hardware implementation", HIP 7.15.0 documentation, section "Vector L1 cache". <https://rocm.docs.amd.com/projects/HIP/en/latest/understand/hardware_implementation.html#vector-l1-cache>
[^amd-sl1]: AMD, "Hardware implementation", HIP 7.15.0 documentation, section "Scalar L1 data cache (sL1D)". <https://rocm.docs.amd.com/projects/HIP/en/latest/understand/hardware_implementation.html#scalar-l1-data-cache-sl1d>
[^amd-coherence]: AMD, "Hardware implementation", HIP 7.15.0 documentation, sections "L2 cache architecture" and "Memory coherence". <https://rocm.docs.amd.com/projects/HIP/en/latest/understand/hardware_implementation.html#memory-coherence>
[^amd-hbm]: AMD, "Hardware implementation", HIP 7.15.0 documentation, section "Memory organization". <https://rocm.docs.amd.com/projects/HIP/en/latest/understand/hardware_implementation.html#memory-organization>
[^msl-as]: Apple, "Metal Shading Language Specification", version 4.1, 2026, section 4, "Address Spaces", sections 4.1 to 4.4. <https://developer.apple.com/metal/Metal-Shading-Language-Specification.pdf>
[^msl-coherence]: Apple, "Metal Shading Language Specification", version 4.1, 2026, section 4.8, "Memory Coherency". <https://developer.apple.com/metal/Metal-Shading-Language-Specification.pdf>
[^fst]: Apple, "Metal Feature Set Tables", May 2026, table "GPU implementation limits by family": maximum total threadgroup memory allocation, threadgroup memory length alignment, and maximum threads per threadgroup. <https://developer.apple.com/metal/Metal-Feature-Set-Tables.pdf>
[^wwdc22]: Apple, "Scale compute workloads across Apple GPUs", WWDC22 session 10159, 2022: the part on threadgroup memory and threadgroup atomics. <https://developer.apple.com/videos/play/wwdc2022/10159/>
[^storage]: Apple, "Choosing a resource storage mode for Apple GPUs", Metal documentation. <https://developer.apple.com/documentation/metal/choosing-a-resource-storage-mode-for-apple-gpus>
[^mlir-as]: MLIR Project, "'gpu' Dialect", section "GPU address spaces". <https://mlir.llvm.org/docs/Dialects/GPU/#gpu-address-spaces>
[^mlir-attr]: MLIR Project, "'gpu' Dialect", section "Memory attribution". <https://mlir.llvm.org/docs/Dialects/GPU/#memory-attribution>
[^llvm-nvptx]: LLVM Project, "User Guide for NVPTX Back-end", section "Address Spaces". <https://llvm.org/docs/NVPTXUsage.html#address-spaces>
[^llvm-amdgpu]: LLVM Project, "User Guide for AMDGPU Backend", section "AMDGPU Address Spaces". <https://llvm.org/docs/AMDGPUUsage.html#amdgpu-address-spaces>

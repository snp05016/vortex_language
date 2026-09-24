# G5. Occupancy and latency hiding

<p class="page-intro">A GPU keeps its arithmetic units busy by running far more warps than fit on the hardware at once, switching to a ready one whenever another stalls. This chapter defines occupancy, the fraction of that switching room a kernel uses, works out the budgets that cap it, and follows a well known case where maximizing occupancy makes a kernel slower.</p>

<p class="vx-meta" markdown="1">Level: Intermediate · Reading time: about 30 minutes · Builds on: [G2. The SIMT execution model](g2-simt.md), [G3. The GPU memory hierarchy](g3-memory-hierarchy.md)</p>

???+ remember "Before you start, remember"

    ??? question "What is a warp, and how many lanes act on one instruction together?"

        A warp is a group of threads that a GPU's scheduler issues one instruction to at a time; on current NVIDIA GPUs and on Apple GPUs it is 32 lanes wide.

        Introduced in [G2. The SIMT execution model](g2-simt.md).

    ??? question "Where do a thread's local variables live, and why is reading one fast?"

        In registers: a private, per-thread storage area built into the streaming multiprocessor itself, so reading or writing one never leaves the core.

        Introduced in [G3. The GPU memory hierarchy](g3-memory-hierarchy.md).

    ??? question "Why does a matrix transpose need a shared-memory tile at all?"

        Its read and its write cannot both be coalesced by any thread mapping, because the index that comes last in the read comes first in the write. The kernel stages one tile through shared memory, the on-chip scratchpad, to turn the data around where a strided access is cheap to make.

        Introduced in [G4. Memory performance: coalescing and bank conflicts](g4-memory-performance.md).

    ??? question "May a Vortex compiler reorder the additions in a dot-product accumulation?"

        No. Each floating-point operation is one IEEE 754 operation, rounded once, and must not be contracted, reassociated or reordered.

        Introduced in [Numbers, literals and casts, decision 56](../decisions/numbers.md#d56).

!!! goals "In this chapter"

    - Define occupancy as a ratio, and compute it from a kernel's threads per block, registers per thread and shared memory per block.
    - Identify which of three budgets, registers, shared memory or the SM's own thread and block limits, caps a given kernel's occupancy.
    - State Little's law and use it to compute how many operations must be in flight to reach a machine's peak throughput.
    - Explain why a kernel that trades occupancy for instruction-level parallelism can run faster, not slower.
    - Explain why none of this changes what a Vortex program computes.

## One thread's share of the SM

Return to the matrix product from [G4](g4-memory-performance.md), unchanged:

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

On the simplest GPU mapping, one thread runs this whole body for one `(row, column)` pair. At any point in the `k` loop, that thread is holding a handful of values at once: `row`, `column`, `k` and `sum`, plus whatever addresses the compiler computes from them. A GPU compiler keeps each of those values in a **register**, one of a fixed number of per-thread storage slots built into the streaming multiprocessor, the same fast, private memory named in [G3](g3-memory-hierarchy.md). The whole register file is a shared, finite resource: on an NVIDIA GPU of compute capability 9.0 (Hopper), one SM has 65,536 32-bit registers to divide among every thread resident on it.[^pg-table30]

A thread that holds more values at once needs more registers. Suppose a thread computed two output columns instead of one, an accumulator for each:

```vortex
// fragment
let mut sum0: f32 = 0.0;
let mut sum1: f32 = 0.0;
// both live across the same k loop: two accumulators, roughly twice the
// register footprint of one thread computing one output.
```

This is the trade that **register tiling** makes, a technique the matmul ladder in [G10](g10-matmul-ladder.md) builds up in stages: fewer, busier threads, each holding more live state. That state has to come from the same register file every other resident thread is sharing, and the file does not grow to fit it. This chapter is about what happens to the rest of the SM when one kernel asks for more of that file per thread.

## Occupancy: how much of the switching room is used

An SM does not run one warp at a time and wait. It keeps a pool of **resident** warps, warps that have been assigned registers and shared memory and are ready to run, and its scheduler issues an instruction from whichever resident warp is not currently stalled. A warp stalls when its next instruction depends on a result that has not arrived yet, a load from memory or the far end of a multi-cycle arithmetic operation. While it waits, the scheduler issues from a different resident warp instead, rather than leaving the pipeline idle. This is why keeping more warps resident helps: the more of them there are, the more likely at least one has an instruction ready to issue on any given cycle.

**Occupancy** is how full that pool is: the number of warps resident on an SM, divided by the largest number the SM can hold.[^bp-occ] It is a ratio, not a resource by itself, and three separate resources cap the number of thread blocks (and so the number of warps) an SM can keep resident at once:

- **Threads.** The SM has a maximum number of resident threads; a block that requests more threads leaves room for fewer other blocks.
- **Registers.** Every resident thread's registers come out of the SM's one register file; a block whose threads use more registers each leaves room for fewer blocks.
- **Shared memory.** Every resident block's shared-memory allocation comes out of the SM's one pool of shared memory; a block that asks for more leaves room for fewer blocks.

A fourth number, the maximum number of resident blocks the SM allows regardless of the other three, is a hard ceiling on top of all of them. NVIDIA's Best Practices Guide walks through exactly this calculation, dividing each of the SM's per-SM totals by the amount one block asks for, and taking the smallest result as the number of resident blocks.[^bp-calc] Whichever of the four gives the smallest number is the one that decided occupancy for that kernel; the other three had room to spare.

The example below runs that calculation for a Hopper SM (2,048 threads, 64 warps and 32 blocks resident at most,[^pg-table30] 65,536 registers and 228 KiB of shared memory in total[^hopper-occ]) against a few kernel shapes. It is a simplified version of what NVIDIA's own occupancy tools compute: real hardware rounds a block's register and shared-memory use up to an allocation granularity that this ignores, so its numbers are upper bounds on what a profiler would report, not a substitute for measuring.

--8<-- "includes/examples/gpu/g5-occupancy/occupancy.cpp.md"

Read the first two rows together. Two hundred fifty-six threads per block is eight warps; at 32 registers per thread, the register budget allows eight such blocks (65,536 ÷ (32 × 256) = 8), exactly as many as the thread budget allows (2,048 ÷ 256 = 8), so neither one is the tighter limit and the SM fills to 64 of its 64 warps: 100 percent. Doubling the registers per thread to 64 does not change the thread budget, but it halves the register budget to four blocks, and the SM occupancy is now 32 of 64 warps: 50 percent. The last two rows hold registers fixed and instead grow the shared memory each block asks for; 32 KiB per block admits seven blocks out of the SM's 228 KiB, and 96 KiB admits only two. In every row, the smallest of the four budgets is the one printed as the limit.

??? check "A kernel launches 256-thread blocks that use 64 registers per thread on the Hopper SM above. Which budget limits it, and what occupancy results?"

    Registers: 65,536 ÷ (64 × 256) = 4 blocks, while the thread budget alone would allow 8. Four blocks of eight warps each is 32 active warps out of 64, so the occupancy is 50 percent. This is exactly the second row of the table above.

## The common advice, and where it runs out

It is common advice, repeated across CUDA guides, that hiding latency on a GPU means keeping many warps resident, so that when one stalls the scheduler always has another ready one to switch to. Read on its own, that advice suggests always maximizing occupancy. Vasily Volkov opened a 2010 GPU Technology Conference talk by naming exactly this recommendation and its usual motivation, "the only way to hide latencies," and then complicated it.[^volkov-prologue]

His first slide is a measurement, not an argument. CUBLAS, NVIDIA's matrix-multiply library, changed its kernel between versions 1.1 and 2.0 on a G80: the newer kernel used 64 threads per block instead of 512, an eight times smaller block, and its measured occupancy on that GPU fell from 67 percent to 33 percent. Its measured performance rose from 128 to 204.5 GFlop/s, about 1.6 times faster, at roughly half the occupancy.[^volkov-table] A batch of 1,024-point complex FFTs shows the same shape: CUFFT 2.2 to 2.3 shrank its thread blocks from 256 to 64 threads, occupancy fell from 33 percent to 17 percent, and measured performance rose from 45 to 93 GFlop/s, almost doubling.[^volkov-table] Both are real libraries' real kernels on one GPU generation, not a synthetic microbenchmark; Volkov states the conclusion directly: maximizing occupancy can cost performance.[^volkov-table]

He names two fallacies behind the common advice: that multithreading is the only way to hide latency on a GPU, and that shared memory is as fast as registers.[^volkov-fallacies] The rest of this chapter works out why the first is wrong, and what a thread can do instead of waiting for more neighbors to be scheduled in.

## Latency, throughput and Little's law

**Latency** is how long one operation takes to produce its result: on the architecture Volkov measured, about 20 cycles for an arithmetic instruction and 400 or more cycles for a memory access.[^volkov-latency] A dependent instruction, one that reads a value another instruction just wrote, cannot issue until that latency has passed. An independent instruction, one that reads none of the values still in flight, can issue immediately; the wait only shows up when there is nothing independent left to do.

**Throughput** is a different quantity: how many operations complete per cycle once the pipeline is full, a rate rather than a duration. Latency and throughput get compared as if they were on the same scale, but they are not; Volkov's example on the GPU he measured gives roughly 480 arithmetic operations completing per cycle across the whole chip and about 32 memory operations per cycle, figures that describe steady-state capacity, not how long any single operation takes.[^volkov-throughput]

These two quantities combine through **Little's law**, a result from queueing theory that Volkov states in one line: the parallelism needed to reach full throughput is latency multiplied by throughput.[^volkov-little] On the G80 to GT200 architecture he used as a worked example, arithmetic latency is about 24 cycles and one SM completes about 8 operations per cycle at peak, so an SM needs about 24 × 8 = 192 independent operations in flight at once to keep that throughput fully occupied.[^volkov-little] Fewer than that, and some cycles pass with nothing ready to issue; the pipeline is not full, no matter how the 192 would have been supplied.

<figure class="vx-figure">
<svg viewBox="0 0 760 280" role="img" aria-label="A grid of 192 operations in flight, filled by 8 arriving and 8 completing every cycle over a 24-cycle latency" aria-describedby="g5-f1-desc">
<title id="g5-f1-title">Little's law: how many operations must be in flight</title>
<desc id="g5-f1-desc">A grid of 24 columns by 8 rows, 192 cells, each cell one operation currently between issue and completion. Each column is one cycle's worth of operations: the rightmost column just began, and the leftmost column is about to complete after 24 cycles in flight. Every cycle, 8 operations enter on the right and 8 complete and leave on the left, so the grid always holds 24 times 8 equals 192 operations, the parallelism this SM needs to keep 8 operations per cycle of throughput fully hidden behind 24 cycles of latency.</desc>
<text class="vx-text" x="20" y="24">One SM, one instruction type: 24 cycles of latency, 8 operations/cycle of throughput</text>
<line class="vx-line" x1="141" y1="54" x2="619" y2="54"/>
<line class="vx-line" x1="141" y1="48" x2="141" y2="60"/>
<line class="vx-line" x1="619" y1="48" x2="619" y2="60"/>
<text class="vx-text-muted" x="380" y="46" text-anchor="middle">24 cycles: one operation's latency</text>
<rect class="vx-cell-on" x="141" y="74" width="18" height="16"/>
<rect class="vx-cell-on" x="161" y="74" width="18" height="16"/>
<rect class="vx-cell-on" x="181" y="74" width="18" height="16"/>
<rect class="vx-cell-on" x="201" y="74" width="18" height="16"/>
<rect class="vx-cell-on" x="221" y="74" width="18" height="16"/>
<rect class="vx-cell-on" x="241" y="74" width="18" height="16"/>
<rect class="vx-cell-on" x="261" y="74" width="18" height="16"/>
<rect class="vx-cell-on" x="281" y="74" width="18" height="16"/>
<rect class="vx-cell-on" x="301" y="74" width="18" height="16"/>
<rect class="vx-cell-on" x="321" y="74" width="18" height="16"/>
<rect class="vx-cell-on" x="341" y="74" width="18" height="16"/>
<rect class="vx-cell-on" x="361" y="74" width="18" height="16"/>
<rect class="vx-cell-on" x="381" y="74" width="18" height="16"/>
<rect class="vx-cell-on" x="401" y="74" width="18" height="16"/>
<rect class="vx-cell-on" x="421" y="74" width="18" height="16"/>
<rect class="vx-cell-on" x="441" y="74" width="18" height="16"/>
<rect class="vx-cell-on" x="461" y="74" width="18" height="16"/>
<rect class="vx-cell-on" x="481" y="74" width="18" height="16"/>
<rect class="vx-cell-on" x="501" y="74" width="18" height="16"/>
<rect class="vx-cell-on" x="521" y="74" width="18" height="16"/>
<rect class="vx-cell-on" x="541" y="74" width="18" height="16"/>
<rect class="vx-cell-on" x="561" y="74" width="18" height="16"/>
<rect class="vx-cell-on" x="581" y="74" width="18" height="16"/>
<rect class="vx-cell-on" x="601" y="74" width="18" height="16"/>
<rect class="vx-cell-on" x="141" y="92" width="18" height="16"/>
<rect class="vx-cell-on" x="161" y="92" width="18" height="16"/>
<rect class="vx-cell-on" x="181" y="92" width="18" height="16"/>
<rect class="vx-cell-on" x="201" y="92" width="18" height="16"/>
<rect class="vx-cell-on" x="221" y="92" width="18" height="16"/>
<rect class="vx-cell-on" x="241" y="92" width="18" height="16"/>
<rect class="vx-cell-on" x="261" y="92" width="18" height="16"/>
<rect class="vx-cell-on" x="281" y="92" width="18" height="16"/>
<rect class="vx-cell-on" x="301" y="92" width="18" height="16"/>
<rect class="vx-cell-on" x="321" y="92" width="18" height="16"/>
<rect class="vx-cell-on" x="341" y="92" width="18" height="16"/>
<rect class="vx-cell-on" x="361" y="92" width="18" height="16"/>
<rect class="vx-cell-on" x="381" y="92" width="18" height="16"/>
<rect class="vx-cell-on" x="401" y="92" width="18" height="16"/>
<rect class="vx-cell-on" x="421" y="92" width="18" height="16"/>
<rect class="vx-cell-on" x="441" y="92" width="18" height="16"/>
<rect class="vx-cell-on" x="461" y="92" width="18" height="16"/>
<rect class="vx-cell-on" x="481" y="92" width="18" height="16"/>
<rect class="vx-cell-on" x="501" y="92" width="18" height="16"/>
<rect class="vx-cell-on" x="521" y="92" width="18" height="16"/>
<rect class="vx-cell-on" x="541" y="92" width="18" height="16"/>
<rect class="vx-cell-on" x="561" y="92" width="18" height="16"/>
<rect class="vx-cell-on" x="581" y="92" width="18" height="16"/>
<rect class="vx-cell-on" x="601" y="92" width="18" height="16"/>
<rect class="vx-cell-on" x="141" y="110" width="18" height="16"/>
<rect class="vx-cell-on" x="161" y="110" width="18" height="16"/>
<rect class="vx-cell-on" x="181" y="110" width="18" height="16"/>
<rect class="vx-cell-on" x="201" y="110" width="18" height="16"/>
<rect class="vx-cell-on" x="221" y="110" width="18" height="16"/>
<rect class="vx-cell-on" x="241" y="110" width="18" height="16"/>
<rect class="vx-cell-on" x="261" y="110" width="18" height="16"/>
<rect class="vx-cell-on" x="281" y="110" width="18" height="16"/>
<rect class="vx-cell-on" x="301" y="110" width="18" height="16"/>
<rect class="vx-cell-on" x="321" y="110" width="18" height="16"/>
<rect class="vx-cell-on" x="341" y="110" width="18" height="16"/>
<rect class="vx-cell-on" x="361" y="110" width="18" height="16"/>
<rect class="vx-cell-on" x="381" y="110" width="18" height="16"/>
<rect class="vx-cell-on" x="401" y="110" width="18" height="16"/>
<rect class="vx-cell-on" x="421" y="110" width="18" height="16"/>
<rect class="vx-cell-on" x="441" y="110" width="18" height="16"/>
<rect class="vx-cell-on" x="461" y="110" width="18" height="16"/>
<rect class="vx-cell-on" x="481" y="110" width="18" height="16"/>
<rect class="vx-cell-on" x="501" y="110" width="18" height="16"/>
<rect class="vx-cell-on" x="521" y="110" width="18" height="16"/>
<rect class="vx-cell-on" x="541" y="110" width="18" height="16"/>
<rect class="vx-cell-on" x="561" y="110" width="18" height="16"/>
<rect class="vx-cell-on" x="581" y="110" width="18" height="16"/>
<rect class="vx-cell-on" x="601" y="110" width="18" height="16"/>
<rect class="vx-cell-on" x="141" y="128" width="18" height="16"/>
<rect class="vx-cell-on" x="161" y="128" width="18" height="16"/>
<rect class="vx-cell-on" x="181" y="128" width="18" height="16"/>
<rect class="vx-cell-on" x="201" y="128" width="18" height="16"/>
<rect class="vx-cell-on" x="221" y="128" width="18" height="16"/>
<rect class="vx-cell-on" x="241" y="128" width="18" height="16"/>
<rect class="vx-cell-on" x="261" y="128" width="18" height="16"/>
<rect class="vx-cell-on" x="281" y="128" width="18" height="16"/>
<rect class="vx-cell-on" x="301" y="128" width="18" height="16"/>
<rect class="vx-cell-on" x="321" y="128" width="18" height="16"/>
<rect class="vx-cell-on" x="341" y="128" width="18" height="16"/>
<rect class="vx-cell-on" x="361" y="128" width="18" height="16"/>
<rect class="vx-cell-on" x="381" y="128" width="18" height="16"/>
<rect class="vx-cell-on" x="401" y="128" width="18" height="16"/>
<rect class="vx-cell-on" x="421" y="128" width="18" height="16"/>
<rect class="vx-cell-on" x="441" y="128" width="18" height="16"/>
<rect class="vx-cell-on" x="461" y="128" width="18" height="16"/>
<rect class="vx-cell-on" x="481" y="128" width="18" height="16"/>
<rect class="vx-cell-on" x="501" y="128" width="18" height="16"/>
<rect class="vx-cell-on" x="521" y="128" width="18" height="16"/>
<rect class="vx-cell-on" x="541" y="128" width="18" height="16"/>
<rect class="vx-cell-on" x="561" y="128" width="18" height="16"/>
<rect class="vx-cell-on" x="581" y="128" width="18" height="16"/>
<rect class="vx-cell-on" x="601" y="128" width="18" height="16"/>
<rect class="vx-cell-on" x="141" y="146" width="18" height="16"/>
<rect class="vx-cell-on" x="161" y="146" width="18" height="16"/>
<rect class="vx-cell-on" x="181" y="146" width="18" height="16"/>
<rect class="vx-cell-on" x="201" y="146" width="18" height="16"/>
<rect class="vx-cell-on" x="221" y="146" width="18" height="16"/>
<rect class="vx-cell-on" x="241" y="146" width="18" height="16"/>
<rect class="vx-cell-on" x="261" y="146" width="18" height="16"/>
<rect class="vx-cell-on" x="281" y="146" width="18" height="16"/>
<rect class="vx-cell-on" x="301" y="146" width="18" height="16"/>
<rect class="vx-cell-on" x="321" y="146" width="18" height="16"/>
<rect class="vx-cell-on" x="341" y="146" width="18" height="16"/>
<rect class="vx-cell-on" x="361" y="146" width="18" height="16"/>
<rect class="vx-cell-on" x="381" y="146" width="18" height="16"/>
<rect class="vx-cell-on" x="401" y="146" width="18" height="16"/>
<rect class="vx-cell-on" x="421" y="146" width="18" height="16"/>
<rect class="vx-cell-on" x="441" y="146" width="18" height="16"/>
<rect class="vx-cell-on" x="461" y="146" width="18" height="16"/>
<rect class="vx-cell-on" x="481" y="146" width="18" height="16"/>
<rect class="vx-cell-on" x="501" y="146" width="18" height="16"/>
<rect class="vx-cell-on" x="521" y="146" width="18" height="16"/>
<rect class="vx-cell-on" x="541" y="146" width="18" height="16"/>
<rect class="vx-cell-on" x="561" y="146" width="18" height="16"/>
<rect class="vx-cell-on" x="581" y="146" width="18" height="16"/>
<rect class="vx-cell-on" x="601" y="146" width="18" height="16"/>
<rect class="vx-cell-on" x="141" y="164" width="18" height="16"/>
<rect class="vx-cell-on" x="161" y="164" width="18" height="16"/>
<rect class="vx-cell-on" x="181" y="164" width="18" height="16"/>
<rect class="vx-cell-on" x="201" y="164" width="18" height="16"/>
<rect class="vx-cell-on" x="221" y="164" width="18" height="16"/>
<rect class="vx-cell-on" x="241" y="164" width="18" height="16"/>
<rect class="vx-cell-on" x="261" y="164" width="18" height="16"/>
<rect class="vx-cell-on" x="281" y="164" width="18" height="16"/>
<rect class="vx-cell-on" x="301" y="164" width="18" height="16"/>
<rect class="vx-cell-on" x="321" y="164" width="18" height="16"/>
<rect class="vx-cell-on" x="341" y="164" width="18" height="16"/>
<rect class="vx-cell-on" x="361" y="164" width="18" height="16"/>
<rect class="vx-cell-on" x="381" y="164" width="18" height="16"/>
<rect class="vx-cell-on" x="401" y="164" width="18" height="16"/>
<rect class="vx-cell-on" x="421" y="164" width="18" height="16"/>
<rect class="vx-cell-on" x="441" y="164" width="18" height="16"/>
<rect class="vx-cell-on" x="461" y="164" width="18" height="16"/>
<rect class="vx-cell-on" x="481" y="164" width="18" height="16"/>
<rect class="vx-cell-on" x="501" y="164" width="18" height="16"/>
<rect class="vx-cell-on" x="521" y="164" width="18" height="16"/>
<rect class="vx-cell-on" x="541" y="164" width="18" height="16"/>
<rect class="vx-cell-on" x="561" y="164" width="18" height="16"/>
<rect class="vx-cell-on" x="581" y="164" width="18" height="16"/>
<rect class="vx-cell-on" x="601" y="164" width="18" height="16"/>
<rect class="vx-cell-on" x="141" y="182" width="18" height="16"/>
<rect class="vx-cell-on" x="161" y="182" width="18" height="16"/>
<rect class="vx-cell-on" x="181" y="182" width="18" height="16"/>
<rect class="vx-cell-on" x="201" y="182" width="18" height="16"/>
<rect class="vx-cell-on" x="221" y="182" width="18" height="16"/>
<rect class="vx-cell-on" x="241" y="182" width="18" height="16"/>
<rect class="vx-cell-on" x="261" y="182" width="18" height="16"/>
<rect class="vx-cell-on" x="281" y="182" width="18" height="16"/>
<rect class="vx-cell-on" x="301" y="182" width="18" height="16"/>
<rect class="vx-cell-on" x="321" y="182" width="18" height="16"/>
<rect class="vx-cell-on" x="341" y="182" width="18" height="16"/>
<rect class="vx-cell-on" x="361" y="182" width="18" height="16"/>
<rect class="vx-cell-on" x="381" y="182" width="18" height="16"/>
<rect class="vx-cell-on" x="401" y="182" width="18" height="16"/>
<rect class="vx-cell-on" x="421" y="182" width="18" height="16"/>
<rect class="vx-cell-on" x="441" y="182" width="18" height="16"/>
<rect class="vx-cell-on" x="461" y="182" width="18" height="16"/>
<rect class="vx-cell-on" x="481" y="182" width="18" height="16"/>
<rect class="vx-cell-on" x="501" y="182" width="18" height="16"/>
<rect class="vx-cell-on" x="521" y="182" width="18" height="16"/>
<rect class="vx-cell-on" x="541" y="182" width="18" height="16"/>
<rect class="vx-cell-on" x="561" y="182" width="18" height="16"/>
<rect class="vx-cell-on" x="581" y="182" width="18" height="16"/>
<rect class="vx-cell-on" x="601" y="182" width="18" height="16"/>
<rect class="vx-cell-on" x="141" y="200" width="18" height="16"/>
<rect class="vx-cell-on" x="161" y="200" width="18" height="16"/>
<rect class="vx-cell-on" x="181" y="200" width="18" height="16"/>
<rect class="vx-cell-on" x="201" y="200" width="18" height="16"/>
<rect class="vx-cell-on" x="221" y="200" width="18" height="16"/>
<rect class="vx-cell-on" x="241" y="200" width="18" height="16"/>
<rect class="vx-cell-on" x="261" y="200" width="18" height="16"/>
<rect class="vx-cell-on" x="281" y="200" width="18" height="16"/>
<rect class="vx-cell-on" x="301" y="200" width="18" height="16"/>
<rect class="vx-cell-on" x="321" y="200" width="18" height="16"/>
<rect class="vx-cell-on" x="341" y="200" width="18" height="16"/>
<rect class="vx-cell-on" x="361" y="200" width="18" height="16"/>
<rect class="vx-cell-on" x="381" y="200" width="18" height="16"/>
<rect class="vx-cell-on" x="401" y="200" width="18" height="16"/>
<rect class="vx-cell-on" x="421" y="200" width="18" height="16"/>
<rect class="vx-cell-on" x="441" y="200" width="18" height="16"/>
<rect class="vx-cell-on" x="461" y="200" width="18" height="16"/>
<rect class="vx-cell-on" x="481" y="200" width="18" height="16"/>
<rect class="vx-cell-on" x="501" y="200" width="18" height="16"/>
<rect class="vx-cell-on" x="521" y="200" width="18" height="16"/>
<rect class="vx-cell-on" x="541" y="200" width="18" height="16"/>
<rect class="vx-cell-on" x="561" y="200" width="18" height="16"/>
<rect class="vx-cell-on" x="581" y="200" width="18" height="16"/>
<rect class="vx-cell-on" x="601" y="200" width="18" height="16"/>
<line class="vx-line" x1="137" y1="145" x2="70" y2="145"/>
<polygon class="vx-arrowhead" points="54,145 70,138 70,152"/>
<line class="vx-line" x1="623" y1="145" x2="690" y2="145"/>
<polygon class="vx-arrowhead" points="706,145 690,138 690,152"/>
<text class="vx-text-muted" x="380" y="238" text-anchor="middle">8 operations enter on the right every cycle; 8 complete and leave on the left</text>
<text class="vx-text-accent" x="380" y="262" text-anchor="middle">needed parallelism = latency × throughput = 24 × 8 = 192 operations in flight</text>
</svg>
<figcaption>Figure 1. Little's law drawn as a pipe of fixed length. Each of the 192 cells is one operation currently between issue and completion; the grid never shrinks below that count without leaving some of the SM's 8-per-cycle throughput unused. Numbers are Volkov's example for the G80 to GT200 architecture, not a measurement of any specific chip.</figcaption>
</figure>

Little's law says how many operations must be in flight. It says nothing about where they come from. Two hundred cycles' worth of independent work can come from 192 different warps, each contributing one operation, or from one warp issuing 192 independent operations back to back, or anything between. Volkov names the two ends of that range: **thread-level parallelism (TLP)**, more resident warps each contributing independent work, and **instruction-level parallelism (ILP)**, more independent instructions issued by a single thread before it needs a result back.[^volkov-tlp-ilp] Both supply operations the scheduler can issue while something else is still in flight; Little's law does not care which one filled the grid.

<figure class="vx-figure">
<svg viewBox="0 0 760 460" role="img" aria-label="Two ways to reach the same six units of parallelism: six warps with one operation each, or one warp with six independent operations" aria-describedby="g5-f2-desc">
<title id="g5-f2-title">Thread-level and instruction-level parallelism reach the same total</title>
<desc id="g5-f2-desc">Top row, six boxes labelled warp 1 through warp 6, each holding one highlighted operation: six warps, one independent operation each, six operations in flight. Bottom, one box labelled warp 1 holding six stacked highlighted operations: one warp, six independent operations issued back to back, six operations in flight. Both rows total six.</desc>
<text class="vx-text" x="20" y="24">Six units of parallelism, two ways</text>
<text class="vx-text-muted" x="20" y="46">more warps (TLP): one operation per warp</text>
<rect class="vx-box" x="112" y="74" width="74" height="74"/>
<rect class="vx-cell-on" x="136.0" y="98.0" width="26" height="26"/>
<text class="vx-text-muted" x="149.0" y="166" text-anchor="middle">warp 1</text>
<rect class="vx-box" x="202" y="74" width="74" height="74"/>
<rect class="vx-cell-on" x="226.0" y="98.0" width="26" height="26"/>
<text class="vx-text-muted" x="239.0" y="166" text-anchor="middle">warp 2</text>
<rect class="vx-box" x="292" y="74" width="74" height="74"/>
<rect class="vx-cell-on" x="316.0" y="98.0" width="26" height="26"/>
<text class="vx-text-muted" x="329.0" y="166" text-anchor="middle">warp 3</text>
<rect class="vx-box" x="382" y="74" width="74" height="74"/>
<rect class="vx-cell-on" x="406.0" y="98.0" width="26" height="26"/>
<text class="vx-text-muted" x="419.0" y="166" text-anchor="middle">warp 4</text>
<rect class="vx-box" x="472" y="74" width="74" height="74"/>
<rect class="vx-cell-on" x="496.0" y="98.0" width="26" height="26"/>
<text class="vx-text-muted" x="509.0" y="166" text-anchor="middle">warp 5</text>
<rect class="vx-box" x="562" y="74" width="74" height="74"/>
<rect class="vx-cell-on" x="586.0" y="98.0" width="26" height="26"/>
<text class="vx-text-muted" x="599.0" y="166" text-anchor="middle">warp 6</text>
<text class="vx-text-accent" x="650" y="115">6 in flight</text>
<line class="vx-line" x1="20" y1="196" x2="740" y2="196"/>
<text class="vx-text-muted" x="20" y="220">more ILP (fewer warps): six independent operations from one thread</text>
<rect class="vx-box" x="112" y="240" width="74" height="168"/>
<rect class="vx-cell-on" x="124" y="248" width="50" height="22"/>
<rect class="vx-cell-on" x="124" y="274" width="50" height="22"/>
<rect class="vx-cell-on" x="124" y="300" width="50" height="22"/>
<rect class="vx-cell-on" x="124" y="326" width="50" height="22"/>
<rect class="vx-cell-on" x="124" y="352" width="50" height="22"/>
<rect class="vx-cell-on" x="124" y="378" width="50" height="22"/>
<text class="vx-text-muted" x="149.0" y="426" text-anchor="middle">warp 1</text>
<text class="vx-text-accent" x="220" y="330">6 in flight</text>
</svg>
<figcaption>Figure 2. Six warps contributing one independent operation each, and one warp contributing six, both keep six operations in flight. Little's law adds warps times operations per thread; it has no preference between the two factors.</figcaption>
</figure>

The example below computes exactly this sum for a range of (warps, independent operations per thread) pairs, against the 192 target from Volkov's G80 to GT200 example.[^volkov-little]

--8<-- "includes/examples/gpu/g5-occupancy/little_law.cpp.md"

Read the rows in pairs. Two warps at one operation each and one warp at two operations each both put 64 operations in flight and both reach 33 percent of the target; four warps at one and two warps at two both reach 128, or 66 percent. Three of the eight rows reach the full 192: six warps with no extra ILP, three warps with two independent operations each, and a single warp issuing six. None of those three is more "occupied" than the others in any way Little's law measures; only the first uses six resident warps to do it.

??? check "Using the 192-operation target, how many warps at ILP 1 reach it, and how many at ILP 3?"

    192 ÷ 32 = 6 warps at ILP 1. At ILP 3, each warp supplies 32 × 3 = 96 operations, so 192 ÷ 96 = 2 warps. Both appear in the table above.

## When lower occupancy wins

Put the two ideas together and Volkov's CUBLAS and CUFFT numbers stop looking strange. A kernel with 512 threads per block and one accumulator per thread supplies its needed parallelism almost entirely through TLP: many warps, one independent operation apiece. A kernel with 64 threads per block and several accumulators per thread, the register-tiled style [G10](g10-matmul-ladder.md) builds toward, supplies the same total a different way: fewer warps, each contributing more independent work. The second kernel's occupancy calculator, run the way [example 1](#occupancy-how-much-of-the-switching-room-is-used) above ran it, would report a lower percentage, because it counts resident warps, not operations in flight. Occupancy was never the quantity that mattered; it was a proxy for parallelism that stops being accurate once a thread starts supplying more than one operation at a time.

Volkov makes the same point about hardware that structurally forces this choice. On the GF104 GPU he measured, each SM has 48 arithmetic cores fed by only two warp schedulers, so reaching the chip's full instruction rate needs three instructions issued per cycle from a scheduler that can only issue two; the missing instruction has to come from ILP within an already-issued warp, not from adding a third warp.[^volkov-gf104] Some hardware, in other words, cannot reach its own peak through occupancy alone, no matter how many warps a kernel keeps resident.

??? check "Volkov's CUFFT comparison shows occupancy fall from 33 to 17 percent while performance rises. Does dropping occupancy always help?"

    No. It helped there because the smaller thread blocks freed registers that the kernel spent on more independent work per thread, supplying the needed parallelism through ILP instead of TLP. Dropping occupancy without adding anything to replace it leaves cycles with nothing ready to issue, the outcome Little's law predicts when the total falls below the target.

## Apple GPUs: a different occupancy story

Apple's Metal Shading Language Specification does not publish a warps-per-core ceiling the way NVIDIA's guides do, so a percentage-of-maximum occupancy is not the number Apple's own tools report. In a WWDC22 session on scaling compute workloads across Apple GPUs, Apple states the target directly in threads: "1K to 2K concurrent threads per shader core is considered a very good occupancy" for a kernel of ordinary complexity.[^ap9] The same session describes a "theoretical occupancy" statistic that Xcode's compiler analysis added in Xcode 14, computed from a kernel's register and threadgroup-memory use the same way [example 1](#occupancy-how-much-of-the-switching-room-is-used) does, without needing the reader to know the underlying per-core limits.[^ap9]

The budgets behind that number, the SIMD-group width, the register file, threadgroup memory, are the same three kinds of resource this chapter has been dividing up; only the units Apple chooses to report them in differ. A Vortex compiler targeting Metal would need its own measured ceilings for those budgets, the same way [G4](g4-memory-performance.md#thirty-two-addresses-one-instruction) measured a SIMD-group width of 32 on the owner's M4 Pro rather than assuming NVIDIA's numbers applied.

## What does not change: the bits

Occupancy and thread mapping are scheduling decisions: how many copies of the same arithmetic run at once, and when the hardware switches between them. Neither changes what the arithmetic computes. A Vortex compiler that lowers occupancy to make room for more independent accumulators per thread is still evaluating the same expression the source program wrote, one IEEE 754 operation at a time, in the order [decision 56](../decisions/numbers.md#d56) requires; it has only changed how many threads are doing it and how much of each one's work is independent at once. And because every output element the naive kernel writes belongs to exactly one `(row, column)` pair, changing how many threads or blocks are resident never creates two threads racing to write the same element of the `&mut` output; occupancy is a property of the schedule, not of the values it produces.

## Measuring it

Occupancy is something a profiler reports, not something to guess. On NVIDIA GPUs, Nsight Compute reports achieved occupancy alongside the theoretical maximum this chapter's calculator computes, so a kernel can be checked against both its ceiling and what it reached. On Apple GPUs, Xcode's compiler statistics report the theoretical occupancy described above once a kernel is compiled.[^ap9] Neither tool ships with this repository, so record what one of them says about a real kernel here, rather than trusting the simplified model:

| kernel | threads/block | registers/thread (compiler-reported) | shared memory/block | theoretical occupancy | achieved occupancy |
| --- | --- | --- | --- | --- | --- |
| | | | | | |
| | | | | | |

Record the GPU, its driver or OS version, the compiler and the date with the table.

## For Vortex

!!! vortex "Exercise"

    **Build** an occupancy and parallelism report that extends the target description from [G4's exercise](g4-memory-performance.md#for-vortex): given a kernel's launch shape and its estimated resource use, report both the occupancy this chapter defines and the operations-in-flight total Little's law asks for.

    1. Four new fields on the target description: resident threads, warps and blocks per SM, and the SM's register and shared-memory totals. Fill them for NVIDIA from the sources cited here, and mark the Apple entries you have not measured on the M4 Pro as unknown, the same convention G4 used.
    2. Given a kernel's threads per block and its registers-per-thread and shared-memory-per-block figures, an occupancy function that reports the resident block count, which of the four budgets set it, and the resulting occupancy, following the model in this chapter's first example.
    3. Given a target's latency and throughput for one instruction class (numbers the reader supplies or measures, per [P1](../optimize/p1-measure-first.md); leave them unset rather than inventing them), the needed-parallelism figure from Little's law, and whether a proposed (warps, independent operations per thread) pair reaches it.
    4. A remark for every report, as the [sixth principle](../philosophy.md#6-explain-performance-decisions) asks, distinguishing a kernel limited by occupancy from one that reaches full parallelism at low occupancy through ILP.

    **Not yet:** estimating a real kernel's register and shared-memory use from Vortex's own IR (that is back-end work, [C3](../backend/c3-linear-scan.md) onward, extended to a GPU target); generating GPU code at all ([M12](../mlir/m12-vortex-gpu-path.md) weighs the paths); measuring real latency and throughput numbers for the reader's own target ([G14](g14-measuring-gpu-code.md)); anything about barriers or reductions ([G6](g6-synchronization.md)).

    **Proof that it works:**

    - Golden tests reproducing this chapter's occupancy table for the Hopper numbers given here: all six rows, including which of the four budgets each one reports as the limit.
    - Golden tests reproducing the little_law example's eight rows for the 192-operation target, including the three that reach it.
    - A pair test: for any two (warps, ilp) configurations with equal products, the report's operations-in-flight figure and its verdict against a needed-parallelism target agree.
    - A differential test: for a few hundred random SM budgets and kernel shapes, the occupancy function's resident-block count matches an independent brute-force count like this chapter's example.

## Key ideas

!!! recap "Questions you can now answer"

    - **What is occupancy?** The number of warps resident on an SM divided by the largest number the SM supports.
    - **What three budgets can cap it?** Registers per thread, shared memory per block, and the SM's own limits on resident threads and blocks; the smallest of the four decides.
    - **Why can maximizing occupancy reduce performance?** It cannot by itself; but a kernel restructured to raise occupancy sometimes gives up the independent per-thread work (ILP) that was supplying its parallelism a different way, and the restructuring, not the occupancy number, is what costs performance.
    - **What does Little's law say?** The parallelism needed to reach full throughput equals latency multiplied by throughput; that parallelism can come from more warps, more independent operations per thread, or any mix of the two.
    - **Why does none of this change a Vortex program's result?** Occupancy and thread mapping decide when and how many copies of the same arithmetic run, never the order of the floating-point operations within one thread, and `&mut` rules out two threads writing the same output element.

## Where this comes back

!!! next "You will use this again in"

    - [G6. Synchronization, atomics and reductions](g6-synchronization.md): *resident warps*, *warp scheduler*
    - [G10. The GPU matmul ladder](g10-matmul-ladder.md): *register tiling*, *occupancy*, *instruction-level parallelism*
    - [G12. Fusion case study: FlashAttention](g12-flashattention.md): *register pressure*, *occupancy*
    - [G14. Measuring GPU code](g14-measuring-gpu-code.md): *theoretical occupancy*, *achieved occupancy*
    - [M12. Designing Vortex's GPU path](../mlir/m12-vortex-gpu-path.md): *target description*, *occupancy*

## Sources and further reading

Read Volkov's talk first: it makes this chapter's whole argument in fourteen slides. Then the Best Practices Guide's occupancy section for the mechanics, and the Hopper Tuning Guide for one architecture's numbers.

[^pg-table30]: NVIDIA, "CUDA Programming Guide", v13.4.2, section 5.1.3, "Features and Technical Specifications", Table 30, the column for compute capability 9.0: 2,048 resident threads, 64 resident warps and 32 resident blocks per SM. <https://docs.nvidia.com/cuda/cuda-programming-guide/05-appendices/compute-capabilities.html#features-and-technical-specifications>
[^bp-occ]: NVIDIA, "CUDA C++ Best Practices Guide", 13.4, section 11.1, "Occupancy": the ratio of active warps to the maximum warps a multiprocessor supports. <https://docs.nvidia.com/cuda/cuda-c-best-practices-guide/index.html#occupancy>
[^bp-calc]: NVIDIA, "CUDA C++ Best Practices Guide", 13.4, section 11.1.1, "Calculating Occupancy". <https://docs.nvidia.com/cuda/cuda-c-best-practices-guide/index.html#calculating-occupancy>
[^hopper-occ]: NVIDIA, "Hopper Tuning Guide", section 1.4.1.1, "Occupancy": 65,536 32-bit registers and 228 KiB of shared memory per SM on H100 (compute capability 9.0). <https://docs.nvidia.com/cuda/hopper-tuning-guide/index.html#occupancy>
[^volkov-prologue]: Vasily Volkov, "Better Performance at Lower Occupancy", GTC 2010, 22 September 2010, slide 2, "Prologue". <https://www.nvidia.com/content/GTC-2010/pdfs/2238_GTC2010.pdf>
[^volkov-table]: Vasily Volkov, "Better Performance at Lower Occupancy", GTC 2010, slide 3, "Faster codes run at lower occupancy": the CUBLAS 1.1/2.0 and CUFFT 2.2/2.3 measurements on a G80. <https://www.nvidia.com/content/GTC-2010/pdfs/2238_GTC2010.pdf>
[^volkov-fallacies]: Vasily Volkov, "Better Performance at Lower Occupancy", GTC 2010, slide 4, "Two common fallacies". <https://www.nvidia.com/content/GTC-2010/pdfs/2238_GTC2010.pdf>
[^volkov-latency]: Vasily Volkov, "Better Performance at Lower Occupancy", GTC 2010, slide 7, "Arithmetic latency": about 20 cycles for arithmetic, 400 or more for memory. <https://www.nvidia.com/content/GTC-2010/pdfs/2238_GTC2010.pdf>
[^volkov-throughput]: Vasily Volkov, "Better Performance at Lower Occupancy", GTC 2010, slide 8, "Arithmetic throughput": about 480 arithmetic operations per cycle and 32 memory operations per cycle on the GPU measured. <https://www.nvidia.com/content/GTC-2010/pdfs/2238_GTC2010.pdf>
[^volkov-little]: Vasily Volkov, "Better Performance at Lower Occupancy", GTC 2010, slides 10 and 11, "Use Little's law" and "Arithmetic parallelism in numbers": needed parallelism = latency × throughput, with the G80-GT200 row (about 24 cycles, 8 cores/SM, about 192 operations). <https://www.nvidia.com/content/GTC-2010/pdfs/2238_GTC2010.pdf>
[^volkov-tlp-ilp]: Vasily Volkov, "Better Performance at Lower Occupancy", GTC 2010, slides 12 to 14, "Thread-level parallelism (TLP)", "Instruction-level parallelism (ILP)" and "You can use both ILP and TLP on GPU". <https://www.nvidia.com/content/GTC-2010/pdfs/2238_GTC2010.pdf>
[^volkov-gf104]: Vasily Volkov, "Better Performance at Lower Occupancy", GTC 2010, slide 14: on GF104, 48 cores per SM and one instruction broadcast across 16 cores requires 3 instructions issued per cycle, but the SM has only 2 warp schedulers, so ILP is required to exceed 66 percent of peak. <https://www.nvidia.com/content/GTC-2010/pdfs/2238_GTC2010.pdf>
[^ap9]: Apple, WWDC22 session 10159, "Scale compute workloads across Apple GPUs": "1K to 2K concurrent threads per shader core is considered a very good occupancy" for relatively complex kernels, and Xcode 14's compiler statistics report a "theoretical occupancy". <https://developer.apple.com/videos/play/wwdc2022/10159/>

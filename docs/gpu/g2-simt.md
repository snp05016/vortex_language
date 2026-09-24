# G2. The SIMT execution model

<p class="page-intro">A GPU does not run one thread at a time, and it does not run all of them freely in parallel either: it issues one instruction to a fixed-size group of threads at once. This chapter names that group, works out what it costs when the group's threads disagree about what to do next, and connects the choice of group size to a Vortex compiler's own decisions.</p>

<p class="vx-meta" markdown="1">Level: Foundations · Reading time: about 30 minutes · Builds on: [G1. Throughput machines](g1-throughput-machines.md)</p>

???+ remember "Before you start, remember"

    ??? question "What does a CPU's loop vectorizer do to a loop's iterations?"

        It widens several of them into one instruction: instead of one scalar `add` per iteration, one wide instruction does four, eight or however many the target's vector width allows, one iteration per lane, all independent of each other.

        Introduced in [P10. Vectorization](../optimize/p10-vectorization.md).

    ??? question "What makes a loop's iterations safe to hand to separate threads with no synchronization between them?"

        Each iteration writes to a location no other iteration writes to, and no iteration depends on a result an earlier one produced.

        Introduced in [P13. Multithreading](../optimize/p13-multithreading.md).

    ??? question "Why does a throughput-oriented machine try to keep many independent operations in flight, rather than finishing one as fast as possible?"

        Because the time any single operation spends waiting on a slow resource, such as memory, is hidden as long as other, independent operations are ready to run during the wait. Little's law ties a system's throughput to how much work is in flight divided by how long each piece of work takes.

        Introduced in [G1. Throughput machines](g1-throughput-machines.md).

    ??? question "Is the extent of a Vortex array such as `[f32; 4096]` a value the compiler knows while it compiles, or only a value the running program computes?"

        The compiler knows it. An array's shape is part of its type, fixed at compile time and identical at every call site.

        Introduced in [Arrays and shapes, decision 43](../decisions/arrays.md#d43).

!!! goals "In this chapter"

    - Explain why GPU hardware runs threads in a fixed-size group instead of one at a time, and name that group in NVIDIA's, AMD's and Apple's own words.
    - Read a kernel's block and thread indices as ordinary arithmetic on a flat thread id.
    - Predict how many serialized passes a warp needs when its lanes disagree about which branch to take, and recognize a warp-uniform value that avoids the cost entirely.
    - State what independent thread scheduling changed since Volta, and what it left unchanged about the cost of divergence.
    - Decide, for one loop nest over a fixed-shape Vortex array, whether a block size exists that gives every warp full, undivided work.

## One thread, one loop iteration

Start with a loop a CPU would run in order, one element after another:

```vortex
// items: valid
fn increment(x: &mut [f32; 4096]) {
    for i in 0..4096 {
        x[i] = x[i] + 1.0;
    }
}
```

On a CPU, this either runs 4,096 times through the body, or, once [P10](../optimize/p10-vectorization.md)'s vectorizer gets to it, four or eight times at once in one wide instruction. A GPU takes a third route: it creates one **thread** for every value of `i` and runs all 4,096 of them, in principle, together. A GPU thread is not an operating-system thread; it is the smallest unit of work the hardware schedules, closer to one iteration of a wide, parallel loop, written as if it owned the whole loop body to itself. Thread number 517 does exactly the work of `i = 517`: it reads `x[517]`, adds `1.0`, and writes `x[517]` back, and it never touches any other element.

Launching a kernel means telling the GPU two numbers: how many **blocks** to create, and how many threads each block holds. NVIDIA's programming guide calls the whole collection of blocks the **grid**.[^thread-hierarchy] Every thread receives two built-in indices, one for the block it belongs to and one for its own position inside that block, and the kernel computes whatever flat index it needs from them; for a one-dimensional grid, `i = block_id * threads_per_block + thread_id`.[^thread-hierarchy] For a multi-dimensional block, threads are numbered so the first coordinate moves fastest, the same convention [G4](g4-memory-performance.md) leans on when it says a warp is 32 threads with consecutive `x`.

## Warps, wavefronts and SIMD-groups

The GPU does not, in fact, run every thread of a block on its own. It gathers threads into a fixed-size group and issues one instruction to the whole group at once. NVIDIA groups every 32 consecutive threads of a block, by the fastest-varying index, into a **warp**.[^basics-simt] AMD's hardware documentation describes the same grouping as a **wavefront**: 64 threads wide on its CDNA data-center parts, 32 wide on its RDNA client parts.[^hip-cu] Apple calls it a **SIMD-group**, and a WWDC session reports that every Apple GPU's SIMD-group is 32 threads wide, a number a Metal program can read back from its compute pipeline as `threadExecutionWidth`.[^apple-simd] This book uses **warp** as the general word from here on, and **lane** for one thread's position inside it, reusing the word [P10](../optimize/p10-vectorization.md) gave a position inside a CPU vector register: the two ideas share a name because they share a shape, one instruction acting on several independent slots at once.

A block is not free to be any size. The hardware still slices it into warps of the vendor's fixed width, and a block whose thread count does not divide evenly leaves its last warp partly idle. Suppose a kernel launches 2 blocks of 40 threads each, on hardware with 32-thread warps, a size chosen badly on purpose to show what happens at the edge. Each block needs `ceil(40 / 32) = 2` warps: one full warp of 32 active lanes, and one warp that only has 8 real threads to give it, so 24 of its 32 lane slots run with nothing to do.

--8<-- "includes/examples/gpu/g2-simt/thread_hierarchy.cpp.md"

<figure class="vx-figure">
<svg viewBox="0 0 760 330" role="img" aria-label="A grid of two blocks of 40 threads each, split into warps of 32 lanes. Each block has one full warp of 32 active lanes and one partial warp with only 8 active lanes; the remaining 24 lane slots of that warp are drawn empty, because the block has no thread left to put there.">
<title id="g2-f1-title">A grid of two blocks of 40 threads, split into warps of 32 lanes</title>
<desc id="g2-f1-desc">Two groups of two rows of small squares. Each row of 32 squares is one warp. In block 0, the first row (warp 0) has all 32 squares filled, meaning all 32 lanes are active; the second row (warp 1) has its first 8 squares filled and the remaining 24 drawn as empty outlines, meaning only 8 of its lanes are active and 24 are idle. Block 1 repeats the same pattern.</desc>
<text class="vx-text" x="20" y="26">One grid, two blocks of 40 threads each; a warp is 32 lanes</text>
<text class="vx-text" x="20" y="54">block 0 (40 threads)</text>
<rect class="vx-cell-on" x="148" y="76" width="16" height="24"/>
<rect class="vx-cell-on" x="166" y="76" width="16" height="24"/>
<rect class="vx-cell-on" x="184" y="76" width="16" height="24"/>
<rect class="vx-cell-on" x="202" y="76" width="16" height="24"/>
<rect class="vx-cell-on" x="220" y="76" width="16" height="24"/>
<rect class="vx-cell-on" x="238" y="76" width="16" height="24"/>
<rect class="vx-cell-on" x="256" y="76" width="16" height="24"/>
<rect class="vx-cell-on" x="274" y="76" width="16" height="24"/>
<rect class="vx-cell-on" x="292" y="76" width="16" height="24"/>
<rect class="vx-cell-on" x="310" y="76" width="16" height="24"/>
<rect class="vx-cell-on" x="328" y="76" width="16" height="24"/>
<rect class="vx-cell-on" x="346" y="76" width="16" height="24"/>
<rect class="vx-cell-on" x="364" y="76" width="16" height="24"/>
<rect class="vx-cell-on" x="382" y="76" width="16" height="24"/>
<rect class="vx-cell-on" x="400" y="76" width="16" height="24"/>
<rect class="vx-cell-on" x="418" y="76" width="16" height="24"/>
<rect class="vx-cell-on" x="436" y="76" width="16" height="24"/>
<rect class="vx-cell-on" x="454" y="76" width="16" height="24"/>
<rect class="vx-cell-on" x="472" y="76" width="16" height="24"/>
<rect class="vx-cell-on" x="490" y="76" width="16" height="24"/>
<rect class="vx-cell-on" x="508" y="76" width="16" height="24"/>
<rect class="vx-cell-on" x="526" y="76" width="16" height="24"/>
<rect class="vx-cell-on" x="544" y="76" width="16" height="24"/>
<rect class="vx-cell-on" x="562" y="76" width="16" height="24"/>
<rect class="vx-cell-on" x="580" y="76" width="16" height="24"/>
<rect class="vx-cell-on" x="598" y="76" width="16" height="24"/>
<rect class="vx-cell-on" x="616" y="76" width="16" height="24"/>
<rect class="vx-cell-on" x="634" y="76" width="16" height="24"/>
<rect class="vx-cell-on" x="652" y="76" width="16" height="24"/>
<rect class="vx-cell-on" x="670" y="76" width="16" height="24"/>
<rect class="vx-cell-on" x="688" y="76" width="16" height="24"/>
<rect class="vx-cell-on" x="706" y="76" width="16" height="24"/>
<text class="vx-text-muted" x="20" y="93">warp 0</text>
<rect class="vx-cell-on" x="148" y="112" width="16" height="24"/>
<rect class="vx-cell-on" x="166" y="112" width="16" height="24"/>
<rect class="vx-cell-on" x="184" y="112" width="16" height="24"/>
<rect class="vx-cell-on" x="202" y="112" width="16" height="24"/>
<rect class="vx-cell-on" x="220" y="112" width="16" height="24"/>
<rect class="vx-cell-on" x="238" y="112" width="16" height="24"/>
<rect class="vx-cell-on" x="256" y="112" width="16" height="24"/>
<rect class="vx-cell-on" x="274" y="112" width="16" height="24"/>
<rect class="vx-box" x="292" y="112" width="16" height="24"/>
<rect class="vx-box" x="310" y="112" width="16" height="24"/>
<rect class="vx-box" x="328" y="112" width="16" height="24"/>
<rect class="vx-box" x="346" y="112" width="16" height="24"/>
<rect class="vx-box" x="364" y="112" width="16" height="24"/>
<rect class="vx-box" x="382" y="112" width="16" height="24"/>
<rect class="vx-box" x="400" y="112" width="16" height="24"/>
<rect class="vx-box" x="418" y="112" width="16" height="24"/>
<rect class="vx-box" x="436" y="112" width="16" height="24"/>
<rect class="vx-box" x="454" y="112" width="16" height="24"/>
<rect class="vx-box" x="472" y="112" width="16" height="24"/>
<rect class="vx-box" x="490" y="112" width="16" height="24"/>
<rect class="vx-box" x="508" y="112" width="16" height="24"/>
<rect class="vx-box" x="526" y="112" width="16" height="24"/>
<rect class="vx-box" x="544" y="112" width="16" height="24"/>
<rect class="vx-box" x="562" y="112" width="16" height="24"/>
<rect class="vx-box" x="580" y="112" width="16" height="24"/>
<rect class="vx-box" x="598" y="112" width="16" height="24"/>
<rect class="vx-box" x="616" y="112" width="16" height="24"/>
<rect class="vx-box" x="634" y="112" width="16" height="24"/>
<rect class="vx-box" x="652" y="112" width="16" height="24"/>
<rect class="vx-box" x="670" y="112" width="16" height="24"/>
<rect class="vx-box" x="688" y="112" width="16" height="24"/>
<rect class="vx-box" x="706" y="112" width="16" height="24"/>
<text class="vx-text-muted" x="20" y="129">warp 1</text>
<text class="vx-text-muted" x="148" y="152">warp 0: 32 of 32 lanes active, warp 1: 8 of 32 active, 24 idle</text>
<text class="vx-text" x="20" y="182">block 1 (40 threads)</text>
<rect class="vx-cell-on" x="148" y="196" width="16" height="24"/>
<rect class="vx-cell-on" x="166" y="196" width="16" height="24"/>
<rect class="vx-cell-on" x="184" y="196" width="16" height="24"/>
<rect class="vx-cell-on" x="202" y="196" width="16" height="24"/>
<rect class="vx-cell-on" x="220" y="196" width="16" height="24"/>
<rect class="vx-cell-on" x="238" y="196" width="16" height="24"/>
<rect class="vx-cell-on" x="256" y="196" width="16" height="24"/>
<rect class="vx-cell-on" x="274" y="196" width="16" height="24"/>
<rect class="vx-cell-on" x="292" y="196" width="16" height="24"/>
<rect class="vx-cell-on" x="310" y="196" width="16" height="24"/>
<rect class="vx-cell-on" x="328" y="196" width="16" height="24"/>
<rect class="vx-cell-on" x="346" y="196" width="16" height="24"/>
<rect class="vx-cell-on" x="364" y="196" width="16" height="24"/>
<rect class="vx-cell-on" x="382" y="196" width="16" height="24"/>
<rect class="vx-cell-on" x="400" y="196" width="16" height="24"/>
<rect class="vx-cell-on" x="418" y="196" width="16" height="24"/>
<rect class="vx-cell-on" x="436" y="196" width="16" height="24"/>
<rect class="vx-cell-on" x="454" y="196" width="16" height="24"/>
<rect class="vx-cell-on" x="472" y="196" width="16" height="24"/>
<rect class="vx-cell-on" x="490" y="196" width="16" height="24"/>
<rect class="vx-cell-on" x="508" y="196" width="16" height="24"/>
<rect class="vx-cell-on" x="526" y="196" width="16" height="24"/>
<rect class="vx-cell-on" x="544" y="196" width="16" height="24"/>
<rect class="vx-cell-on" x="562" y="196" width="16" height="24"/>
<rect class="vx-cell-on" x="580" y="196" width="16" height="24"/>
<rect class="vx-cell-on" x="598" y="196" width="16" height="24"/>
<rect class="vx-cell-on" x="616" y="196" width="16" height="24"/>
<rect class="vx-cell-on" x="634" y="196" width="16" height="24"/>
<rect class="vx-cell-on" x="652" y="196" width="16" height="24"/>
<rect class="vx-cell-on" x="670" y="196" width="16" height="24"/>
<rect class="vx-cell-on" x="688" y="196" width="16" height="24"/>
<rect class="vx-cell-on" x="706" y="196" width="16" height="24"/>
<text class="vx-text-muted" x="20" y="213">warp 0</text>
<rect class="vx-cell-on" x="148" y="232" width="16" height="24"/>
<rect class="vx-cell-on" x="166" y="232" width="16" height="24"/>
<rect class="vx-cell-on" x="184" y="232" width="16" height="24"/>
<rect class="vx-cell-on" x="202" y="232" width="16" height="24"/>
<rect class="vx-cell-on" x="220" y="232" width="16" height="24"/>
<rect class="vx-cell-on" x="238" y="232" width="16" height="24"/>
<rect class="vx-cell-on" x="256" y="232" width="16" height="24"/>
<rect class="vx-cell-on" x="274" y="232" width="16" height="24"/>
<rect class="vx-box" x="292" y="232" width="16" height="24"/>
<rect class="vx-box" x="310" y="232" width="16" height="24"/>
<rect class="vx-box" x="328" y="232" width="16" height="24"/>
<rect class="vx-box" x="346" y="232" width="16" height="24"/>
<rect class="vx-box" x="364" y="232" width="16" height="24"/>
<rect class="vx-box" x="382" y="232" width="16" height="24"/>
<rect class="vx-box" x="400" y="232" width="16" height="24"/>
<rect class="vx-box" x="418" y="232" width="16" height="24"/>
<rect class="vx-box" x="436" y="232" width="16" height="24"/>
<rect class="vx-box" x="454" y="232" width="16" height="24"/>
<rect class="vx-box" x="472" y="232" width="16" height="24"/>
<rect class="vx-box" x="490" y="232" width="16" height="24"/>
<rect class="vx-box" x="508" y="232" width="16" height="24"/>
<rect class="vx-box" x="526" y="232" width="16" height="24"/>
<rect class="vx-box" x="544" y="232" width="16" height="24"/>
<rect class="vx-box" x="562" y="232" width="16" height="24"/>
<rect class="vx-box" x="580" y="232" width="16" height="24"/>
<rect class="vx-box" x="598" y="232" width="16" height="24"/>
<rect class="vx-box" x="616" y="232" width="16" height="24"/>
<rect class="vx-box" x="634" y="232" width="16" height="24"/>
<rect class="vx-box" x="652" y="232" width="16" height="24"/>
<rect class="vx-box" x="670" y="232" width="16" height="24"/>
<rect class="vx-box" x="688" y="232" width="16" height="24"/>
<rect class="vx-box" x="706" y="232" width="16" height="24"/>
<text class="vx-text-muted" x="20" y="249">warp 1</text>
<text class="vx-text-muted" x="148" y="272">warp 0: 32 of 32 lanes active, warp 1: 8 of 32 active, 24 idle</text>
<rect class="vx-cell-on" x="20" y="298" width="16" height="16"/>
<text class="vx-text-muted" x="44" y="311">active lane</text>
<rect class="vx-box" x="170" y="298" width="16" height="16"/>
<text class="vx-text-muted" x="194" y="311">lane slot present, no thread to fill it</text>
</svg>
<figcaption>Figure 1. A grid of two blocks of 40 threads, split into warps of 32 lanes. Each block's second warp has only 8 real threads; its other 24 lane slots still exist in hardware but do no work.</figcaption>
</figure>

## SIMT: one instruction, many threads

NVIDIA's name for this architecture is **SIMT**, single instruction, multiple threads, and its guide is explicit that the model lets each thread keep its own state and its own control flow, while warning that performance suffers when a warp's threads take different paths.[^basics-simt] That is a real difference from a CPU's SIMD lane. A SIMD lane, from [P10](../optimize/p10-vectorization.md), is a slot inside one wide register that a single vector instruction always treats identically to its neighbours: `fadd.4s`'s lane 2 cannot take a branch that lane 0 skips, because there is only one instruction, and it has no branches to take. A SIMT thread is a stronger fiction: the source language lets it write an ordinary `if`, loop or function call as though it ran alone, with its own registers and its own place in the program. The hardware then does the work of making 32 of these separately written threads share one instruction stream whenever it can, which is most of the time, because most kernels send every thread down the same path.

A block's threads all run on one **streaming multiprocessor** (NVIDIA's name), **compute unit** (AMD's) or **GPU core** (Apple's): the physical unit that owns the registers, the warp schedulers, and the on-chip memory a block's threads can share. [G3](g3-memory-hierarchy.md) opens that unit up; this chapter only needs to know that a block's warps live there together, competing for the same resources.

??? check "A kernel's block has 96 threads, and the hardware's warp size is 32. How many warps does the block need, and is any of them partial?"

    Exactly 3, and none of them is partial: `96 / 32 = 3` with no remainder, so every warp gets a full 32 real threads. Partial warps come from a block size that does not divide evenly by the warp size, not from having more than one warp.

## Divergence: when a warp's lanes disagree

Consider a kernel whose body is, in effect, `if lane is even: p(); else: q();`. SIMT lets every lane evaluate that condition on its own, so half the warp wants to run `p` and half wants `q`. The hardware cannot do both at once: it makes two passes over the warp, one running `p` with the odd lanes masked off, one running `q` with the even lanes masked off. This chapter's counting rule is a model of that cost, not a cycle count: a warp needs one pass for every distinct choice its active lanes make. All 32 lanes agreeing costs one pass. A two-way split costs two passes, whichever lanes are in which group. A per-lane choice, such as a switch keyed on the lane's own index, can cost as many as 32.

A value that every lane of a warp computes identically, such as one that depends only on the block, is **warp-uniform**. Branching on a warp-uniform value costs nothing extra: every lane in the warp takes the same pass, because there is only one distinct choice to make.

--8<-- "includes/examples/gpu/g2-simt/divergence_cost.cpp.md"

The rule counts distinct choices, not how the lanes making them are arranged: a warp split down the middle and a warp split by parity both cost two passes here, because both produce exactly two distinct values among 32 lanes. Real hardware can still prefer one arrangement over the other for reasons this model does not capture, such as one arm being shorter than the other; the passes themselves are what this chapter tracks, and independent thread scheduling, covered next, changes how flexibly the hardware interleaves them, not how many exist.

??? check "A warp's 32 lanes each evaluate `switch (lane_id % 4)`. How many passes does the warp need? If every lane instead evaluated `switch (block_id % 4)`, using the one `block_id` value shared by the whole block, how many passes would it need, and why?"

    4 passes for `lane_id % 4`: the 32 lanes split into 4 groups of 8. 1 pass for `block_id % 4`: every lane of the warp is part of the same block, so `block_id` is warp-uniform, and the switch produces only one distinct value across the whole warp.

A compiler that lowers to MLIR's `gpu` dialect works with this same idea as ordinary IR, not as hardware magic. The dialect gives a lowering pass `gpu.thread_id` and `gpu.block_id` operations for exactly the indices this chapter has been computing by hand,[^mlir-thread-id] inside a `gpu.func` marked `kernel` and launched, from a module carrying the `gpu.container_module` attribute, through `gpu.launch_func`.[^mlir-func] The file below writes one of two tags per thread, depending on which half of its warp a thread's index falls in: `mlir-opt` only parses, verifies and prints it, since nothing in this book's toolchain runs a GPU kernel yet, but the branch it contains, `scf.if` with both arms explicit, is exactly the structured shape a front end must produce for an `if`/`else` like the one this section has been describing.

--8<-- "includes/examples/gpu/g2-simt/thread_branch.mlir.md"

How a compiler decides that a branch is warp-uniform, and how it turns an arbitrary control-flow graph into the properly nested, structured branches MLIR's `scf` and `gpu` dialects expect, is [G9](g9-gpu-compilers-in-llvm.md)'s subject.

## Independent thread scheduling

Everything so far describes a warp as if it had one shared program counter: the whole warp is either running the `if` arm or the `else` arm together, and nothing begins the next arm until every active lane of the current one has finished. That was literally true of NVIDIA hardware before compute capability 7.0, the Volta architecture: one program counter and one active mask per warp, shared by all 32 threads. Starting with Volta, each thread keeps its own program counter and call stack, a change the guide calls **independent thread scheduling**: the hardware can now interleave a warp's diverged paths more freely, and reconverge them at points other than the end of the branching construct.[^thread-scheduling]

This does not remove the passes this chapter has been counting. Two distinct paths through a warp are still two bodies of work that cannot both occupy the warp's one instruction slot in the same cycle; independent thread scheduling changes when and how flexibly the hardware interleaves that work, not how much of it there is. What it does change is a correctness assumption older kernels sometimes relied on: code that assumed the whole warp reached some point together, with no explicit synchronization instruction, can now break, because a thread's neighbours may be executing an entirely different point of the program at that moment. The guide's answer is to make the assumption explicit, with a warp-level synchronization instruction, rather than to depend on implicit lockstep.[^thread-scheduling]

## Bringing this back to Vortex: fixed shapes and the boundary guard

[G4](g4-memory-performance.md) already put one thread on each `(row, column)` pair of the multiply kernel this book returns to throughout the GPU chapters:

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

Turning the two outer loops into a grid of threads is the first decision a GPU compiler makes for this kernel, before it ever reaches which index a warp should vary ([G4](g4-memory-performance.md)'s subject) or how many threads should share a block ([G5](g5-occupancy.md)'s subject). This chapter's part of that decision is the one the block size controls directly: whether the mapping ever forces a lane to sit idle, or a warp to spend an extra pass finding out it has nothing to do.

A generic GPU kernel usually cannot assume the array it processes has any particular size: the host passes a size as an ordinary run-time value, so a kernel's first line is often a guard, `if (idx < n) { ...do the work... }`, protecting against the extra threads a rounded-up block count creates. That guard is itself a branch, and for every block except possibly the last, every one of its lanes agrees on the answer. Only the boundary block, when `n` is not a multiple of the block size, has a warp with lanes on both sides of the guard, splitting it into two passes: one that does the real work, and one, doing nothing, for the lanes that ran past the end.

Vortex does not need to take that guard on faith. [Decision 43](../decisions/arrays.md#d43) fixes an array's shape as part of its type, a compile-time constant identical at every call site, and the [performance philosophy](../philosophy.md#performance-philosophy) already lists this exact decision, assigning each worker its share of the computation's indexes, among the GPU optimizations Vortex should eventually support. A compiler that knows, at compile time, that `multiply`'s `row` and `column` both range over exactly 64 values can choose a block size that divides 64 with no remainder in both directions, for example 32 lanes along `row` by 2 along `column`, and then it never needs the guard at all: every launched thread corresponds to a real element of `c`, every warp of every block is fully active, and the divergence this chapter describes does not arise, for this kernel, on this shape. The reasoning runs out for a shape whose extent does not divide the warp size, and working out what changes is this chapter's exercise.

??? check "A kernel runs one thread per element of a 70-element array, launched as 3 blocks of 32 threads each (so each block is exactly one warp), with the guard `idx < 70` at the top of every thread. Block 0 covers indices 0-31, block 1 covers 32-63, block 2 covers 64-95. How many passes does each block's warp need for the guard?"

    Blocks 0 and 1: 1 pass each. Every lane's index is below 70, so the guard is warp-uniform (true for all 32 lanes) even though the array's extent is not a multiple of 32. Block 2: 2 passes. Its lanes cover indices 64-95; the 6 lanes with `idx` 64 to 69 find the guard true, and the 26 lanes with `idx` 70 to 95 find it false, two distinct values among the warp's active lanes.

## For Vortex

!!! vortex "Exercise"

    Vortex's v0.1 compiler targets CPUs only ([stage 10](../compiler/guide/stage-10-matrix-multiplication.md)); there is no GPU back end to write code for yet. Treat this as a design note for the day one exists, of the kind the [philosophy](../philosophy.md#6-explain-performance-decisions) says a Vortex compiler should be able to give the programmer.

    Build: for `multiply`'s `row` and `column` loops, on a hypothetical target whose warp size is 32, work out a block size, however you split it between the two directions, for which every warp of every launched block is fully active, with no lane outside the array's `[row, column]` range. Show the arithmetic that proves your chosen block size divides 64 with no remainder in both directions, the way this chapter checked 40 against a warp of 32.

    Then redo the reasoning for a `[f32; 70, 70]` array. Decide whether any block size avoids the boundary guard entirely for that shape. If none does, say which blocks would need the guard, and, using this chapter's counting rule (the number of distinct values the guard produces among a warp's active lanes), how many passes their warps need.

    Do not design an actual GPU code generator, a bounds-check-elimination pass, or handling for shapes whose factors are awkward, such as a prime extent: this exercise stops at working the arithmetic out by hand for two shapes, not at a general rule for every shape.

    Test it: adapt `divergence_cost.cpp`'s `passes` function to the guard `idx < n`, for your chosen block size and each of the two shapes, and run it for every warp your block size produces. A correct answer gives 1 pass for every warp on the 64 × 64 shape, and more than 1 for at least one warp on the 70 × 70 shape; where your hand arithmetic and the program disagree, trust the program.

## Key ideas

!!! recap "Questions you can now answer"

    - **What is a warp, and how wide is one on NVIDIA, AMD and Apple hardware?** A fixed-size group of threads that a GPU issues one instruction to at a time. 32 threads on NVIDIA (a warp) and on Apple GPUs (a SIMD-group); 64 on AMD's CDNA parts and 32 on its RDNA parts (a wavefront).
    - **How does a SIMT lane differ from a SIMD lane?** A SIMD lane is a slot inside one wide instruction: every lane always does what that instruction says. A SIMT lane is a full thread, written as if it ran alone, that the hardware merely runs alongside its warp's other lanes whenever they agree on what to do next.
    - **How many passes does a warp need for a branch its lanes disagree about?** One pass per distinct choice among its active lanes: one if they agree (a warp-uniform branch), up to 32 if every lane disagrees with every other.
    - **What did independent thread scheduling change, and what did it leave the same?** It gave each thread its own program counter and call stack, since Volta, so a warp's diverged paths can interleave and reconverge more flexibly, and it made code that assumed implicit lockstep unsafe without an explicit sync. It did not remove the passes a divergent branch costs.
    - **Why can a compiler skip the boundary guard for the multiply kernel's 64 × 64 shape, but not always for other shapes?** 64 divides evenly by the warp size in both directions, so a block size exists that leaves no lane outside the array. A shape whose extent does not divide the chosen block size always leaves at least one boundary warp with lanes on both sides of the guard.
    - **Does a divergent branch change a Vortex kernel's floating-point results?** Not by itself: each lane still computes its own IEEE 754 arithmetic exactly as written, in isolation. Divergence changes how long the warp takes, not what any lane computes, unless the kernel also combines lanes' results with a reduction, which is [G6](g6-synchronization.md)'s subject.

## Where this comes back

!!! next "You will use this again in"

    - [G4. Memory performance: coalescing and bank conflicts](g4-memory-performance.md): *lane*, *thread mapping*
    - [G5. Occupancy and latency hiding](g5-occupancy.md): *warp*, *active warps per SM*
    - [G6. Synchronization, atomics and reductions](g6-synchronization.md): *barrier*, *warp-uniform*
    - [G9. GPU compilers inside LLVM](g9-gpu-compilers-in-llvm.md): *divergence*, *reconvergence*, *uniformity*
    - [G10. The GPU matmul ladder](g10-matmul-ladder.md): *thread mapping*, *block size*
    - [M10. MLIR for GPUs](../mlir/m10-mlir-for-gpus.md): *thread id*, *block id*

## Sources and further reading

Read the Programming Guide's thread-hierarchy and SIMT sections first, then its independent-thread-scheduling section; the HIP and Apple sources cover the same ground for their own hardware.

[^thread-hierarchy]: NVIDIA, "CUDA Programming Guide", v13.4.2, section 2.3.2, "Thread Hierarchy". <https://docs.nvidia.com/cuda/cuda-programming-guide/02-basics/writing-cuda-kernels.html#thread-hierarchy>
[^basics-simt]: NVIDIA, "CUDA Programming Guide", v13.4.2, section 2.3.1, "Basics of SIMT". <https://docs.nvidia.com/cuda/cuda-programming-guide/02-basics/writing-cuda-kernels.html>
[^thread-scheduling]: NVIDIA, "CUDA Programming Guide", v13.4.2, section 3.2.2.1.1, "Independent Thread Scheduling". <https://docs.nvidia.com/cuda/cuda-programming-guide/03-advanced/advanced-kernel-programming.html>
[^hip-cu]: AMD, "Hardware implementation", HIP 7.15.0 documentation. <https://rocm.docs.amd.com/projects/HIP/en/latest/understand/hardware_implementation.html>
[^apple-simd]: Apple, WWDC22 session 10159, "Scale compute workloads across Apple GPUs", 2022. <https://developer.apple.com/videos/play/wwdc2022/10159/>
[^mlir-thread-id]: MLIR Project, "'gpu' Dialect", operations `gpu.thread_id` and `gpu.block_id`. <https://mlir.llvm.org/docs/Dialects/GPU/#gputhread_id-gputhreadidop>
[^mlir-func]: MLIR Project, "'gpu' Dialect", operations `gpu.func` and `gpu.launch_func`. <https://mlir.llvm.org/docs/Dialects/GPU/#gpufunc-gpugpufuncop>

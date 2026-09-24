# G6. Synchronization, atomics and reductions

<p class="page-intro">Threads that share memory can step on each other unless something orders their reads and writes. This chapter covers the three tools that provide that order (the barrier, the atomic and the shuffle), then uses them to build a sum, and shows why summing the same numbers in parallel can print a different answer than summing them one at a time.</p>

<p class="vx-meta" markdown="1">Level: Intermediate · Reading time: about 35 minutes · Builds on: [G2. The SIMT execution model](g2-simt.md)</p>

???+ remember "Before you start, remember"

    ??? question "What is a warp, and what do its threads do on every instruction?"

        A fixed-size group of threads that a GPU core issues one instruction to at a time. Every active thread in the warp executes that instruction together; a thread the current branch does not apply to sits out, rather than running a different instruction of its own.

        Introduced in [G2. The SIMT execution model](g2-simt.md).

    ??? question "What is shared memory, and which threads can read a value another thread stored there?"

        An on-chip scratchpad that belongs to one thread block; every thread of that block can read and write it, and no thread outside the block can see it at all. A thread may read a value another thread of the same block wrote only once both threads have passed a point where the hardware guarantees the write is visible.

        Introduced in [G3. The GPU memory hierarchy](g3-memory-hierarchy.md#a-blocks-shared-scratchpad).

    ??? question "Why can a Vortex compiler freely reassign which thread reads which memory address, but not freely reorder a sum?"

        Reassigning addresses among threads moves values without touching any arithmetic operation's order, so it cannot change a result. Splitting a sum's work among threads changes the order its additions happen in, and Vortex's floating-point rules make that order part of the observable result.

        Introduced in [G4. Memory performance: coalescing and bank conflicts](g4-memory-performance.md#what-does-not-change-the-bits).

    ??? question "What must every f32 or f64 operation in Vortex do, and what may an implementation never do to it?"

        Produce the IEEE 754 result, rounded once, to nearest with ties to even. An implementation must not contract it into a fused operation, reassociate it with a neighbouring operation, reorder it, or evaluate it in a wider format.

        Introduced in [Numbers, literals and casts, decision 56](../decisions/numbers.md#d56).

!!! goals "In this chapter"

    - Explain why threads that share memory without an ordering rule can read a stale or half-written value, and how a barrier fixes that.
    - Distinguish an atomic read-modify-write from a barrier, and say what an atomic still leaves to chance.
    - Use a warp or SIMD-group shuffle to exchange a value between lanes without going through memory, and explain why it cannot cross a divergent branch.
    - Predict when two correct ways of summing the same numbers produce different bit patterns, and connect that to Vortex's floating-point rules.
    - Design a reduction whose result a Vortex program can promise to reproduce.

## A shared counter, and a wrong answer

Two threads each need to add 1 to a counter three times. If nothing coordinates them, here is one way their instructions could interleave: both threads read the counter's current value before either writes its new one back.

--8<-- "includes/examples/gpu/g6-synchronization/race.cpp.md"

Six increments should leave the counter at 6. The unsynchronized version leaves it at 3, because every round loses one increment: thread 0 reads 0, thread 1 also reads 0, thread 0 writes 1, and thread 1's write of `0 + 1` overwrites it with 1 again. This is a **race**: two operations on the same location, at least one a write, whose order the program does not fix, so the answer depends on an order the program never chose. A **data race** on the same address is exactly the hazard the [safety philosophy](../philosophy.md#safety-philosophy) asks a compiler to prevent in safe code.

The fix in the example is not a lock or a barrier; it is to make each round's read-modify-write indivisible, so there is no window between the read and the write for the other thread to step into. That indivisible operation is an **atomic**, covered below. A GPU kernel has a second tool for a different problem: two threads that do not touch the same address, but where one thread's later work depends on values several other threads wrote earlier. For that, the tool is a **barrier**.

## The barrier: ordering without a value

Suppose every thread of a block writes one element of a shared array, and then every thread needs to read an element some other thread wrote. Nothing here races on one address, but there is still an ordering problem: a thread that reaches the read before another thread has reached its write sees old, or partially written, data.

A **barrier** is a program point every thread of some group must reach before any of them may continue past it, and after which every write issued before the barrier is guaranteed visible to every read issued after it, for that group. CUDA's `__syncthreads()` is exactly this: a full block waits at the barrier, and only once every thread has arrived does any thread proceed, with every prior write to shared or global memory by any thread of the block now visible to the rest.[^n2-sync] Metal's threadgroup barrier plays the same role for a threadgroup, and Metal 4.1 lets a kernel attach a memory order to the barrier and to individual atomics, rather than assuming one fixed order for every use.[^ap1-sync]

A barrier moves no data. It adds one fact: everything written before it, by any thread in the group, happened before anything read after it, by any thread in the group. [G4](g4-memory-performance.md) already used one, without naming it, to make a tiled transpose correct: every thread of a block writes its element of the shared tile, `gpu.barrier` runs, and only then does every thread read a different element of the same tile, now guaranteed to be there.

??? check "In the transpose kernel from G4, what could a thread read if the `gpu.barrier` between the store and the load were removed?"

    Any bit pattern the shared-memory location happened to hold: the write from the intended thread, an old value from a previous block that reused the same on-chip memory, or, on real hardware, a torn mix of old and new bytes if the write was still in flight. Removing the barrier removes the only guarantee that the store completed and is visible before the load runs.

## Memory scopes: who has to notice

A barrier and an atomic both promise visibility, but visibility to whom is itself a choice. A **memory scope** is the set of threads an ordering guarantee applies to. CUDA's C++ atomics take an explicit scope: `cuda::thread_scope_thread` (only the issuing thread need ever see the effect, so the compiler is free to keep it in a register), `cuda::thread_scope_block`, `cuda::thread_scope_device` (every thread on the GPU), and `cuda::thread_scope_system` (the GPU and the CPU together).[^n3-scopes] `__syncthreads()`'s barrier is fixed at block scope: it says nothing about threads in another block, which may be running now, may not have started yet, or may have already finished, since CUDA does not guarantee that all of a grid's blocks run at the same time.[^n2-sync]

Scope is why "add a barrier" cannot fix every ordering problem. A reduction that needs to combine partial sums from every block of a grid cannot use a single block-scoped barrier at all: there is no grid-wide barrier inside a single kernel launch, only ordering tools that reach as far as the block. Combining across blocks needs either a second kernel launch, which the driver serializes after the first, or a device-scope atomic, next.

## Atomics: safe at one address, silent about order

An **atomic operation** reads a memory location, computes a new value from it, and writes the new value back, as one indivisible step that no other thread's atomic operation on the same location can interleave with. `atomicAdd` is the common case: every thread's addition is applied in full, one after another, with no lost update, whatever order the hardware chooses to apply them in.[^n2-atomics]

That last clause matters. An atomic guarantees *that* every operation happens exactly once and in full. It says nothing about *which* order the hardware applies them in, and for `atomicAdd` on floating-point values, order is not free: floating-point addition rounds after every step, so `(a + b) + c` and `a + (b + c)` can be different representable numbers even though every individual addition is correctly rounded. The next program makes this concrete by trying every possible arrival order for four fixed values on one thread, rather than waiting for real hardware to pick one at random:

--8<-- "includes/examples/gpu/g6-synchronization/atomic_order.cpp.md"

Twenty-four orders, only three distinct bit patterns among them: this is the CPU, in a single thread, running every order a device-scope `atomicAdd` reduction over these four values could produce, depending only on which thread's add the hardware happens to apply first. Nothing here needs real concurrency to demonstrate the point: it is a fact about floating-point addition, not about scheduling. Real hardware picks one of these orders per kernel launch, and unless the program forces a particular order, which launch's order it picked is not something the program controls.

??? check "Why does `atomicAdd` need no barrier, even though a race on the same address without it would be wrong?"

    A barrier orders operations that touch different addresses, or that would otherwise not be ordered at all. `atomicAdd` already makes every access to its one address indivisible and total, so there is nothing left for a barrier to add: the hazard a barrier prevents (reading a write that has not happened yet) cannot occur when every access to that address is folded into one atomic step.

## Shuffles: moving a value without touching memory

A barrier and an atomic both go through memory. A **shuffle** (CUDA's warp shuffle functions, Metal's SIMD-group functions) reads another lane's register directly, with no shared-memory location and no barrier, because every thread of one warp already executes the same instruction at the same time: there is no ordering left to establish, only a value to move.[^n2-shuffle] `__shfl_down_sync` is one shape of this: each lane's new value comes from the lane `delta` positions higher, letting a warp shift values toward lane 0 without touching memory at all.[^n2-shuffle]

`__shfl_xor_sync` shifts a different way: lane `i`'s new value comes from lane `i XOR mask`, which pairs every lane with exactly one partner and, unlike the down-shift, pairs every lane symmetrically. Applying it repeatedly with mask 16, then 8, then 4, then 2, then 1 across a 32-lane warp, each lane adding the shuffled-in value to its own, leaves every lane holding the sum of all 32: five instructions, no shared memory, no barrier, and any one lane can write the answer because they all have it. The MLIR `gpu` dialect exposes the same operation as `gpu.shuffle`, taking a kind (`xor`, `up`, `down` or `idx`), an operand, an offset and a width,[^gpu-dialect] and mlir-opt can check that a kernel written against it is well formed:

--8<-- "includes/examples/gpu/g6-synchronization/warp_reduce.mlir.md"

## Convergence: why these operations cannot dodge a branch

A shuffle's meaning depends on which other lanes are doing the same shuffle at the same moment: "the value in lane `i XOR 16`" only means something if lane `i XOR 16` is executing that same shuffle right now. A barrier's meaning depends on the same thing: "every thread of the block has arrived" only means something if every thread of the block is going to reach that barrier at all. Both are **convergent operations**: their result depends on which threads are participating together, a fact that a divergent branch can change.[^l5]

This is why a compiler must not move a barrier or a shuffle across a divergent branch, even one it could otherwise hoist or sink freely for an ordinary instruction. LLVM's documentation puts the reason plainly: for a convergent operation, "the set of threads which participate in communication is implicitly affected by control flow."[^l5] Move a shuffle out of one arm of an `if` and into the code before the branch, and lanes that would have taken the other arm now participate in a shuffle the source program never asked them to join; the two arms no longer compute what the unoptimized program computed. [G2](g2-simt.md) introduced divergence as a cost. This is why it is also a correctness boundary: the transformations that are always safe for ordinary arithmetic are not always safe for a barrier, an atomic, or a shuffle.

## Reductions: the same total, three different bit patterns

A **reduction** combines many values into one with an associative operation: a sum, a maximum, a count. Combining them in parallel means choosing a grouping, a specific order in which the operation is applied, and the figure below shows two groupings of the same four values.

<figure class="vx-figure">
<svg viewBox="0 0 700 236" role="img" aria-labelledby="g6-f1-title g6-f1-desc">
<title id="g6-f1-title">Two groupings of the same four values</title>
<desc id="g6-f1-desc">Four leaf values a0 to a3 are combined into one sum two ways. On the left, stride pairing combines a0 with a2 and a1 with a3 first, then combines those two partial sums: the grouping a warp shuffle or a shared-memory tree reduction uses. On the right, neighbour pairing combines a0 with a1 and a2 with a3 first, then combines those: the grouping a sequential or segmented sum uses. Both reach a sum of all four values, by a different route.</desc>

<text class="vx-text-muted" x="157" y="16" text-anchor="middle">Stride pairing (shuffle or shared-memory tree)</text>
<text class="vx-text-muted" x="537" y="16" text-anchor="middle">Neighbour pairing (sequential or segmented)</text>

<g class="vx-seq" style="--vx-i: 0; --vx-n: 3">
<rect class="vx-box" x="20" y="170" width="50" height="36" rx="4"/>
<text class="vx-text" x="45" y="193" text-anchor="middle">a0</text>
<rect class="vx-box" x="95" y="170" width="50" height="36" rx="4"/>
<text class="vx-text" x="120" y="193" text-anchor="middle">a1</text>
<rect class="vx-box" x="170" y="170" width="50" height="36" rx="4"/>
<text class="vx-text" x="195" y="193" text-anchor="middle">a2</text>
<rect class="vx-box" x="245" y="170" width="50" height="36" rx="4"/>
<text class="vx-text" x="270" y="193" text-anchor="middle">a3</text>
<rect class="vx-box" x="400" y="170" width="50" height="36" rx="4"/>
<text class="vx-text" x="425" y="193" text-anchor="middle">a0</text>
<rect class="vx-box" x="475" y="170" width="50" height="36" rx="4"/>
<text class="vx-text" x="500" y="193" text-anchor="middle">a1</text>
<rect class="vx-box" x="550" y="170" width="50" height="36" rx="4"/>
<text class="vx-text" x="575" y="193" text-anchor="middle">a2</text>
<rect class="vx-box" x="625" y="170" width="50" height="36" rx="4"/>
<text class="vx-text" x="650" y="193" text-anchor="middle">a3</text>
</g>

<g class="vx-seq" style="--vx-i: 1; --vx-n: 3">
<line class="vx-line" x1="45" y1="170" x2="120" y2="131"/>
<line class="vx-line" x1="195" y1="170" x2="120" y2="131"/>
<line class="vx-line" x1="120" y1="170" x2="195" y2="131"/>
<line class="vx-line" x1="270" y1="170" x2="195" y2="131"/>
<rect class="vx-box" x="95" y="95" width="50" height="36" rx="4"/>
<text class="vx-text" x="120" y="118" text-anchor="middle">a0+a2</text>
<rect class="vx-box" x="170" y="95" width="50" height="36" rx="4"/>
<text class="vx-text" x="195" y="118" text-anchor="middle">a1+a3</text>
<line class="vx-line" x1="425" y1="170" x2="462" y2="131"/>
<line class="vx-line" x1="500" y1="170" x2="462" y2="131"/>
<line class="vx-line" x1="575" y1="170" x2="612" y2="131"/>
<line class="vx-line" x1="650" y1="170" x2="612" y2="131"/>
<rect class="vx-box" x="437" y="95" width="50" height="36" rx="4"/>
<text class="vx-text" x="462" y="118" text-anchor="middle">a0+a1</text>
<rect class="vx-box" x="587" y="95" width="50" height="36" rx="4"/>
<text class="vx-text" x="612" y="118" text-anchor="middle">a2+a3</text>
</g>

<g class="vx-seq" style="--vx-i: 2; --vx-n: 3">
<line class="vx-line" x1="120" y1="95" x2="157" y2="56"/>
<line class="vx-line" x1="195" y1="95" x2="157" y2="56"/>
<rect class="vx-box-accent" x="132" y="20" width="50" height="36" rx="4"/>
<text class="vx-text" x="157" y="43" text-anchor="middle">sum</text>
<line class="vx-line" x1="462" y1="95" x2="537" y2="56"/>
<line class="vx-line" x1="612" y1="95" x2="537" y2="56"/>
<rect class="vx-box-accent" x="512" y="20" width="50" height="36" rx="4"/>
<text class="vx-text" x="537" y="43" text-anchor="middle">sum</text>
</g>
</svg>
<figcaption>Figure 1. The same four values, added in two different groupings. Left: pair a0 with a2 and a1 with a3 first, then add the two partial sums, the grouping a warp shuffle's xor pattern or a shared-memory tree reduction with the same stride order computes. Right: pair each value with its neighbour first, then add the two partial sums, the grouping a sequential fold or a segmented, per-group reduction computes. Every addition on both sides is a single, correctly rounded IEEE&nbsp;754 operation. The two totals can still land on different representable numbers, because rounding happens after every addition and the two sides round at different points.</figcaption>
</figure>

The next example runs this idea at the size of one warp, thirty-two values, three ways: a strict left-to-right fold (one running accumulator, the order a single thread would use); a stride-halving tree, the grouping a shared-memory reduction or the shuffle pattern above computes; and a segmented sum, the grouping four independent partial sums, combined in a fixed order, would compute.

--8<-- "includes/examples/gpu/g6-synchronization/reduction_orders.cpp.md"

All three totals are correct: every addition in every method is a single, correctly rounded operation on the same thirty-two input values, exactly as [decision 56](../decisions/numbers.md#d56) requires. None of them is more correct than the others. They are three different real numbers' worth of rounding error, and the example's values were chosen, the way [P11](../optimize/p11-floating-point.md) chooses its associativity example, to make the difference land somewhere visible rather than in a bit nobody would notice.

??? check "The tree method and a warp-shuffle reduction using mask order 16, 8, 4, 2, 1 sum the same thirty-two values. Do they produce the same bits?"

    Yes. Both apply the identical grouping, pairing the same elements in the same order at each round; only the mechanism differs (values move through shared memory and a barrier in one, through registers and a shuffle in the other). Two correctly rounded additions of the same two operands, in the same order, produce the same bit pattern regardless of where the operands live.

## Choosing a reduction Vortex can promise

Nothing above is a bug. Every method computed a valid sum of the same thirty-two numbers, correctly rounded at every step. The problem for a language like Vortex is a promise, not a defect: [the safety philosophy](../philosophy.md#safety-philosophy) requires that "floating-point reassociation, reduced precision, and non-deterministic parallel reductions" have documented behavior, and "documented" has to mean something more specific than "some correct grouping was used."

Two different guarantees are available, and they cost different things:

- **A fixed grouping.** Pick one algorithm, such as the stride-halving tree, and always use it for a given input size and launch shape. The result is then exactly reproducible: the same program, the same input, the same launch configuration, produces the same bits every time, on every run, because the grouping never depends on anything the hardware decides at run time. Change the launch shape (a different block size, a different split across warps), and the grouping, and so the bits, can change with it. Reproducible summation libraries such as ReproBLAS take a version of this approach further, fixing both an order and a representation designed to make the result independent of the order altogether.[^reproblas]
- **Whatever order arrives.** Let every thread apply its share with a plain `atomicAdd` into one location. This costs the least code and, on some hardware, the least time, but the order is whatever the memory system's arbitration produces, and [the atomic example above](#atomics-safe-at-one-address-silent-about-order) already showed that different orders of the same values are not guaranteed to agree.

A fixed grouping is not free of cost either: it can mean more synchronization (a barrier between rounds of a tree, where an atomics-only version needs none), a specific launch shape the schedule must honor, or, for full order-independence, extra arithmetic per element. Demmel and Nguyen's reproducible summation work is a worked example of exactly that trade: guaranteed order-independence, purchased with additional computation per term.[^dn13]

One hazard both guarantees avoid for free: a reduction's output cannot alias its input. A signature such as `fn total(values: &[f32; n], out: &mut f32)` gives the callee a `&mut` result and a separate, non-aliased input array, and [decision 25](../decisions/references.md#d25) already forbids a variable lent as `&mut` from appearing again as another argument of the same call. Every thread's partial write, whatever grouping produced it, lands in a location the compiler knows no read of the input can also touch.

## For Vortex

!!! vortex "Exercise"

    **Build** a written design, not code: a rule for what a reduction (`sum`, `max`, or a fold the language exposes over a fixed-shape array) is allowed to do in Vortex, using the [feature decision worksheet](../philosophy.md#feature-decision-worksheet). Answer, in writing, for a reduction over an `[f32; N]` or a GPU-mapped array:

    1. **Problem:** which real Vortex program needs a reduction, and does it need the same bits on every run, or only a correct sum?
    2. **Example:** the smallest valid Vortex program that reduces an array, and what its documented result is.
    3. **Boundary:** what looks like a reduction but is not one under your rule (for instance, a fold with a non-associative or order-sensitive combining function).
    4. **Compiler knowledge:** does the compiler need to know the reduction's grouping to generate code, or only that the operation is associative enough to parallelize at all?
    5. **Safety:** which of "reassociation," "reduced precision" and "non-deterministic parallel reduction," the three hazards the safety philosophy names, does your rule allow, forbid, or require an explicit opt-in for?
    6. **Targets:** does your rule mean the same thing on a single CPU thread, on multiple CPU threads, and on a GPU launch, or does the GPU case need its own wording?
    7. **Diagnostics:** what, if anything, should the compiler tell the programmer about which grouping a reduction used?
    8. **Testing:** a test that proves two runs of the same program, on the same input, produce identical bits, and, if your rule allows a fast, non-deterministic mode, a test that proves that mode is only reachable through the opt-in your rule names.

    **Not yet:** generating any GPU or CPU code for a reduction ([M12](../mlir/m12-vortex-gpu-path.md) weighs the paths, [G10](g10-matmul-ladder.md) builds a kernel that contains one), and a parallel CPU implementation ([P13](../optimize/p13-multithreading.md) covers splitting work across CPU threads).

    **Proof that it works:** your worksheet answers are internally consistent (an answer to 5 that forbids non-deterministic reduction cannot leave question 8's opt-in test unanswered), name a concrete test for every claim in question 8, and state, for the example program in question 2, the exact bits or exact real-number result a conforming implementation must produce.

## Key ideas

!!! recap "Questions you can now answer"

    - **What does a barrier guarantee, and to whom?** That every write issued before it, by any thread in some group, is visible to every read issued after it, by any thread in that same group. It moves no data and says nothing about threads outside the group.
    - **What does an atomic guarantee, and what does it leave open?** That a read-modify-write on one address happens as one indivisible step, so no update is lost. It does not fix the order in which different threads' atomics are applied.
    - **Why does a shuffle need no barrier?** It reads another lane's register directly, and the lanes of one warp already execute together, so there is no missing ordering for a barrier to add.
    - **Why can a compiler not move a barrier or a shuffle across a divergent branch?** Both are convergent operations: their meaning depends on which threads participate together, and a divergent branch can change that set.
    - **Can two correct reductions of the same numbers give different answers?** Yes: floating-point addition rounds after every step, so two different groupings of the same values can round to different representable numbers, even though every individual addition follows IEEE 754 exactly.
    - **What makes a parallel reduction reproducible across runs?** Fixing its grouping (and its launch shape) so the order of additions never depends on anything decided at run time, unlike a plain atomic accumulation.

## Where this comes back

!!! next "You will use this again in"

    - [G7. Programming models tour](g7-programming-models.md): *barrier*, *atomic*, *shuffle* under each model's own names
    - [G10. The GPU matmul ladder](g10-matmul-ladder.md): *barrier*, *shared-memory tree reduction*
    - [G11. Matrix units](g11-matrix-units.md): *warp shuffle*, *convergent operation*
    - [G12. Fusion case study: FlashAttention](g12-flashattention.md): *reduction*, *fixed grouping*
    - [G14. Measuring GPU code](g14-measuring-gpu-code.md): *atomic*, *reduction*
    - [M10. MLIR for GPUs](../mlir/m10-mlir-for-gpus.md): *`gpu.barrier`*, *`gpu.shuffle`*
    - [M12. Designing Vortex's GPU path](../mlir/m12-vortex-gpu-path.md): *documented reduction grouping*

## Sources and further reading

Read the CUDA Programming Guide's synchronization and atomics sections first, then the appendix's warp shuffle functions, then the convergent-operations note, which explains why the first three cannot be freely reordered.

[^n2-sync]: NVIDIA, "CUDA Programming Guide", v13.4.2, section 2.3.2.1, "Thread Block Synchronization". <https://docs.nvidia.com/cuda/cuda-programming-guide/02-basics/writing-cuda-kernels.html#thread-block-synchronization>
[^ap1-sync]: Apple, "Metal Shading Language Specification", version 4.1, section 6.10 (threadgroup and SIMD-group synchronization and functions; Metal 4.1 adds an explicit memory order to barriers and atomics). <https://developer.apple.com/metal/Metal-Shading-Language-Specification.pdf>
[^n3-scopes]: NVIDIA, "CUDA Programming Guide", v13.4.2, section 3.2.3, "Thread Scopes". <https://docs.nvidia.com/cuda/cuda-programming-guide/03-advanced/advanced-kernel-programming.html#thread-scopes>
[^n2-atomics]: NVIDIA, "CUDA Programming Guide", v13.4.2, section 2.3.5, "Atomics". <https://docs.nvidia.com/cuda/cuda-programming-guide/02-basics/writing-cuda-kernels.html#atomics>
[^n2-shuffle]: NVIDIA, "CUDA Programming Guide", v13.4.2, section 5.4.6.5, "Warp Shuffle Functions" (C++ Language Extensions appendix). <https://docs.nvidia.com/cuda/cuda-programming-guide/05-appendices/cpp-language-extensions.html#warp-shuffle-functions>
[^l5]: LLVM Project, "Convergent Operations", read 2026-09-24. <https://llvm.org/docs/ConvergentOperations.html>
[^gpu-dialect]: MLIR, "'gpu' Dialect", the `gpu.shuffle` and `gpu.barrier` operations, read 2026-09-24. <https://mlir.llvm.org/docs/Dialects/GPU/>
[^reproblas]: ReproBLAS, a library for reproducible summation and BLAS operations regardless of order or thread count. <https://bebop.cs.berkeley.edu/reproblas/>
[^dn13]: James Demmel and Hong Diep Nguyen, "Fast Reproducible Floating-Point Summation", *21st IEEE Symposium on Computer Arithmetic (ARITH)*, 2013. <https://doi.org/10.1109/ARITH.2013.9>

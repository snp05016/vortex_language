# G6. Synchronization, atomics and reductions

<p class="page-intro">Threads that share memory need a rule that orders their reads and writes, or the answer depends on timing nobody chose. This chapter covers the three tools GPUs provide for that (the barrier, the atomic and the shuffle), builds sums out of them at the level of a warp, a block and a whole grid, and shows why two correct parallel sums of the same numbers can differ in the last bits, which is a question the Vortex language has to answer, not only its compiler.</p>

<p class="vx-meta" markdown="1">Level: Intermediate · Reading time: about 45 minutes · Builds on: [G2. The SIMT execution model](g2-simt.md), [G3. The GPU memory hierarchy](g3-memory-hierarchy.md)</p>

???+ remember "Before you start, remember"

    ??? question "On NVIDIA GPUs since compute capability 7.0, may code assume that the 32 threads of a warp execute every instruction together?"

        No. With independent thread scheduling every thread has its own program counter, and threads can diverge and reconverge in groups smaller than a warp. Code in which the threads of one warp exchange data must synchronize them explicitly.

        Introduced in [G2. The SIMT execution model](g2-simt.md#independent-thread-scheduling).

    ??? question "Which threads can read a value that a thread stored in shared memory?"

        Only threads of the same thread block. Each block has its own on-chip scratchpad, which other blocks cannot see (distributed shared memory, for the blocks of one cluster on newer NVIDIA GPUs, is the exception).

        Introduced in [G3. The GPU memory hierarchy](g3-memory-hierarchy.md#shared-memory-a-blocks-scratchpad).

    ??? question "When is a branch uniform?"

        When its condition has the same value in every active lane of the warp, so the whole warp takes one side. The block index and the kernel's arguments are uniform; the thread index, and anything computed from it, is divergent.

        Introduced in [G2. The SIMT execution model](g2-simt.md#uniform-values-and-branches-that-disappear).

    ??? question "Why can `(a + b) + c` and `a + (b + c)` give different `f32` results?"

        Each addition rounds its exact result to the nearest representable value. The two groupings round different intermediate values, so the rounding errors differ, and so can the results.

        Introduced in [P11. Floating point under optimization](../optimize/p11-floating-point.md).

    ??? question "What must every `f32` or `f64` operation in Vortex do, and what may an implementation never do to it?"

        Produce the IEEE 754 result, rounded once to nearest with ties to even. An implementation must not contract it into a fused operation, reassociate or reorder it, or evaluate it in a wider format.

        Introduced in [Numbers, literals and casts, decision 56](../decisions/numbers.md#d56).

!!! goals "In this chapter"

    - Trace every interleaving of two unsynchronized increments, and explain which ones lose an update and how an atomic removes them.
    - Place a barrier so that every thread of its group reaches it, and say what it makes visible and to which threads.
    - Distinguish what an atomic guarantees from what it leaves to the hardware, and predict when the arrival order changes a result.
    - Build a sum from warp shuffles, from shared memory and a barrier, and across the blocks of a grid, and explain why none of these operations may sit behind a branch that splits its group.
    - Predict when two correct parallel sums of the same numbers produce different bits, and weigh the ways a language can promise a reproducible reduction.

## Two threads, one counter

Start with the smallest shared-memory program there is. A counter in memory holds 0. Two threads each run `counter = counter + 1`. An ordinary increment is not one memory operation: each thread **loads** the counter into a register, adds 1 there, and **stores** the register back. Call thread 0's steps L0 and S0, and thread 1's L1 and S1. Each thread's own steps stay in order, but nothing fixes how the two threads' steps interleave.

There are six interleavings, few enough to write out. In `L0 S0 L1 S1`, thread 1 loads the 1 that thread 0 stored, and the counter ends at 2. In `L0 L1 S0 S1`, both threads load 0, both store 1, and the counter ends at 1: thread 0's increment is gone. The first example tries all six.

--8<-- "includes/examples/gpu/g6-synchronization/race.cpp.md"

Only the two schedules that finish one thread before the other starts give 2. The other four lose an update. This is a **race**: the result depends on an order the program never fixed. A **data race**, the precise form, is two accesses to the same location from different threads, at least one of them a write, with nothing that orders them.

CUDA does not promise even the four wrong answers: the Programming Guide says a data race has undefined behavior and the values read can be anything.[^n5-fence] Its own teaching example makes the same point with a sum, where a plain `s[0] = s[0] + x` instead of an atomic add gives a total that is too small and changes from run to run and from GPU to GPU.[^n2-atomics]

<figure class="vx-figure">
<svg viewBox="0 0 720 310" role="img" aria-label="Two plain increments losing an update, and two atomic increments that cannot" aria-describedby="g6-f1-desc">
<title id="g6-f1-title">A lost update, and the same two increments as atomics</title>
<desc id="g6-f1-desc">Left: time runs downward through four steps. Thread 0 loads 0 into r0, thread 1 loads 0 into r1, thread 0 stores 1, thread 1 stores 1. The counter reads 0, 0, 1, 1; the last store is marked as the one that overwrote thread 0's work. Right: each thread performs a single atomic add; the counter reads 1, then 2, in either order.</desc>
<text class="vx-text" x="20" y="24">plain increment: a load, then a store</text>
<text class="vx-text" x="390" y="24">atomic increment: one indivisible step</text>
<text class="vx-text-muted" x="75" y="52" text-anchor="middle">thread 0</text>
<text class="vx-text-muted" x="185" y="52" text-anchor="middle">counter</text>
<text class="vx-text-muted" x="295" y="52" text-anchor="middle">thread 1</text>
<text class="vx-text-muted" x="445" y="52" text-anchor="middle">thread 0</text>
<text class="vx-text-muted" x="555" y="52" text-anchor="middle">counter</text>
<text class="vx-text-muted" x="665" y="52" text-anchor="middle">thread 1</text>
<rect class="vx-box" x="20" y="70" width="110" height="34" rx="4"/>
<text class="vx-text" x="75" y="92" text-anchor="middle">load: r0 = 0</text>
<rect class="vx-box-strong" x="160" y="70" width="50" height="34" rx="4"/>
<text class="vx-text" x="185" y="92" text-anchor="middle">0</text>
<line class="vx-line" x1="130" y1="87" x2="160" y2="87"/>
<rect class="vx-box" x="240" y="120" width="110" height="34" rx="4"/>
<text class="vx-text" x="295" y="142" text-anchor="middle">load: r1 = 0</text>
<rect class="vx-box-strong" x="160" y="120" width="50" height="34" rx="4"/>
<text class="vx-text" x="185" y="142" text-anchor="middle">0</text>
<line class="vx-line" x1="210" y1="137" x2="240" y2="137"/>
<rect class="vx-box" x="20" y="170" width="110" height="34" rx="4"/>
<text class="vx-text" x="75" y="192" text-anchor="middle">store 0 + 1</text>
<rect class="vx-box-strong" x="160" y="170" width="50" height="34" rx="4"/>
<text class="vx-text" x="185" y="192" text-anchor="middle">1</text>
<line class="vx-line" x1="130" y1="187" x2="160" y2="187"/>
<rect class="vx-box" x="240" y="220" width="110" height="34" rx="4"/>
<text class="vx-text" x="295" y="242" text-anchor="middle">store 0 + 1</text>
<rect class="vx-box-bad" x="160" y="220" width="50" height="34" rx="4"/>
<text class="vx-text" x="185" y="242" text-anchor="middle">1</text>
<line class="vx-line" x1="210" y1="237" x2="240" y2="237"/>
<text class="vx-text-accent" x="185" y="282" text-anchor="middle">two increments, counter = 1</text>
<text class="vx-text-muted" x="185" y="300" text-anchor="middle">thread 1's store overwrote thread 0's</text>
<rect class="vx-box" x="390" y="70" width="110" height="34" rx="4"/>
<text class="vx-text" x="445" y="92" text-anchor="middle">add 1</text>
<rect class="vx-box-strong" x="530" y="70" width="50" height="34" rx="4"/>
<text class="vx-text" x="555" y="92" text-anchor="middle">1</text>
<line class="vx-line" x1="500" y1="87" x2="530" y2="87"/>
<rect class="vx-box" x="610" y="120" width="110" height="34" rx="4"/>
<text class="vx-text" x="665" y="142" text-anchor="middle">add 1</text>
<rect class="vx-box-strong" x="530" y="120" width="50" height="34" rx="4"/>
<text class="vx-text" x="555" y="142" text-anchor="middle">2</text>
<line class="vx-line" x1="580" y1="137" x2="610" y2="137"/>
<text class="vx-text-accent" x="555" y="200" text-anchor="middle">either order: counter = 2</text>
<text class="vx-text-muted" x="555" y="218" text-anchor="middle">no step can fall between</text>
<text class="vx-text-muted" x="555" y="236" text-anchor="middle">the read and the write</text>
<line class="vx-line" x1="365" y1="10" x2="365" y2="305"/>
</svg>
<figcaption>Figure 1. The schedule <code>L0 L1 S0 S1</code> on the left: both threads read 0 before either writes, and thread 1's store of 1 overwrites thread 0's. On the right each increment is one indivisible step, so there is no gap between a thread's read and its write for the other thread to fall into, and both orders end at 2.</figcaption>
</figure>

The last line of the example shows the fix for this program. An **atomic** increment reads, adds and writes as one indivisible step, so no other thread's access to the same location can fall between its read and its write. Two steps, two schedules, and both end at 2. Vortex's [safety philosophy](../philosophy.md#safety-philosophy) asks the language to prevent data races in safe code, so every tool in this chapter is something a Vortex GPU compiler will either generate itself or have to reason about.

The counter is the case where two threads touch one address. The more common case on a GPU touches many addresses: each thread writes its own element of an array, then reads an element that another thread wrote. No two threads write the same word, and an atomic would not help, but a thread that reads too early sees an old value. That problem needs a different tool.

## The barrier: ordering without a value

A **barrier** is a point in the program that every thread of a group must reach before any of them continues. CUDA's `__syncthreads()` is the barrier for a thread block. The Programming Guide defines it in two parts. First, it waits until every thread of the block that has not exited reaches the same `__syncthreads()` call. Second, it orders memory: the call **happens before** any participating thread leaves it. "Happens before" is the C++ memory model's relation for "everything on the earlier side is visible to the later side".[^n5-sync]

Put plainly, every read or write a thread of the block made before the barrier is visible to every thread of the block after it. The barrier moves no data and computes nothing. It only adds that ordering. [G4](g4-memory-performance.md)'s tiled transpose needed exactly this: every thread stores its element of a shared tile, the block waits at a barrier, and only then does each thread read an element that another thread stored. CUDA's cooperative-groups interface states the same two guarantees for its `sync()` on a thread block: all threads arrive before any proceeds, and all memory accesses made before are visible after.[^cg]

A barrier comes with a rule that the rest of this chapter keeps meeting: every thread of the group must reach it, or none. `__syncthreads()` is allowed inside an `if` only when the condition has the same value in the whole block; otherwise the Programming Guide warns that the kernel may hang or behave in undefined ways.[^n5-sync] The Metal Shading Language states the same rule for `threadgroup_barrier`, and adds that inside a loop, if any thread executes the barrier in an iteration, every thread must execute it in that iteration.[^msl-sync] MLIR's `gpu.barrier` says it in compiler terms: once one thread executes a particular dynamic instance of the barrier, all threads of its scope must execute that same instance.[^mlir-gpu]

```text
if block_index > 0   { barrier }   // same value in every thread of a block: allowed
if thread_index < 32 { barrier }   // splits a block of 64: may hang
```

GPUs also have a barrier for a single warp. CUDA's `__syncwarp(mask)` synchronizes the lanes named in the mask and orders their memory accesses the same way,[^n5-sync] and Metal has `simdgroup_barrier`.[^msl-sync] After independent thread scheduling ([G2](g2-simt.md#independent-thread-scheduling)), this is how code tells the hardware that lanes of one warp must meet before they exchange data through memory.

??? check "A block has 128 threads. Each thread knows `n`, a kernel argument, and `t`, its own index. Which of these barriers is legal: one inside `if n > 0`, one inside `if t < n`, one after a loop `for i in 0..n` whose body has no barrier, and one inside the body of `for i in 0..t`?"

    The first and third. `n` has the same value in every thread, so either all threads enter `if n > 0` or none do, and every thread reaches a barrier placed after a loop exactly once, however long the loop ran. `if t < n` splits the block whenever `n` is between 1 and 127. The loop `for i in 0..t` runs a different number of times in each thread, so thread 5 reaches its sixth barrier while thread 3 has left the loop: the threads disagree about which instance of the barrier they are at, the case that the Metal and MLIR rules above forbid in so many words.

## Scopes and fences: who has to notice

A barrier makes writes visible to the threads of one block and to nobody else. That limit has a name. A **memory scope** is the set of threads that an ordering guarantee covers. CUDA defines five: thread, block, cluster (a group of blocks that newer GPUs can schedule together), device and system. The Programming Guide pairs each with the level of the memory hierarchy where the threads in that scope agree on a value: nowhere for a single thread, the L1 cache for a block, the L2 cache for a cluster or the whole device, and the L2 plus the caches of connected processors for the system.[^n3-scopes]

<figure class="vx-figure">
<svg viewBox="0 0 720 325" role="img" aria-label="CUDA's memory scopes as nested regions, with the cache level where each becomes coherent" aria-describedby="g6-f2-desc">
<title id="g6-f2-title">Memory scopes, nested</title>
<desc id="g6-f2-desc">Five nested rectangles. Innermost, a thread, whose private values live in registers. Around it, block scope, coherent at L1, where __syncthreads and atomicAdd_block act. Around that, cluster scope, coherent at L2. Around that, device scope, the whole GPU, coherent at L2, where atomicAdd and __threadfence act. Outermost, system scope, including the CPU and other GPUs, coherent at L2 and connected caches, where atomicAdd_system and __threadfence_system act.</desc>
<rect class="vx-box" x="10" y="30" width="700" height="290" rx="6"/>
<text class="vx-text-muted" x="22" y="52">system scope: GPU, CPU and other GPUs; coherent at L2 and connected caches</text>
<rect class="vx-box" x="30" y="70" width="600" height="210" rx="6"/>
<text class="vx-text-muted" x="42" y="92">device scope: the whole GPU; coherent at L2</text>
<rect class="vx-box" x="50" y="110" width="440" height="160" rx="6"/>
<text class="vx-text-muted" x="62" y="132">cluster scope: coherent at L2</text>
<rect class="vx-box" x="70" y="150" width="300" height="110" rx="6"/>
<text class="vx-text-muted" x="82" y="172">block scope: coherent at L1</text>
<rect class="vx-box-accent" x="90" y="190" width="130" height="50" rx="4"/>
<text class="vx-text" x="155" y="220" text-anchor="middle">thread</text>
<text class="vx-text-muted" x="155" y="256" text-anchor="middle">registers only</text>
<text class="vx-mono" x="235" y="206">__syncthreads</text>
<text class="vx-mono" x="235" y="228">atomicAdd_block</text>
<text class="vx-mono" x="505" y="206">atomicAdd</text>
<text class="vx-mono" x="505" y="228">__threadfence</text>
<text class="vx-mono" x="40" y="306">atomicAdd_system, __threadfence_system</text>
</svg>
<figcaption>Figure 2. CUDA's thread scopes, each containing the ones inside it, labelled with the level of the memory hierarchy where its threads agree on a value and with the operations that act at that scope. A wider scope reaches more threads and has to go further out in the hierarchy to do it.</figcaption>
</figure>

Ordering matters even between threads that never meet at a barrier. Memory on a GPU is **weakly ordered**: two writes by one thread need not become visible to another thread in the order they were made.[^n5-fence]

The Programming Guide's example has one thread store 10 to `X` and then 20 to `Y`, while another thread reads `Y` and then `X`. Without synchronization the reader may see the new `Y` and the old `X`. A **memory fence** placed between the two stores, and another between the two loads, rules that outcome out: if the reader sees the new `Y`, it also sees the new `X`. The fence is scoped too: `__threadfence_block()` for threads of one block, `__threadfence()` for the device, `__threadfence_system()` for the system.[^n5-fence]

A fence orders, but it does not wait for anyone. Modern CUDA code usually expresses the same ordering through atomics with a **memory order**, borrowed from C++. A **release** store makes the thread's earlier writes visible to any thread that reads the stored value with an **acquire** load; a **relaxed** operation is atomic and orders nothing else. The Programming Guide's producer-consumer example writes data, sets a flag with a release store at block scope, and has the consumer spin on the flag with acquire loads before it reads the data.[^n3-scoped]

The same page gives the performance rule: use the narrowest scope that is correct, because block-scoped atomics are much faster than system-scoped ones, and the weakest memory order that is correct.[^n3-scoped] Metal reached the same design recently: from Metal 4.1, `threadgroup_barrier`, `simdgroup_barrier` and the atomic functions accept a memory order, including acquire and release, and the barriers accept a thread scope.[^msl-new] For a compiler, scope and order are two more facts it must choose for every synchronizing operation it emits, and choosing them too wide is safe but slow, while choosing them too narrow is fast and wrong.

## Atomics: safe at one address, silent about order

An **atomic read-modify-write** reads a memory location, computes a new value from it and writes the result back as one step that no other atomic on the same location can interrupt. CUDA's `atomicAdd(address, val)` reads the old value, computes old + val, stores it and returns the old value; it works on `int`, `unsigned`, `float`, `double` and several narrower and vector types, in global or shared memory.[^n5-atomics] The plain function is atomic at device scope; `atomicAdd_block` and `atomicAdd_system` choose block and system scope. These original ("legacy") functions have relaxed ordering: they guarantee atomicity and nothing about other memory.[^n5-atomics]

Any other atomic can be built from **compare-and-swap**. `atomicCAS(address, expected, desired)` stores `desired` only if the location still holds `expected`, and returns what it found. A thread reads the old value, computes a new one, and retries the swap until no other thread got there first. The Programming Guide builds a floating-point add this way, comparing the values as integer bit patterns, which avoids spinning forever on a NaN, a value that never compares equal to itself.[^n5-atomics] Metal's `atomic_float` supports add and subtract in device memory since Metal 3, and in threadgroup memory from Metal 4.1.[^msl-atomics]

What an atomic does not fix is order. When several lanes of a warp perform an atomic on one address, the Programming Guide says every read-modify-write happens and they are serialized, but the order in which they occur is undefined.[^n3-its] For integer addition the order cannot matter: addition of integers is exact, so every order gives the same total as long as nothing overflows. For floating-point addition it can. The second example applies all 24 arrival orders of four partial sums to a total that starts at 0.

--8<-- "includes/examples/gpu/g6-synchronization/atomic_order.cpp.md"

Work one order by hand. `f32` has a 24-bit significand, so at 2^24 = 16,777,216 the representable values are 2 apart, and 2^24 + 1 lies exactly between two of them. Round-to-nearest-even picks 2^24. So the order 2^24, 1, 1, −2^24 loses both 1s and ends at 0; the order −2^24, 1, 1, 2^24 keeps both, because −16,777,215 is representable, and ends at 2. The 24 orders produce three totals, and the integer version produces one. Every add in every order was a single, correctly rounded IEEE 754 operation. The difference comes from the order alone.

Atomics also cost time when many threads aim at one address, because the hardware serializes them. The Programming Guide advises using them sparingly for that reason, and its own sum example first adds up each block's values in shared memory, so that each block performs one atomic instead of one per thread.[^n2-atomics]

??? check "One kernel counts the elements of an array greater than a threshold, with an `atomicAdd` of 1 on an `int` per element found. Another sums those elements with an `atomicAdd` on a `float`. Which result can differ between two runs on the same input?"

    Only the `float` sum. The count adds integers, and integer addition gives the same total in every order (as long as the count stays below the largest `int`). The sum adds floating-point values in whatever order the hardware serializes the atomics, and, as the 24 orders above show, different orders can round to different totals.

## Shuffles: moving a value without touching memory

A barrier and an atomic both communicate through memory. Inside one warp there is a shorter path. A **shuffle** lets a lane read a register of another lane of the same warp directly, with no shared memory at all. CUDA has four forms: `__shfl_sync` reads from a lane given by number, `__shfl_up_sync` and `__shfl_down_sync` from the lane a fixed distance below or above, and `__shfl_xor_sync` from the lane whose number is the caller's lane number XOR a mask.[^n5-shfl]

Every one of these takes a `mask` naming the lanes that take part, and waits until every non-exited lane in the mask reaches the call. The mask is not a formality. Since independent thread scheduling, the lanes of a warp are not guaranteed to be at the same instruction, and code written for older GPUs that assumed they were, such as reductions within a warp that used no synchronization, has to be revisited.[^n3-its] A shuffle also has rules like a barrier's: every lane named in the mask must call it with the same mask, and a lane that reads from a lane not taking part gets an undefined value.[^n5-shfl]

The XOR form is the one reductions use; the Programming Guide calls its pattern a **butterfly**, used for tree reductions and broadcasts.[^n5-shfl] With mask 4, lanes 0 and 4 swap values, 1 and 5, and so on: every lane has exactly one partner. If each lane adds the value it receives to its own, then repeats with masks 2 and 1, every lane ends holding the sum of all eight. Figure 3 works it for lanes holding 1 to 8.

<figure class="vx-figure">
<svg viewBox="0 0 740 340" role="img" aria-label="An XOR butterfly over eight lanes holding 1 to 8; after three steps every lane holds 36" aria-describedby="g6-f3-desc">
<title id="g6-f3-title">An XOR butterfly sum over eight lanes</title>
<desc id="g6-f3-desc">Four rows of eight boxes, one column per lane. The first row holds the values 1 to 8. After the step with mask 4, lanes 0 to 3 and lanes 4 to 7 both hold 6, 8, 10, 12. After mask 2 the lanes alternate 16 and 20. After mask 1 every lane holds 36. Lines show that each lane adds its own value and the value of the lane whose number differs in one bit.</desc>
<text class="vx-text-muted" x="150" y="30" text-anchor="middle">lane 0</text>
<text class="vx-text-muted" x="226" y="30" text-anchor="middle">lane 1</text>
<text class="vx-text-muted" x="302" y="30" text-anchor="middle">lane 2</text>
<text class="vx-text-muted" x="378" y="30" text-anchor="middle">lane 3</text>
<text class="vx-text-muted" x="454" y="30" text-anchor="middle">lane 4</text>
<text class="vx-text-muted" x="530" y="30" text-anchor="middle">lane 5</text>
<text class="vx-text-muted" x="606" y="30" text-anchor="middle">lane 6</text>
<text class="vx-text-muted" x="682" y="30" text-anchor="middle">lane 7</text>
<text class="vx-text" x="20" y="80">lane values</text>
<text class="vx-mono" x="20" y="160">xor 4</text>
<text class="vx-mono" x="20" y="240">xor 2</text>
<text class="vx-mono" x="20" y="320">xor 1</text>
<line class="vx-line" x1="150" y1="90" x2="150" y2="140"/>
<line class="vx-line" x1="454" y1="90" x2="150" y2="140"/>
<line class="vx-line" x1="226" y1="90" x2="226" y2="140"/>
<line class="vx-line" x1="530" y1="90" x2="226" y2="140"/>
<line class="vx-line" x1="302" y1="90" x2="302" y2="140"/>
<line class="vx-line" x1="606" y1="90" x2="302" y2="140"/>
<line class="vx-line" x1="378" y1="90" x2="378" y2="140"/>
<line class="vx-line" x1="682" y1="90" x2="378" y2="140"/>
<line class="vx-line" x1="454" y1="90" x2="454" y2="140"/>
<line class="vx-line" x1="150" y1="90" x2="454" y2="140"/>
<line class="vx-line" x1="530" y1="90" x2="530" y2="140"/>
<line class="vx-line" x1="226" y1="90" x2="530" y2="140"/>
<line class="vx-line" x1="606" y1="90" x2="606" y2="140"/>
<line class="vx-line" x1="302" y1="90" x2="606" y2="140"/>
<line class="vx-line" x1="682" y1="90" x2="682" y2="140"/>
<line class="vx-line" x1="378" y1="90" x2="682" y2="140"/>
<line class="vx-line" x1="150" y1="170" x2="150" y2="220"/>
<line class="vx-line" x1="302" y1="170" x2="150" y2="220"/>
<line class="vx-line" x1="226" y1="170" x2="226" y2="220"/>
<line class="vx-line" x1="378" y1="170" x2="226" y2="220"/>
<line class="vx-line" x1="302" y1="170" x2="302" y2="220"/>
<line class="vx-line" x1="150" y1="170" x2="302" y2="220"/>
<line class="vx-line" x1="378" y1="170" x2="378" y2="220"/>
<line class="vx-line" x1="226" y1="170" x2="378" y2="220"/>
<line class="vx-line" x1="454" y1="170" x2="454" y2="220"/>
<line class="vx-line" x1="606" y1="170" x2="454" y2="220"/>
<line class="vx-line" x1="530" y1="170" x2="530" y2="220"/>
<line class="vx-line" x1="682" y1="170" x2="530" y2="220"/>
<line class="vx-line" x1="606" y1="170" x2="606" y2="220"/>
<line class="vx-line" x1="454" y1="170" x2="606" y2="220"/>
<line class="vx-line" x1="682" y1="170" x2="682" y2="220"/>
<line class="vx-line" x1="530" y1="170" x2="682" y2="220"/>
<line class="vx-line" x1="150" y1="250" x2="150" y2="300"/>
<line class="vx-line" x1="226" y1="250" x2="150" y2="300"/>
<line class="vx-line" x1="226" y1="250" x2="226" y2="300"/>
<line class="vx-line" x1="150" y1="250" x2="226" y2="300"/>
<line class="vx-line" x1="302" y1="250" x2="302" y2="300"/>
<line class="vx-line" x1="378" y1="250" x2="302" y2="300"/>
<line class="vx-line" x1="378" y1="250" x2="378" y2="300"/>
<line class="vx-line" x1="302" y1="250" x2="378" y2="300"/>
<line class="vx-line" x1="454" y1="250" x2="454" y2="300"/>
<line class="vx-line" x1="530" y1="250" x2="454" y2="300"/>
<line class="vx-line" x1="530" y1="250" x2="530" y2="300"/>
<line class="vx-line" x1="454" y1="250" x2="530" y2="300"/>
<line class="vx-line" x1="606" y1="250" x2="606" y2="300"/>
<line class="vx-line" x1="682" y1="250" x2="606" y2="300"/>
<line class="vx-line" x1="682" y1="250" x2="682" y2="300"/>
<line class="vx-line" x1="606" y1="250" x2="682" y2="300"/>
<rect class="vx-box" x="125.0" y="60" width="50" height="30" rx="4"/>
<text class="vx-text" x="150" y="80" text-anchor="middle">1</text>
<rect class="vx-box" x="201.0" y="60" width="50" height="30" rx="4"/>
<text class="vx-text" x="226" y="80" text-anchor="middle">2</text>
<rect class="vx-box" x="277.0" y="60" width="50" height="30" rx="4"/>
<text class="vx-text" x="302" y="80" text-anchor="middle">3</text>
<rect class="vx-box" x="353.0" y="60" width="50" height="30" rx="4"/>
<text class="vx-text" x="378" y="80" text-anchor="middle">4</text>
<rect class="vx-box" x="429.0" y="60" width="50" height="30" rx="4"/>
<text class="vx-text" x="454" y="80" text-anchor="middle">5</text>
<rect class="vx-box" x="505.0" y="60" width="50" height="30" rx="4"/>
<text class="vx-text" x="530" y="80" text-anchor="middle">6</text>
<rect class="vx-box" x="581.0" y="60" width="50" height="30" rx="4"/>
<text class="vx-text" x="606" y="80" text-anchor="middle">7</text>
<rect class="vx-box" x="657.0" y="60" width="50" height="30" rx="4"/>
<text class="vx-text" x="682" y="80" text-anchor="middle">8</text>
<rect class="vx-box" x="125.0" y="140" width="50" height="30" rx="4"/>
<text class="vx-text" x="150" y="160" text-anchor="middle">6</text>
<rect class="vx-box" x="201.0" y="140" width="50" height="30" rx="4"/>
<text class="vx-text" x="226" y="160" text-anchor="middle">8</text>
<rect class="vx-box" x="277.0" y="140" width="50" height="30" rx="4"/>
<text class="vx-text" x="302" y="160" text-anchor="middle">10</text>
<rect class="vx-box" x="353.0" y="140" width="50" height="30" rx="4"/>
<text class="vx-text" x="378" y="160" text-anchor="middle">12</text>
<rect class="vx-box" x="429.0" y="140" width="50" height="30" rx="4"/>
<text class="vx-text" x="454" y="160" text-anchor="middle">6</text>
<rect class="vx-box" x="505.0" y="140" width="50" height="30" rx="4"/>
<text class="vx-text" x="530" y="160" text-anchor="middle">8</text>
<rect class="vx-box" x="581.0" y="140" width="50" height="30" rx="4"/>
<text class="vx-text" x="606" y="160" text-anchor="middle">10</text>
<rect class="vx-box" x="657.0" y="140" width="50" height="30" rx="4"/>
<text class="vx-text" x="682" y="160" text-anchor="middle">12</text>
<rect class="vx-box" x="125.0" y="220" width="50" height="30" rx="4"/>
<text class="vx-text" x="150" y="240" text-anchor="middle">16</text>
<rect class="vx-box" x="201.0" y="220" width="50" height="30" rx="4"/>
<text class="vx-text" x="226" y="240" text-anchor="middle">20</text>
<rect class="vx-box" x="277.0" y="220" width="50" height="30" rx="4"/>
<text class="vx-text" x="302" y="240" text-anchor="middle">16</text>
<rect class="vx-box" x="353.0" y="220" width="50" height="30" rx="4"/>
<text class="vx-text" x="378" y="240" text-anchor="middle">20</text>
<rect class="vx-box" x="429.0" y="220" width="50" height="30" rx="4"/>
<text class="vx-text" x="454" y="240" text-anchor="middle">16</text>
<rect class="vx-box" x="505.0" y="220" width="50" height="30" rx="4"/>
<text class="vx-text" x="530" y="240" text-anchor="middle">20</text>
<rect class="vx-box" x="581.0" y="220" width="50" height="30" rx="4"/>
<text class="vx-text" x="606" y="240" text-anchor="middle">16</text>
<rect class="vx-box" x="657.0" y="220" width="50" height="30" rx="4"/>
<text class="vx-text" x="682" y="240" text-anchor="middle">20</text>
<rect class="vx-box-accent" x="125.0" y="300" width="50" height="30" rx="4"/>
<text class="vx-text" x="150" y="320" text-anchor="middle">36</text>
<rect class="vx-box-accent" x="201.0" y="300" width="50" height="30" rx="4"/>
<text class="vx-text" x="226" y="320" text-anchor="middle">36</text>
<rect class="vx-box-accent" x="277.0" y="300" width="50" height="30" rx="4"/>
<text class="vx-text" x="302" y="320" text-anchor="middle">36</text>
<rect class="vx-box-accent" x="353.0" y="300" width="50" height="30" rx="4"/>
<text class="vx-text" x="378" y="320" text-anchor="middle">36</text>
<rect class="vx-box-accent" x="429.0" y="300" width="50" height="30" rx="4"/>
<text class="vx-text" x="454" y="320" text-anchor="middle">36</text>
<rect class="vx-box-accent" x="505.0" y="300" width="50" height="30" rx="4"/>
<text class="vx-text" x="530" y="320" text-anchor="middle">36</text>
<rect class="vx-box-accent" x="581.0" y="300" width="50" height="30" rx="4"/>
<text class="vx-text" x="606" y="320" text-anchor="middle">36</text>
<rect class="vx-box-accent" x="657.0" y="300" width="50" height="30" rx="4"/>
<text class="vx-text" x="682" y="320" text-anchor="middle">36</text>
</svg>
<figcaption>Figure 3. A butterfly sum over an eight-lane warp. At each step, lane <em>i</em> adds the value of lane <em>i</em> XOR <em>mask</em> to its own. After the step with mask 4 each lane holds a sum of 2 values, after mask 2 of 4, after mask 1 of all 8, and every lane holds the same 36, so any lane can write the result. A 32-lane warp needs five steps: masks 16, 8, 4, 2 and 1.</figcaption>
</figure>

Other targets have the same operation under other names. Metal's `simd_shuffle_xor` returns the value of the lane whose SIMD lane ID is the caller's ID XOR a mask, and Metal also has `simd_sum`, which adds a value across the active threads of the SIMD-group and gives every one of them the result. The specification describes SIMD-group functions as sharing data without threadgroup memory and without a barrier.[^msl-simd] CUDA's hardware warp reductions, such as `__reduce_add_sync`, exist on compute capability 8.x and later, and only for integers.[^n5-wreduce]

MLIR's `gpu` dialect has `gpu.shuffle` with the modes `xor`, `up`, `down` and `idx`. It returns the value and a flag that says whether the value is valid: the flag is false in a lane outside the first `width` lanes, and for `up` and `down` when the lane to read from would fall outside them. `width` must be the same in every lane, and the first `width` lanes must be active.[^mlir-gpu] The third example is a 32-lane butterfly sum written with it. `mlir-opt` 18 parses, verifies and prints it; nothing runs.

--8<-- "includes/examples/gpu/g6-synchronization/warp_reduce.mlir.md"

## Convergence: why these operations cannot sit behind a split

A shuffle's result depends on which lanes execute it together: "the value of lane 16" means something only if lane 16 is executing the same shuffle. A barrier's meaning depends on the same thing. LLVM calls such an operation **convergent** and defines it as communication between threads, outside the memory model, where "the set of threads which participate in communication is implicitly affected by control flow."[^l5] An `if` on the thread's index changes that set.

That is the reason behind the barrier rule above, and it binds the compiler as much as the programmer. Consider a warp that sums its positive values with a warp-level sum in one arm of `if v > 0`, and its negative values with another warp-level sum in the `else` arm. Each sum adds only the lanes that took its arm. A compiler that sees the same call in both arms and hoists it above the `if`, which is legal for an ordinary function call, would make every lane join one sum and change both results.[^l5]

LLVM's documentation therefore forbids hoisting or sinking a convergent operation in general, and allows it across a branch that it can prove uniform, since then every thread of the group takes the same side anyway.[^l5] Atomics are not convergent: they communicate through memory, under the memory model, and their result does not depend on which threads execute them together. The same documentation adds that being convergent implies no memory ordering and no barrier, and that threads executing a convergent operation together need not do so at the same moment.[^l5] [G9](g9-gpu-compilers-in-llvm.md#convergent-operations-what-an-optimizer-must-not-move) shows how LLVM marks convergent calls so that passes written for CPUs leave them alone.

## Reductions: the same total, different bits

A **reduction** combines many values into one with an operation such as a sum, a product, a minimum or a maximum. Run in parallel, a reduction must choose a **grouping**: which values are combined first, and in what order the partial results meet. For a maximum, or an integer sum that does not overflow, the grouping does not matter. For a floating-point sum it can, and four values are enough to see it. Take 2^24, 1, −2^24 and 1, whose exact sum is 2.

<figure class="vx-figure">
<svg viewBox="0 0 720 262" role="img" aria-label="Two groupings of the values 2 to the 24, 1, minus 2 to the 24 and 1; one gives 2, the other 1" aria-describedby="g6-f4-desc">
<title id="g6-f4-title">Two groupings of the same four values</title>
<desc id="g6-f4-desc">Two trees built on the same four leaves: 2 to the 24, 1, minus 2 to the 24, 1. Left, stride pairing adds the first and third leaves to get 0, and the second and fourth to get 2, then adds those to get 2, the exact sum. Right, neighbour pairing adds the first two leaves, where 2 to the 24 plus 1 rounds back to 2 to the 24 and the 1 is lost, and the last two to get minus 2 to the 24 plus 1, then adds those to get 1.</desc>
<text class="vx-text-muted" x="180" y="20" text-anchor="middle">stride pairing: a0 + a2, a1 + a3</text>
<text class="vx-text-muted" x="540" y="20" text-anchor="middle">neighbour pairing: a0 + a1, a2 + a3</text>
<g class="vx-seq" style="--vx-i: 0; --vx-n: 3">
<rect class="vx-box" x="28.0" y="190" width="64" height="34" rx="4"/>
<text class="vx-text" x="60" y="212" text-anchor="middle">2²⁴</text>
<rect class="vx-box" x="108.0" y="190" width="64" height="34" rx="4"/>
<text class="vx-text" x="140" y="212" text-anchor="middle">1</text>
<rect class="vx-box" x="188.0" y="190" width="64" height="34" rx="4"/>
<text class="vx-text" x="220" y="212" text-anchor="middle">−2²⁴</text>
<rect class="vx-box" x="268.0" y="190" width="64" height="34" rx="4"/>
<text class="vx-text" x="300" y="212" text-anchor="middle">1</text>
<rect class="vx-box" x="388.0" y="190" width="64" height="34" rx="4"/>
<text class="vx-text" x="420" y="212" text-anchor="middle">2²⁴</text>
<rect class="vx-box" x="468.0" y="190" width="64" height="34" rx="4"/>
<text class="vx-text" x="500" y="212" text-anchor="middle">1</text>
<rect class="vx-box" x="548.0" y="190" width="64" height="34" rx="4"/>
<text class="vx-text" x="580" y="212" text-anchor="middle">−2²⁴</text>
<rect class="vx-box" x="628.0" y="190" width="64" height="34" rx="4"/>
<text class="vx-text" x="660" y="212" text-anchor="middle">1</text>
</g>
<g class="vx-seq" style="--vx-i: 1; --vx-n: 3">
<line class="vx-line" x1="60" y1="190" x2="140" y2="144"/>
<line class="vx-line" x1="220" y1="190" x2="140" y2="144"/>
<rect class="vx-box" x="108.0" y="110" width="64" height="34" rx="4"/>
<text class="vx-text" x="140" y="132" text-anchor="middle">0</text>
<line class="vx-line" x1="140" y1="190" x2="220" y2="144"/>
<line class="vx-line" x1="300" y1="190" x2="220" y2="144"/>
<rect class="vx-box" x="188.0" y="110" width="64" height="34" rx="4"/>
<text class="vx-text" x="220" y="132" text-anchor="middle">2</text>
<line class="vx-line" x1="420" y1="190" x2="460" y2="144"/>
<line class="vx-line" x1="500" y1="190" x2="460" y2="144"/>
<rect class="vx-box-bad" x="428.0" y="110" width="64" height="34" rx="4"/>
<text class="vx-text" x="460" y="132" text-anchor="middle">2²⁴</text>
<line class="vx-line" x1="580" y1="190" x2="620" y2="144"/>
<line class="vx-line" x1="660" y1="190" x2="620" y2="144"/>
<rect class="vx-box" x="572.0" y="110" width="96" height="34" rx="4"/>
<text class="vx-text" x="620" y="132" text-anchor="middle">−2²⁴ + 1</text>
</g>
<g class="vx-seq" style="--vx-i: 2; --vx-n: 3">
<line class="vx-line" x1="140" y1="110" x2="180" y2="68"/>
<line class="vx-line" x1="220" y1="110" x2="180" y2="68"/>
<rect class="vx-box-accent" x="148.0" y="34" width="64" height="34" rx="4"/>
<text class="vx-text" x="180" y="56" text-anchor="middle">2</text>
<line class="vx-line" x1="460" y1="110" x2="540" y2="68"/>
<line class="vx-line" x1="620" y1="110" x2="540" y2="68"/>
<rect class="vx-box-accent" x="508.0" y="34" width="64" height="34" rx="4"/>
<text class="vx-text" x="540" y="56" text-anchor="middle">1</text>
<text class="vx-text-accent" x="180" y="250" text-anchor="middle">every sum exact: total 2</text>
<text class="vx-text-accent" x="540" y="250" text-anchor="middle">2²⁴ + 1 rounds back to 2²⁴: total 1</text>
</g>
</svg>
<figcaption>Figure 4. The same four values under two groupings. Stride pairing, the grouping of a shared-memory tree or a butterfly, cancels the large values first, and every addition is exact. Neighbour pairing, the grouping of splitting the array into contiguous pieces, adds 1 to 2<sup>24</sup> first, where the result rounds back to 2<sup>24</sup>. Every addition on both sides is one correctly rounded IEEE 754 operation.</figcaption>
</figure>

Follow the right-hand tree by hand. 2^24 + 1 rounds to 2^24, as in the atomic example. −2^24 + 1 = −16,777,215 is representable, so it is exact. Their sum is 1. On the left, 2^24 + (−2^24) = 0 and 1 + 1 = 2, both exact, so the total is 2. A single thread adding left to right gets 2^24, then 2^24 again, then 0, then 1: the same answer as the right-hand tree.

The fourth example scales this to one warp: 32 values, with 2^24 at index 0, −2^24 at index 17 and 1 everywhere else, so the exact sum is 30. It sums them four ways. **Sequential** is one running total from left to right. **Tree** pairs element `i` with element `i + stride` for strides 16, 8, 4, 2 and 1, the pattern of a shared-memory reduction. **Butterfly** simulates the 32 lanes of Figure 3 with masks 16 down to 1. **Segmented** sums four groups of eight left to right, then adds the four partial sums in order.

--8<-- "includes/examples/gpu/g6-synchronization/reduction_orders.cpp.md"

No method gets 30. The sequential sum loses sixteen 1s, the tree one, the segmented sum seven. The tree and the butterfly agree bit for bit. In the first step both add element `i` to element `i + 16` (the butterfly also adds `i + 16` to `i`, which is the same IEEE addition, since it is commutative). The later steps pair the same partial sums again, so the two methods perform the same additions on the same operands. Moving values through registers instead of shared memory changes nothing.

??? check "Why does the sequential sum lose sixteen of the 1s, while the tree loses only one?"

    In the sequential sum the running total sits at 2<sup>24</sup> while elements 1 to 16 arrive, and each 1 added to 2<sup>24</sup> rounds away; only after element 17 cancels the large value do the remaining fourteen 1s count. In the tree, 2<sup>24</sup> meets a single 1 in the first step (element 16), which is lost. After that the large partial sum meets 2, 4 and 8, and 2<sup>24</sup> plus a small even number is representable, so those additions are exact, and so is the last one, where the large values cancel.

A block of more than one warp needs both tools. The fifth example sums 64 values with a block of two warps. Each thread stores its value in workgroup memory (MLIR's name for shared memory), and the block waits at `gpu.barrier`. Then warp 0 adds element `t` and element `t + 32`, the one round that combines values from different warps, and finishes with `gpu.subgroup_reduce`, a sum across its 32 lanes.[^mlir-gpu]

--8<-- "includes/examples/gpu/g6-synchronization/block_reduce.mlir.md"

Look at where each operation sits. The barrier comes before the `if`, where all 64 threads reach it. The `if t < 32` splits the block, so a barrier inside it would break the rule, but it does not split a warp: warp 0 takes it whole and warp 1 skips it whole. The branch is uniform within each warp and divergent within the block, which is why a warp-level operation may sit inside it and a block-level one may not.

The example also shows what a compiler gives away when it uses a high-level reduction. The documentation of `gpu.subgroup_reduce` and `gpu.all_reduce` says what is combined and that every lane gets the result, not in which order the values are combined.[^mlir-gpu] Metal's `simd_sum` is described the same way.[^msl-simd] For an integer sum that is enough. For a floating-point sum, the bits depend on however each target lowers the operation, so a compiler that must promise the bits has to emit the grouping itself, as the butterfly does.

## Across blocks: one launch or two

A reduction over a whole array needs one more level, because each block's partial sum lives in that block. The Programming Guide is direct about it: within a grid there is no mechanism to synchronize all threads.[^n2-atomics] The scheduler assigns blocks to multiprocessors in an order that programs can neither control nor rely on, and a grid may have more blocks than can be resident at once, so a block waiting for another block that has not started yet could wait forever.[^n2-launch] There are four standard ways out.

- **Two launches.** The first kernel writes one partial sum per block to global memory; a second kernel adds them. Kernels issued to one CUDA stream run in the order they were issued, and an operation cannot overtake an earlier one, so the second kernel sees every partial sum.[^streams] The grouping is whatever the second kernel does, fixed by the program.
- **One atomic per block.** Each block adds its partial sum to the total with `atomicAdd`. This is the Programming Guide's own example.[^n2-atomics] It is short and needs one launch, and the blocks arrive in the scheduler's order, so a `float` total can change from run to run.
- **The last block finishes.** Each block writes its partial sum to an array, executes a device-scope fence, and increments a counter with an atomic. The block that receives the counter's final value knows every other block has finished, reads the whole array and adds it up.[^n5-fence] The atomic only decides which block does the final sum; it does not decide the order of the additions. If the last block adds the partial sums in index order, the result has the same bits on every run.
- **A grid-wide barrier.** Cooperative groups can synchronize an entire grid, but only in a kernel started with `cudaLaunchCooperativeKernel`, which either launches all of the grid's blocks or fails.[^cg] A launch that must start every block together can be no larger than the number of blocks the GPU can hold at once, the **resident blocks** that [G5](g5-occupancy.md) counts.

??? check "In the last-block pattern the blocks finish in an unpredictable order, and an atomic counter detects the last one. Why can the final sum still be reproducible, and what change to the kernel would make it vary from run to run?"

    The unpredictable order decides only which block runs the final loop. That loop reads the partial sums from an array indexed by block number and can add them in index order, so the grouping of the floating-point additions is the same whichever block runs it. The sum would vary if the blocks added their partial sums into one total as they finished, for example with a `float` atomic add, because then the arrival order would become the order of the additions.

## Choosing a reduction Vortex can promise

Every method above computes a valid sum, correctly rounded at every step. The question for Vortex is what it promises. [Decision 56](../decisions/numbers.md#d56) already answers part of it: an implementation must not reassociate or reorder floating-point operations. So a Vortex loop that accumulates an `f32` sum must produce the bits of a left-to-right sum, whatever hardware runs it.

```vortex
// items: valid
fn total(values: &[f32; 32]) -> f32 {
    let mut sum: f32 = 0.0;
    for i in 0..32 {
        sum += values[i];
    }
    return sum;
}
```

Given the 32 values of the fourth example, `total` must return 14, the sequential result, on every conforming implementation. A GPU compiler that turned this loop into a tree would return 29, which is closer to the exact answer and still wrong for Vortex. Integers are not free either. Vortex [checks integer overflow](../specification/expressions.md#checked-integer-operations), so the order of an `i32` sum can decide whether it fails: summing 2,147,483,647, 1 and −1 left to right fails at the first addition, while adding 1 and −1 first does not.

The [safety philosophy](../philosophy.md#safety-philosophy) asks for exactly this care: floating-point reassociation and "non-deterministic parallel reductions" must have documented behavior, and transformations that can change observable results should require an explicit language mode or the programmer's permission. A parallel reduction therefore needs a language-level promise. There are four candidates, and they cost different things.

- **Source order only.** The status quo: reductions over floating-point values stay sequential. Correct and simple, and it gives up the parallel speed that GPUs exist for.
- **A documented fixed grouping.** The language defines one grouping, such as a tree over the element indices, and every implementation computes it. The [performance philosophy](../philosophy.md#performance-philosophy) adds a constraint: auto-tuning must not change the observable meaning of a program. A grouping that followed the block size would let a tuner change the bits by choosing a block size, so the grouping must be defined by the data's shape, not by the launch.
- **An order-independent sum.** ReproBLAS, from Demmel and Nguyen's reproducible summation work, gives the same bits for any order, number of processors or data partitioning, using a special accumulator that its authors size at six doubles by default.[^reproblas][^dn13] The cost is extra arithmetic: about 9n floating-point operations to sum n values, which the project reports as a 4× slowdown for a dot product on one Sandy Bridge core, and less than 1.2× for a sum of a million doubles over more than 512 Ivy Bridge cores.[^reproblas]
- **Opt-in non-determinism.** An explicit mode in which the programmer accepts whatever order the hardware produces, in exchange for atomics and vendor reductions.

One hazard is ruled out already. A reduction's inputs and its output must not overlap, or a partial result could overwrite an input another thread has not read yet. [Decision 25](../decisions/references.md#d25) forbids a variable lent as `&mut` from appearing in any other argument of the same call, so a call to a function such as `fn total_into(values: &[f32; 32], out: &mut f32)` cannot hand it the same array as both the input and the place for the output.

## Measuring it

No GPU timings are claimed here. Collect your own on the M4 Pro, where compiling Metal Shading Language source text at run time worked without the offline Metal toolchain when the research for these chapters tried it (2026-09-23):

1. Fill an array of 2^20 `f32` values from a fixed formula that mixes large and small magnitudes, so that rounding matters (the fourth example's pattern, repeated, works).
2. Write four kernels: one `atomic_fetch_add_explicit` on an `atomic_float` in device memory per element; a threadgroup tree with one atomic per threadgroup; the same tree with the partial sums written to an array and added in index order by a second dispatch; and a version that uses `simd_sum` within each SIMD-group.
3. Run each kernel 100 times on the same input and count the distinct bit patterns of the result. Compare each against a sequential sum on the CPU.
4. Time the runs and report the median with its spread, following [P1](../optimize/p1-measure-first.md).

| Kernel | Distinct results in 100 runs | Equal to the CPU sequential sum? | Median time | Spread |
| --- | --- | --- | --- | --- |
| atomic per element | | | | |
| tree, atomic per threadgroup | | | | |
| tree, two dispatches | | | | |
| `simd_sum`, two dispatches | | | | |

Record the machine, the operating system, the date and the threadgroup size with the table, then change the threadgroup size and run again: the two-dispatch versions may give new bits, but the same bits on every run.

## For Vortex

!!! vortex "Exercise"

    **Build** a reduction report in your compiler, and a written rule for what a Vortex reduction may do.

    1. A recognizer over your IR for loops that reduce: a scalar accumulator, initialized before the loop, updated once per iteration by `+`, `*`, a minimum or a maximum of itself and a value that does not depend on it, and not read anywhere else in the loop.
    2. For every reduction it finds, a remark in the style of the [sixth principle](../philosophy.md#6-explain-performance-decisions) naming the accumulator, its type, its operator and whether your current rules allow regrouping it, with the rule that decides ([decision 56](../decisions/numbers.md#d56) for floating point, [checked integer operations](../specification/expressions.md#checked-integer-operations) for integers).
    3. A written design, using the [feature decision worksheet](../philosophy.md#feature-decision-worksheet), for a parallel reduction in Vortex: which of the four candidates above it adopts, how a program asks for it, what its result is on the fourth example's 32 values, and how that result stays the same when a tuner changes the launch shape.

    **Not yet:** generating parallel code for a reduction, on the CPU ([P13](../optimize/p13-multithreading.md)) or the GPU ([G10](g10-matmul-ladder.md), [M12](../mlir/m12-vortex-gpu-path.md)); barriers, atomics or shuffles in the language; and any change to decision 56.

    **Proof that it works:**

    - Golden tests: `total` above, on the fourth example's 32 values, returns 14 (bit pattern `0x41600000`), and the report says the loop is kept in source order.
    - The stage 10 kernel's `k` loop ([G4](g4-memory-performance.md#which-index-runs-across-the-warp)) is reported as an `f32` sum reduction; a loop that stores the running total into an array on each iteration is not reported, because the accumulator is read elsewhere.
    - An `i32` sum of 2,147,483,647, 1 and −1 in source order fails with a runtime error at the first addition, and a test proves that no pass in your pipeline regroups it.
    - The worksheet's answers are consistent: if question 5 forbids non-deterministic reductions by default, question 8 names the test proving the opt-in mode is the only way to reach one, and question 2 states the exact bits your chosen grouping produces for the 32 values.

## Key ideas

!!! recap "Questions you can now answer"

    - **What does a barrier guarantee, and to whom?** Every thread of its group arrives before any continues, and memory accesses made before it are visible after it, to the threads of that group only.
    - **Why must every thread of a block reach the same barrier?** The barrier waits for all of them; a barrier behind a branch that splits the block can hang the kernel, and the specifications leave that case undefined.
    - **What does an atomic guarantee, and what does it leave open?** One indivisible read-modify-write, so no update is lost; the order in which different threads' atomics apply is up to the hardware.
    - **Why can a shuffle not be moved across a branch on the lane's index?** It is convergent: its result depends on which lanes execute it together, and such a branch changes that set.
    - **Can two correct reductions of the same numbers give different bits?** Yes, for floating-point values: different groupings round at different points, while integer sums agree in every order as long as nothing overflows.
    - **How can a grid-wide sum be reproducible?** Store one partial sum per block and add them in a fixed order, in a second launch or in the last block, instead of in arrival order.
    - **What must Vortex decide before a reduction can run in parallel?** Which grouping it promises, independent of the launch shape, or an explicit opt-in to non-deterministic order; decision 56 today requires source order.

## Where this comes back

!!! next "You will use this again in"

    - [G7. Programming models tour](g7-programming-models.md): *barrier*, *atomic*, *shuffle* under each model's names
    - [G9. GPU compilers inside LLVM](g9-gpu-compilers-in-llvm.md): *convergent operation*, *uniform branch*
    - [G10. The GPU matmul ladder](g10-matmul-ladder.md): *barrier*, *grouping* of the `k` sum across tiles
    - [G12. Fusion case study: FlashAttention](g12-flashattention.md): *reduction*, *grouping*
    - [G14. Measuring GPU code](g14-measuring-gpu-code.md): *atomic contention*, *distinct results per run*
    - [M10. MLIR for GPUs](../mlir/m10-mlir-for-gpus.md): *`gpu.barrier`*, *`gpu.shuffle`*, *`gpu.subgroup_reduce`*
    - [M12. Designing Vortex's GPU path](../mlir/m12-vortex-gpu-path.md): *documented reduction grouping*, *memory scope*

## Sources and further reading

Read the Programming Guide's synchronization and atomic functions first, then its warp shuffle functions and the constraints on the `_sync` intrinsics, then LLVM's convergent operation semantics, which explains why a compiler has to treat the first three with care.

[^n2-atomics]: NVIDIA, "CUDA Programming Guide", v13.4.2, section 2.3.5, "Atomics", including 2.3.5.2, "Memory Atomics in Python". <https://docs.nvidia.com/cuda/cuda-programming-guide/02-basics/writing-cuda-kernels.html#atomics>
[^n2-launch]: NVIDIA, "CUDA Programming Guide", v13.4.2, section 2.3.7, "Kernel Launch and Occupancy". <https://docs.nvidia.com/cuda/cuda-programming-guide/02-basics/writing-cuda-kernels.html#kernel-launch-and-occupancy>
[^streams]: NVIDIA, "CUDA Programming Guide", v13.4.2, section 2.5.5, "CUDA Stream Ordering". <https://docs.nvidia.com/cuda/cuda-programming-guide/02-basics/asynchronous-execution.html#cuda-stream-ordering>
[^n3-its]: NVIDIA, "CUDA Programming Guide", v13.4.2, section 3.2.2.1.1, "Independent Thread Scheduling", and the notes that follow it. <https://docs.nvidia.com/cuda/cuda-programming-guide/03-advanced/advanced-kernel-programming.html#independent-thread-scheduling>
[^n3-scopes]: NVIDIA, "CUDA Programming Guide", v13.4.2, section 3.2.3, "Thread Scopes". <https://docs.nvidia.com/cuda/cuda-programming-guide/03-advanced/advanced-kernel-programming.html#thread-scopes>
[^n3-scoped]: NVIDIA, "CUDA Programming Guide", v13.4.2, section 3.2.4.1, "Scoped Atomics", with 3.2.4.1.2, "Performance Considerations". <https://docs.nvidia.com/cuda/cuda-programming-guide/03-advanced/advanced-kernel-programming.html#scoped-atomics>
[^cg]: NVIDIA, "CUDA Programming Guide", v13.4.2, sections 4.4.5.1, "Sync", and 4.4.8, "Large Scale Groups". <https://docs.nvidia.com/cuda/cuda-programming-guide/04-special-topics/cooperative-groups.html#large-scale-groups>
[^n5-sync]: NVIDIA, "CUDA Programming Guide", v13.4.2, sections 5.4.4.1, "Thread Block Synchronization Functions", and 5.4.4.2, "Warp Synchronization Function". <https://docs.nvidia.com/cuda/cuda-programming-guide/05-appendices/cpp-language-extensions.html#thread-block-synchronization-functions>
[^n5-fence]: NVIDIA, "CUDA Programming Guide", v13.4.2, section 5.4.4.3, "Memory Fence Functions", including its single-kernel sum example. <https://docs.nvidia.com/cuda/cuda-programming-guide/05-appendices/cpp-language-extensions.html#memory-fence-functions>
[^n5-atomics]: NVIDIA, "CUDA Programming Guide", v13.4.2, sections 5.4.5, "Atomic Functions", 5.4.5.1, "Legacy Atomic Functions", and 5.4.5.1.1, "atomicAdd()". <https://docs.nvidia.com/cuda/cuda-programming-guide/05-appendices/cpp-language-extensions.html#atomic-functions>
[^n5-wreduce]: NVIDIA, "CUDA Programming Guide", v13.4.2, section 5.4.6.4, "Warp Reduce Functions". <https://docs.nvidia.com/cuda/cuda-programming-guide/05-appendices/cpp-language-extensions.html#warp-reduce-functions>
[^n5-shfl]: NVIDIA, "CUDA Programming Guide", v13.4.2, sections 5.4.6.5, "Warp Shuffle Functions", and 5.4.6.6, "Warp __sync Intrinsic Constraints". <https://docs.nvidia.com/cuda/cuda-programming-guide/05-appendices/cpp-language-extensions.html#warp-shuffle-functions>
[^msl-new]: Apple, "Metal Shading Language Specification", version 4.1, 2026, section 1.3, "New in Metal 4.1". <https://developer.apple.com/metal/Metal-Shading-Language-Specification.pdf>
[^msl-sync]: Apple, "Metal Shading Language Specification", version 4.1, 2026, section 6.10.1, "Threadgroup and SIMD-Group Synchronization Functions". <https://developer.apple.com/metal/Metal-Shading-Language-Specification.pdf>
[^msl-simd]: Apple, "Metal Shading Language Specification", version 4.1, 2026, section 6.10.2, "SIMD-Group Functions": the introduction, `simd_shuffle_xor` and `simd_sum`. <https://developer.apple.com/metal/Metal-Shading-Language-Specification.pdf>
[^msl-atomics]: Apple, "Metal Shading Language Specification", version 4.1, 2026, sections 6.16.1, "Memory Order", and 6.16.4, "Atomic Functions". <https://developer.apple.com/metal/Metal-Shading-Language-Specification.pdf>
[^l5]: LLVM Project, "Convergent Operation Semantics": the sections "Overview", "Reductions inside divergent control flow", "Memory Model Non-Interaction" and "Hoisting and sinking". <https://llvm.org/docs/ConvergentOperations.html>
[^mlir-gpu]: MLIR Project, "'gpu' Dialect": the operations `gpu.barrier`, `gpu.shuffle`, `gpu.subgroup_reduce` and `gpu.all_reduce`, and the section "Memory attribution". <https://mlir.llvm.org/docs/Dialects/GPU/>
[^reproblas]: Willow Ahrens, Hong Diep Nguyen and James Demmel, "ReproBLAS: Reproducible Basic Linear Algebra Sub-programs", project page, University of California, Berkeley: the sections "What are ReproBLAS?", "Main Goals" and "Performance". <https://bebop.cs.berkeley.edu/reproblas/>
[^dn13]: James Demmel and Hong Diep Nguyen, "Fast Reproducible Floating-Point Summation", *2013 IEEE 21st Symposium on Computer Arithmetic (ARITH)*, pp. 163-172, 2013. <https://doi.org/10.1109/ARITH.2013.9>

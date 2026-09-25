# G1. Throughput machines

<p class="page-intro">Two chips can spend the same transistor budget on making one thread fast or on running thousands of threads at once. This chapter gives the tool for telling which choice a piece of work rewards, Little's law, and connects it to the roofline bound from P3: the reason a GPU is built the way it is.</p>

<p class="vx-meta" markdown="1">Level: Foundations · Reading time: about 30 minutes · Builds on: [P3. The roofline model](../optimize/p3-roofline.md)</p>

???+ remember "Before you start, remember"

    ??? question "What is the roofline bound, and what two ceilings does it take the smaller of?"

        Attainable GFlop/s is at most the smaller of a machine's peak flops and its peak bandwidth times the kernel's operational intensity. Left of the ridge point the bound is set by bandwidth; right of it, by compute.

        Introduced in [P3. The roofline model](../optimize/p3-roofline.md#the-roofline-two-lines-and-a-ridge).

    ??? question "Why is the naive matmul kernel's flop count fixed at 524,288 for any schedule that computes the same sums, while its byte count is not?"

        The flop count follows only from the loop's trip counts, which no reordering changes. The byte count depends on how many times each value is re-read from DRAM before it is reused, which is exactly what a schedule (naive, tiled, or anything between) is free to change.

        Introduced in [P3. The roofline model](../optimize/p3-roofline.md#operational-intensity-flops-per-byte-of-dram-traffic).

    ??? question "What does marking Vortex's `&mut c` parameter noalias let a compiler skip, and what rule of the language backs it?"

        The runtime check, and the extra copies of a loop, that a compiler must otherwise keep in case `c` overlaps `a` or `b`. References, decision 25, settles the question at compile time: storage behind a `&mut` parameter is reachable through no other parameter of the same call.

        Introduced in [P3. The roofline model](../optimize/p3-roofline.md#key-ideas), from [Decision 25](../decisions/references.md#d25).

    ??? question "In what order are the elements of a `[f32; 64, 64]` array stored?"

        Row after row, the last index varying fastest: `m[i, j + 1]` sits next to `m[i, j]`, and `m[i + 1, j]` is a whole row away.

        Introduced in [Arrays and shapes, decision 43](../decisions/arrays.md#d43).

!!! goals "In this chapter"

    - Tell latency and throughput apart as two different kinds of quantity, a time and a rate, rather than two names for the same thing.
    - State Little's law for a pipelined resource, and use it to compute how much independent work has to be in flight before that resource runs at its full rate.
    - Explain why a GPU spends its transistor budget on running many simple threads instead of a few fast ones, and name the two ways to supply the parallelism Little's law asks for.
    - Connect Little's law to the roofline bound from P3: both describe a rate that a machine cannot exceed, and hiding latency only lets a kernel reach that rate, never raise it.
    - Read the stage 10 matmul kernel's one-thread-per-output mapping and say what question Little's law asks about it.

## A budget spent two ways

Every processor is built from a fixed budget of transistors, and that budget buys, among other things, a certain amount of arithmetic capability and a certain amount of control logic: the circuitry that decides what to compute next, predicts which way a branch will go, and keeps several instructions in flight against each other's dependencies. Two designs spend that budget very differently.

One design gives most of the budget to control logic and to a small number of cores, so that a single thread's chain of dependent instructions finishes as fast as possible: large caches to avoid waiting on memory, branch prediction to avoid waiting on a decision, and the machinery to run instructions out of their written order when an earlier one stalls. This is a **latency-optimized** design: it is built to make one thing finish sooner. A general-purpose CPU is the familiar example.

The other design gives most of the budget to arithmetic units and spends comparatively little on control logic per thread, accepting that any one thread runs no faster, and often slower, than it would on the first design. Instead it runs thousands of threads side by side, so that while one thread waits on a slow operation, another thread with independent work is ready to use the hardware that would otherwise sit idle. This is a **throughput-optimized** design: it is built to finish the most total work per second, not to make any one piece of it fast. A GPU is the example this book studies from here on: the CUDA Programming Guide frames a kernel from the start as a function meant to be launched across a whole grid of threads, not as a single thread's program that happens to be repeated.[^cuda-kernels]

Neither design is a mistake or an unfinished version of the other. Each is the right answer to a different question: does the work in front of you have one long chain of dependent steps that has to finish before anything else can happen, or does it have thousands of independent pieces that could all run at the same time if something were watching all of them at once? The rest of this chapter makes that question precise.

## Latency and throughput are different numbers

Take a chip that can issue 8 independent arithmetic instructions per cycle, and where each individual instruction takes 24 cycles to go from being issued to its result being ready. These are two different measurements. **Latency** is a time: how long one operation takes to finish, here 24 cycles. **Throughput** is a rate: how many operations the hardware can complete per cycle once enough of them are ready, here 8. Confusing the two is an easy mistake, because on a chip with no independent work at all, the time between one result and the next looks like it should be the latency, 24 cycles; but that is only true when there is nothing else for the hardware to do in the meantime.

Volkov's 2010 talk on GPU performance measures exactly this pair for three NVIDIA streaming-multiprocessor generations. Multiplying the two together gives a third quantity: the number of independent operations that have to be ready, all at once, for the chip's throughput to be fully used.[^volkov-little]

| SM generation | Latency (cycles) | Throughput (independent ops/cycle) | Parallelism needed |
| --- | --- | --- | --- |
| G80-GT200 | 24 | 8 | 192 |
| GF100 | 18 | 32 | 576 |
| GF104 | 18 | 48 | 864 |

Two things are worth noticing before the formula gets a name. First, a faster chip is not always the one with lower latency: GF100 and GF104 have the *same* latency as each other, 18 cycles, and a shorter one than G80-GT200's 24, but the parallelism they need is far higher, because their throughput grew faster than their latency shrank. Second, the "parallelism needed" column is not a count of transistors or of anything the chip's designer chose directly: it is a consequence of the other two columns, computed the same way every time.

## Little's law: how much work has to be in flight

The relationship in that table's third column is a form of **Little's law**, a result from queueing theory: for a system in steady state, the number of items in the system equals the rate at which items arrive times the average time each one stays.[^volkov-little] Applied to one pipelined execution resource, with a fixed latency and a fixed peak throughput, it says:

$$\text{parallelism needed} = \text{latency} \times \text{throughput}$$

`little_law.cpp` computes exactly the table above from Volkov's own numbers, then asks a second question: fixing a thread count, how much independent work per thread (instruction-level parallelism, or **ILP**) closes whatever gap is left between that thread count and the parallelism the chip needs.

--8<-- "includes/examples/gpu/g1-throughput-machines/little_law.cpp.md"

At 192 threads and one independent instruction each, G80-GT200 is already fully supplied: 192 threads is exactly its parallelism requirement, no ILP needed. The same 192 threads leave GF100 three independent instructions short per thread, and GF104 five short. The number of threads a compiler or a programmer launches is one way to reach the target in Little's law's formula; it is never the only way, and a chip whose parallelism requirement outgrows a fixed thread count needs the other way instead.

<figure class="vx-figure">
<svg viewBox="0 0 760 400" role="img" aria-label="Two ways to keep an execution unit busy across a latency: one core taking each request in turn, against many lanes each holding one request at once" aria-describedby="g1-f1-desc">
<title id="g1-f1-title">Little's law: how much work has to be in flight</title>
<desc id="g1-f1-desc">Left panel: one latency-optimized core. A single token crosses a long box, taking L cycles, while three more requests wait behind it; only one is ever in progress. Right panel: eight throughput-optimized lanes, one cycle each, all lit at once with a token in every lane; the panel is labelled that L times R requests must be in flight for every lane to be busy on every cycle. A shared caption states the formula: parallelism equals latency times throughput.</desc>
<text class="vx-text" x="20" y="26">One core, taken in turn</text>
<text class="vx-text-muted" x="20" y="46">latency L cycles per request, throughput 1 request per L cycles</text>
<rect class="vx-box" x="20" y="64" width="24" height="24"/>
<rect class="vx-box" x="20" y="94" width="24" height="24"/>
<rect class="vx-box" x="20" y="124" width="24" height="24"/>
<text class="vx-text-muted" x="52" y="112" text-anchor="start">waiting</text>
<rect class="vx-box-strong" x="120" y="70" width="220" height="70"/>
<text class="vx-mono" x="230" y="110" text-anchor="middle">core, L cycles</text>
<circle class="vx-dot vx-travel" cx="132" cy="105" r="7" style="--vx-distance:196px"/>
<line class="vx-line vx-arrowhead" x1="360" y1="105" x2="380" y2="105"/>
<text class="vx-text-muted" x="120" y="170">Only one request is ever in progress: the rest sit idle,</text>
<text class="vx-text-muted" x="120" y="188">however many wait behind it.</text>
<line class="vx-line" x1="380" y1="10" x2="380" y2="390"/>
<text class="vx-text" x="410" y="26">Many lanes, one cycle each</text>
<text class="vx-text-muted" x="410" y="46">throughput R lanes per cycle, one request retires from each every cycle</text>
<rect class="vx-box-accent vx-seq" x="410" y="60" width="300" height="16" style="--vx-i:0; --vx-n:8"/>
<rect class="vx-box-accent vx-seq" x="410" y="80" width="300" height="16" style="--vx-i:1; --vx-n:8"/>
<rect class="vx-box-accent vx-seq" x="410" y="100" width="300" height="16" style="--vx-i:2; --vx-n:8"/>
<rect class="vx-box-accent vx-seq" x="410" y="120" width="300" height="16" style="--vx-i:3; --vx-n:8"/>
<rect class="vx-box-accent vx-seq" x="410" y="140" width="300" height="16" style="--vx-i:4; --vx-n:8"/>
<rect class="vx-box-accent vx-seq" x="410" y="160" width="300" height="16" style="--vx-i:5; --vx-n:8"/>
<rect class="vx-box-accent vx-seq" x="410" y="180" width="300" height="16" style="--vx-i:6; --vx-n:8"/>
<rect class="vx-box-accent vx-seq" x="410" y="200" width="300" height="16" style="--vx-i:7; --vx-n:8"/>
<text class="vx-mono" x="560" y="234" text-anchor="middle">8 lanes</text>
<text class="vx-text-muted" x="410" y="264">Every lane is one request from a different point in its own L-cycle</text>
<text class="vx-text-muted" x="410" y="282">latency: L of them must overlap per lane for the lane never to stall.</text>
<rect class="vx-box" x="60" y="330" width="640" height="50"/>
<text class="vx-mono" x="380" y="360" text-anchor="middle">parallelism in flight = latency (cycles) &times; throughput (per cycle)</text>
</svg>
<figcaption>Figure 1. Little's law applied to one execution unit. Left, a single core can only ever have one request in progress, so throughput is capped at one request every L cycles no matter how many requests are queued. Right, a unit that can issue R independent requests per cycle needs L &times; R of them in flight, not R, before every cycle actually retires R results; fewer in flight and some lanes sit idle waiting for their own request to finish. The two GPU examples on this page fill in L and R with Volkov's own measurements.[^volkov-little]</figcaption>
</figure>

??? check "A chip's latency is 20 cycles and its throughput is 40 independent operations per cycle. How many independent operations must be in flight to reach full throughput, and what happens at half that number?"

    800 (20 &times; 40). At 400 in flight, Little's law's ramp is linear below the ceiling, so the chip reaches half its peak throughput, 20 operations per cycle, not zero: some cycles retire results, but not as many as the hardware could sustain.

## Two ways to supply the parallelism

Little's law says how much independent work has to be ready; it says nothing about where that work comes from. Volkov's talk demonstrates two different sources on the same hardware. The first is **thread-level parallelism (TLP)**: launch enough threads, each contributing one instruction's worth of independent work, that their combined count reaches the target. The second is **instruction-level parallelism (ILP)**: give each thread more than one independent instruction of its own, so that fewer threads are needed to reach the same total. On a GTX480, whose SM matches the GF100 row above, Volkov reports needing 576 threads to reach full throughput with no ILP, but only 320 threads once each thread carries two independent instructions instead of one.[^volkov-ilp] Halving the thread requirement exactly would predict 288, not 320; real scheduling has overhead and granularity Little's law's idealized formula does not model, the same warning P3 gives about the roofline bound being a limit a kernel can fall short of even when its intensity clears the ridge.

Neither source is free. More threads need more architectural state, one set of registers and one program counter per thread, held in hardware at once; a chip that supports more threads in flight spends transistors on holding their state, whether or not every one of them is actually independent work. More ILP needs the compiler, or the programmer, to find or create instructions with no dependency on each other inside a single thread, which is not always possible: a running sum like the matmul kernel's `sum += a[row, k] * b[k, column]` is a chain of 64 dependent additions by construction, one thread's own arithmetic supplying no ILP at all. [G5](g5-occupancy.md) returns to both costs, including what limits how many threads a chip can actually hold at once.

??? check "Why can a single thread computing `sum += a[row, k] * b[k, column]` across 64 values of `k` supply no instruction-level parallelism of its own?"

    Each addition to `sum` needs the previous addition's result: the loop is one chain of 64 dependent operations by construction. ILP has to come from other, independent work, whether other elements of the same thread's computation or other threads entirely.

## The same ceiling the roofline bound already described

Little's law and the roofline bound from P3 are answers to the same kind of question, asked about two different pipes. The roofline bound says a kernel's attainable GFlop/s cannot exceed the smaller of a machine's peak compute rate and its peak bandwidth times the kernel's operational intensity: hiding memory latency perfectly, with an infinite amount of independent work in flight, still leaves the kernel below the compute ceiling whenever its intensity sits left of the ridge, because the bandwidth ceiling is lower there. Little's law says something about one of those ceilings from underneath: a machine cannot even reach its own peak throughput, compute or memory, unless enough independent work is in flight to keep every one of its pipes busy every cycle. `latency_hiding_ceiling.cpp` makes that second shape concrete, using the same 18-cycle, 32-per-cycle pair from GF100's row above: it computes achieved throughput as a function of how many independent operations are in flight, which rises in a straight line and then goes flat, exactly the two-piece shape of a roofline chart, with "operations in flight" standing in for operational intensity.

--8<-- "includes/examples/gpu/g1-throughput-machines/latency_hiding_ceiling.cpp.md"

The ramp meets the ceiling at 576 operations in flight, the same parallelism `little_law.cpp` computed for GF100 from latency times throughput. Below that point, more independent work always helps, in a straight line. At or above it, more independent work buys nothing: the ceiling itself has not moved, because the ceiling is the chip's peak issue rate, a fact about the hardware, not about how much work is waiting to use it. This is the same lesson P3 draws about the roofline bound: an upper limit is a promise about what cannot be exceeded, never a promise about what will be reached.

??? check "A kernel's operational intensity already clears its machine's ridge point, so the roofline bound says it is compute-bound. Does launching more independent threads raise that bound?"

    No. The roofline bound is set by the machine's peak compute rate once the kernel is right of the ridge; more threads can only help the kernel *reach* that already-fixed ceiling by supplying the parallelism Little's law asks for, not raise the ceiling itself.

## For Vortex

!!! vortex "Exercise"

    **Build** a Little's-law report for a fixed grid-and-block launch shape: given a target description (latency in cycles and peak throughput in independent operations per cycle per SM, for one execution resource such as the arithmetic pipe) and a thread count derived from a loop nest with constant bounds, state whether that launch supplies enough parallelism to reach the target's peak throughput, and if not, by how much it falls short.

    1. A target description type holding latency and throughput for one resource. Leave every field's value to be filled in by the reader; this chapter supplies no number for any real GPU's arithmetic or memory pipe, only Volkov's cited figures for the three chips in the table above, which are not Apple silicon.
    2. A thread count computed from a loop nest's constant bounds, the same kind of fact [O8](../optimize/o8-loops.md) already extracts for the stage 10 kernel: one thread per `(row, column)` pair of its output gives 64 &times; 64 = 4,096 threads for the kernel at its stage 10 size, a number fixed by [Decision 43](../decisions/arrays.md#d43)'s array shapes, not guessed.
    3. Little's law's arithmetic itself: parallelism needed equals latency times throughput, and the report states whether the thread count meets, exceeds or falls short of it, and by how much.
    4. Where a thread count falls short, a suggestion of how many independent operations per thread (ILP) would close the gap, exactly as `little_law.cpp`'s second table does, stated as a number for the reader to judge, never as a code transformation the report performs.

    **Not yet:** deciding how the compiler actually maps loop iterations onto threads and blocks ([M12](../mlir/m12-vortex-gpu-path.md) weighs the paths); anything about *which* loop variable should vary across threads, or what one thread's own memory accesses cost ([G4](g4-memory-performance.md), already published, and [G2](g2-simt.md)); measuring a real chip's latency and throughput ([G14](g14-measuring-gpu-code.md)); occupancy limits from registers or shared memory ([G5](g5-occupancy.md)).

    **Proof that it works:**

    - Golden tests reproducing this chapter's own table: given latency 24 and throughput 8, the report says 192 are needed; given 18 and 32, 576; given 18 and 48, 864.
    - A golden test for the stage 10 kernel's one-thread-per-output shape at `[f32; 64, 64]`: 4,096 threads, checked against a target description built from this chapter's GF100 row, reports that the launch already exceeds the 576 needed with no ILP.
    - A property test: for random positive latency, throughput and thread-count triples, the report's shortfall is `max(0, latency * throughput - threads)`, matching a direct recomputation.
    - A test that changing only the loop nest's bounds, never its body, changes the reported thread count exactly as [Decision 43](../decisions/arrays.md#d43)'s row-major shape predicts, and never changes the target description.

## Key ideas

!!! recap "Questions you can now answer"

    - **What is the difference between latency and throughput?** Latency is a time, how long one operation takes to finish; throughput is a rate, how many operations complete per cycle once enough independent ones are ready.
    - **What does Little's law say about a pipelined resource?** The parallelism needed to reach full throughput equals latency times throughput; below that amount of independent work in flight, achieved throughput rises in a straight line, and above it, it is flat.
    - **What are the two ways to supply that parallelism?** Thread-level parallelism, launching more independent threads, and instruction-level parallelism, giving each thread more independent work of its own; either can substitute for the other, imperfectly, since scheduling has overhead a pure formula does not model.
    - **How does Little's law relate to the roofline bound from P3?** They bound different things from different directions: the roofline bound is a ceiling on a kernel's attainable rate; Little's law says how much independent work is needed to actually reach any ceiling, rather than fall short of an already-fixed one.
    - **Why does a running sum like the matmul kernel's inner loop supply no ILP of its own?** Each addition depends on the previous one's result, so the 64 additions are one dependent chain; independent work has to come from elsewhere, such as other threads.
    - **Why is a GPU built around thousands of threads instead of a few fast ones?** It spends its transistor budget on arithmetic throughput and on holding many threads' state at once rather than on the control logic that makes one thread's dependent chain finish sooner, which only pays off when there is enough independent work to keep that throughput busy.

## Where this comes back

!!! next "You will use this again in"

    - [G2. The SIMT execution model](g2-simt.md): *thread*, *throughput-optimized design*
    - [G4. Memory performance: coalescing and bank conflicts](g4-memory-performance.md): *latency*, *throughput*
    - [G5. Occupancy and latency hiding](g5-occupancy.md): *Little's law*, *thread-level parallelism*, *instruction-level parallelism*
    - [G10. The GPU matmul ladder](g10-matmul-ladder.md): *parallelism needed*, *one-thread-per-output mapping*
    - [M12. Designing Vortex's GPU path](../mlir/m12-vortex-gpu-path.md): *target description*, *Little's-law report*

## Sources and further reading

Read Volkov's slides in order; the arithmetic-latency argument (slides 6 to 26) and the memory-latency argument that follows it use the same law twice, once per kind of pipe.

[^cuda-kernels]: NVIDIA, "CUDA Programming Guide", v13.4, section 2, "Writing SIMT Kernels": a kernel is written as the code for one thread and launched across a grid of threads that run it together. <https://docs.nvidia.com/cuda/cuda-programming-guide/02-basics/writing-cuda-kernels.html>
[^volkov-little]: Vasily Volkov, "Better Performance at Lower Occupancy", GTC 2010, slide 11, "Arithmetic parallelism in numbers" (the G80-GT200, GF100 and GF104 latency, throughput and parallelism figures) and slide 10, "Use Little's law". <https://www.nvidia.com/content/GTC-2010/pdfs/2238_GTC2010.pdf>
[^volkov-ilp]: Vasily Volkov, "Better Performance at Lower Occupancy", GTC 2010, slides 16 and 18: an experiment on a GTX480 (a GF100-class GPU) with no instruction-level parallelism needs 576 threads per SM to reach full throughput; with two independent instructions per thread, 320. <https://www.nvidia.com/content/GTC-2010/pdfs/2238_GTC2010.pdf>

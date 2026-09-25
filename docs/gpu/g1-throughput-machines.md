# G1. Throughput machines

<p class="page-intro">A CPU core spends its transistors on finishing one thread's work sooner; a GPU spends them on running thousands of threads at once, and it is fast only when the program keeps enough independent work in flight. This chapter measures "enough" with Little's law and the roofline, and uses both to show why the stage 10 kernel at 64 × 64 would leave a GPU mostly idle: the first check a Vortex compiler needs before it maps a loop nest onto a GPU.</p>

<p class="vx-meta" markdown="1">Level: Foundations · Reading time: about 40 minutes · Builds on: [P3. The roofline model](../optimize/p3-roofline.md), [P5. The microarchitecture shelf](../optimize/p5-microarchitecture.md)</p>

???+ remember "Before you start, remember"

    ??? question "A loop adds into one accumulator. What limits its speed, and how many independent chains keep P pipelined ports busy when each addition takes L cycles?"

        The adder's latency, not its throughput, because each addition needs the previous sum. Keeping the ports busy takes L × P independent chains: in one latency the ports can start L × P instructions, and each must come from a chain whose last result is already back.

        Introduced in [P5. The microarchitecture shelf](../optimize/p5-microarchitecture.md#latency-throughput-and-the-number-of-chains).

    ??? question "What does an out-of-order core do while a load waits for memory?"

        It keeps executing later instructions whose inputs are ready, as far ahead as its reorder buffer lets it look, and retires everything in program order. It overlaps several slow loads only if the program gives it independent ones.

        Introduced in [P5. The microarchitecture shelf](../optimize/p5-microarchitecture.md#out-of-order).

    ??? question "What is the roofline bound, and what does its ridge point mark?"

        Attainable performance is at most the smaller of the machine's peak flop rate and its peak bandwidth times the kernel's operational intensity. The ridge point is the intensity where the two meet: left of it bandwidth sets the bound, right of it the peak does.

        Introduced in [P3. The roofline model](../optimize/p3-roofline.md#the-roofline-two-lines-and-a-ridge).

    ??? question "Why is the stage 10 kernel's flop count fixed at 524,288, while its bytes of memory traffic are not?"

        The flops follow from the loop bounds alone, which no reordering changes. The bytes depend on how many times each value is fetched again before it is reused, and that is what a schedule, naive or tiled, decides.

        Introduced in [P3. The roofline model](../optimize/p3-roofline.md#operational-intensity-flops-per-byte-of-dram-traffic).

!!! goals "In this chapter"

    - Explain what a latency-optimized and a throughput-optimized design spend their transistors on, and where each finds independent work.
    - State Little's law, and use it to compute how much work must be in flight to keep an arithmetic pipe or a memory system busy.
    - Decide whether a launch of a given size supplies that much work, and find which of several ceilings binds it.
    - Place a kernel on a GPU's roofline, and explain why a GPU's ridge point sits further right than a CPU's.
    - Separate what a compiler can count from a program from what it must take from a measurement.

## A budget spent two ways

The stage 10 kernel multiplies two 64 × 64 matrices with 262,144 multiply-adds, and its 4,096 outputs do not depend on one another: `c[0, 0]` needs nothing from `c[0, 1]`. Only the 64 steps inside one output form a chain, because each addition into `sum` needs the one before it ([P5](../optimize/p5-microarchitecture.md#latency-throughput-and-the-number-of-chains)). A processor can run such a kernel in two ways: finish each chain as fast as possible, one after another, or work on many chains at once. Each way is a design, and each needs its own hardware.

A **latency-optimized** design makes one thread's chain of dependent instructions finish sooner. It spends its transistors on large caches, so that fewer loads wait for DRAM; on branch prediction and speculation, so that it need not stop to learn which way a branch goes; and on an out-of-order window that looks ahead in the thread for instructions whose inputs are ready ([P5](../optimize/p5-microarchitecture.md#out-of-order)). A CPU core is the example. NVIDIA's CUDA Programming Guide draws the contrast this way: a CPU is designed to run a serial sequence of operations, a thread, as fast as possible, and a few tens of threads in parallel, and it gives more of its transistors to caching and flow control.[^pg-gpus]

A **throughput-optimized** design makes the most work finish per second, and accepts that each thread runs slower. The same guide describes a GPU as designed to run thousands of threads in parallel, trading single-thread performance for total throughput, with more of its transistors given to data processing.[^pg-gpus] Its cores, which NVIDIA calls **streaming multiprocessors** (SMs), leave out the machinery a CPU uses to hurry one thread: an SM issues instructions in order and does no branch prediction or speculative execution.[^pg-hw] What it keeps instead is state. The registers and program counter of every thread on an SM stay on chip for the thread's whole life, so switching from one thread to another costs nothing, and at each issue cycle a scheduler picks threads whose next instruction is ready.[^pg-mt] (The SM schedules threads in groups of 32, which [G2](g2-simt.md) introduces as warps; this chapter counts threads.)

Both designs are pipelined, and both overlap independent work. They differ in where they look for it. Figure 1 runs each for twelve cycles on a load that takes eight.

<figure class="vx-figure">
<svg viewBox="0 0 760 378" role="img" aria-label="Two cores running the same kind of work for twelve cycles: a latency-optimized core that looks ahead inside one thread and runs out of independent instructions, and a throughput-optimized core that switches among eight threads and issues every cycle" aria-describedby="g1-f1-desc">
<title id="g1-f1-title">Where each design finds independent work</title>
<desc id="g1-f1-desc">Two panels share a time axis of twelve cycles, with a toy memory latency of eight cycles. Left, a latency-optimized core runs one thread. The thread issues a load in cycle 0; an add that needs the loaded value waits until cycle 8. The core looks ahead and issues four independent instructions, i1 to i4, in cycles 1 to 4, then finds nothing else it may run, so its issue slot is idle in cycles 5, 6 and 7, and resumes when the value arrives. Right, a throughput-optimized core holds eight threads, A to H. Each issues a load in its own cycle, 0 to 7, and waits eight cycles for it; in cycle 8 thread A's value has arrived and A issues its add, then B in cycle 9, and so on. Its issue slot is busy in every cycle. Notes under the panels say what each design spends its transistors on.</desc>
<line class="vx-line" x1="390" y1="12" x2="390" y2="360"/>
<text class="vx-text" x="30" y="24">Latency-optimized core</text>
<text class="vx-text-muted" x="30" y="42">one thread; the core looks ahead inside it</text>
<text class="vx-text" x="410" y="24">Throughput-optimized core</text>
<text class="vx-text-muted" x="410" y="42">eight threads, each issued in program order</text>
<rect class="vx-box-accent" x="30" y="70" width="26" height="20"/>
<text class="vx-mono" x="43" y="85" text-anchor="middle" style="font-size:11px">ld</text>
<rect class="vx-box" x="58" y="77" width="194" height="6"/>
<text class="vx-text-muted" x="58" y="67">load x: waiting for memory</text>
<rect class="vx-box-strong" x="58" y="96" width="26" height="20"/>
<text class="vx-mono" x="71" y="111" text-anchor="middle" style="font-size:11px">i1</text>
<rect class="vx-box-strong" x="86" y="122" width="26" height="20"/>
<text class="vx-mono" x="99" y="137" text-anchor="middle" style="font-size:11px">i2</text>
<rect class="vx-box-strong" x="114" y="148" width="26" height="20"/>
<text class="vx-mono" x="127" y="163" text-anchor="middle" style="font-size:11px">i3</text>
<rect class="vx-box-strong" x="142" y="174" width="26" height="20"/>
<text class="vx-mono" x="155" y="189" text-anchor="middle" style="font-size:11px">i4</text>
<text class="vx-text-muted" x="174" y="137">independent of x:</text>
<text class="vx-text-muted" x="174" y="163">run early</text>
<rect class="vx-box-bad" x="58" y="200" width="194" height="20"/>
<text class="vx-text-muted" x="156" y="215" text-anchor="middle">add needs x: waits</text>
<rect class="vx-box-strong" x="254" y="200" width="26" height="20"/>
<text class="vx-mono" x="267" y="215" text-anchor="middle" style="font-size:11px">add</text>
<text class="vx-mono" x="404" y="79" text-anchor="end" style="font-size:11px">A</text>
<rect class="vx-box-accent" x="410" y="64" width="26" height="20"/>
<text class="vx-mono" x="423" y="79" text-anchor="middle" style="font-size:11px">ld</text>
<rect class="vx-box" x="438" y="71" width="194" height="6"/>
<rect class="vx-box-strong" x="634" y="64" width="26" height="20"/>
<text class="vx-mono" x="647" y="79" text-anchor="middle" style="font-size:11px">add</text>
<text class="vx-mono" x="404" y="101" text-anchor="end" style="font-size:11px">B</text>
<rect class="vx-box-accent" x="438" y="86" width="26" height="20"/>
<text class="vx-mono" x="451" y="101" text-anchor="middle" style="font-size:11px">ld</text>
<rect class="vx-box" x="466" y="93" width="194" height="6"/>
<rect class="vx-box-strong" x="662" y="86" width="26" height="20"/>
<text class="vx-mono" x="675" y="101" text-anchor="middle" style="font-size:11px">add</text>
<text class="vx-mono" x="404" y="123" text-anchor="end" style="font-size:11px">C</text>
<rect class="vx-box-accent" x="466" y="108" width="26" height="20"/>
<text class="vx-mono" x="479" y="123" text-anchor="middle" style="font-size:11px">ld</text>
<rect class="vx-box" x="494" y="115" width="194" height="6"/>
<rect class="vx-box-strong" x="690" y="108" width="26" height="20"/>
<text class="vx-mono" x="703" y="123" text-anchor="middle" style="font-size:11px">add</text>
<text class="vx-mono" x="404" y="145" text-anchor="end" style="font-size:11px">D</text>
<rect class="vx-box-accent" x="494" y="130" width="26" height="20"/>
<text class="vx-mono" x="507" y="145" text-anchor="middle" style="font-size:11px">ld</text>
<rect class="vx-box" x="522" y="137" width="194" height="6"/>
<rect class="vx-box-strong" x="718" y="130" width="26" height="20"/>
<text class="vx-mono" x="731" y="145" text-anchor="middle" style="font-size:11px">add</text>
<text class="vx-mono" x="404" y="167" text-anchor="end" style="font-size:11px">E</text>
<rect class="vx-box-accent" x="522" y="152" width="26" height="20"/>
<text class="vx-mono" x="535" y="167" text-anchor="middle" style="font-size:11px">ld</text>
<rect class="vx-box" x="550" y="159" width="194" height="6"/>
<text class="vx-mono" x="404" y="189" text-anchor="end" style="font-size:11px">F</text>
<rect class="vx-box-accent" x="550" y="174" width="26" height="20"/>
<text class="vx-mono" x="563" y="189" text-anchor="middle" style="font-size:11px">ld</text>
<rect class="vx-box" x="578" y="181" width="166" height="6"/>
<text class="vx-mono" x="404" y="211" text-anchor="end" style="font-size:11px">G</text>
<rect class="vx-box-accent" x="578" y="196" width="26" height="20"/>
<text class="vx-mono" x="591" y="211" text-anchor="middle" style="font-size:11px">ld</text>
<rect class="vx-box" x="606" y="203" width="138" height="6"/>
<text class="vx-mono" x="404" y="233" text-anchor="end" style="font-size:11px">H</text>
<rect class="vx-box-accent" x="606" y="218" width="26" height="20"/>
<text class="vx-mono" x="619" y="233" text-anchor="middle" style="font-size:11px">ld</text>
<rect class="vx-box" x="634" y="225" width="110" height="6"/>
<text class="vx-text-muted" x="30" y="260">issue slot, cycles 0 to 11</text>
<text class="vx-text-muted" x="410" y="260">issue slot, cycles 0 to 11</text>
<rect class="vx-box-strong vx-seq" x="30" y="268" width="26" height="20" style="--vx-i: 0; --vx-n: 12"/>
<text class="vx-mono" x="43" y="283" text-anchor="middle" style="font-size:11px">ld</text>
<rect class="vx-box-strong vx-seq" x="58" y="268" width="26" height="20" style="--vx-i: 1; --vx-n: 12"/>
<text class="vx-mono" x="71" y="283" text-anchor="middle" style="font-size:11px">i1</text>
<rect class="vx-box-strong vx-seq" x="86" y="268" width="26" height="20" style="--vx-i: 2; --vx-n: 12"/>
<text class="vx-mono" x="99" y="283" text-anchor="middle" style="font-size:11px">i2</text>
<rect class="vx-box-strong vx-seq" x="114" y="268" width="26" height="20" style="--vx-i: 3; --vx-n: 12"/>
<text class="vx-mono" x="127" y="283" text-anchor="middle" style="font-size:11px">i3</text>
<rect class="vx-box-strong vx-seq" x="142" y="268" width="26" height="20" style="--vx-i: 4; --vx-n: 12"/>
<text class="vx-mono" x="155" y="283" text-anchor="middle" style="font-size:11px">i4</text>
<rect class="vx-box-bad" x="170" y="268" width="26" height="20"/>
<rect class="vx-box-bad" x="198" y="268" width="26" height="20"/>
<rect class="vx-box-bad" x="226" y="268" width="26" height="20"/>
<rect class="vx-box-strong vx-seq" x="254" y="268" width="26" height="20" style="--vx-i: 8; --vx-n: 12"/>
<text class="vx-mono" x="267" y="283" text-anchor="middle" style="font-size:11px">add</text>
<rect class="vx-box-strong vx-seq" x="282" y="268" width="26" height="20" style="--vx-i: 9; --vx-n: 12"/>
<text class="vx-mono" x="295" y="283" text-anchor="middle" style="font-size:11px">i5</text>
<rect class="vx-box-strong vx-seq" x="310" y="268" width="26" height="20" style="--vx-i: 10; --vx-n: 12"/>
<text class="vx-mono" x="323" y="283" text-anchor="middle" style="font-size:11px">i6</text>
<rect class="vx-box-strong vx-seq" x="338" y="268" width="26" height="20" style="--vx-i: 11; --vx-n: 12"/>
<text class="vx-mono" x="351" y="283" text-anchor="middle" style="font-size:11px">i7</text>
<rect class="vx-box-strong vx-seq" x="410" y="268" width="26" height="20" style="--vx-i: 0; --vx-n: 12"/>
<text class="vx-mono" x="423" y="283" text-anchor="middle" style="font-size:11px">A</text>
<rect class="vx-box-strong vx-seq" x="438" y="268" width="26" height="20" style="--vx-i: 1; --vx-n: 12"/>
<text class="vx-mono" x="451" y="283" text-anchor="middle" style="font-size:11px">B</text>
<rect class="vx-box-strong vx-seq" x="466" y="268" width="26" height="20" style="--vx-i: 2; --vx-n: 12"/>
<text class="vx-mono" x="479" y="283" text-anchor="middle" style="font-size:11px">C</text>
<rect class="vx-box-strong vx-seq" x="494" y="268" width="26" height="20" style="--vx-i: 3; --vx-n: 12"/>
<text class="vx-mono" x="507" y="283" text-anchor="middle" style="font-size:11px">D</text>
<rect class="vx-box-strong vx-seq" x="522" y="268" width="26" height="20" style="--vx-i: 4; --vx-n: 12"/>
<text class="vx-mono" x="535" y="283" text-anchor="middle" style="font-size:11px">E</text>
<rect class="vx-box-strong vx-seq" x="550" y="268" width="26" height="20" style="--vx-i: 5; --vx-n: 12"/>
<text class="vx-mono" x="563" y="283" text-anchor="middle" style="font-size:11px">F</text>
<rect class="vx-box-strong vx-seq" x="578" y="268" width="26" height="20" style="--vx-i: 6; --vx-n: 12"/>
<text class="vx-mono" x="591" y="283" text-anchor="middle" style="font-size:11px">G</text>
<rect class="vx-box-strong vx-seq" x="606" y="268" width="26" height="20" style="--vx-i: 7; --vx-n: 12"/>
<text class="vx-mono" x="619" y="283" text-anchor="middle" style="font-size:11px">H</text>
<rect class="vx-box-strong vx-seq" x="634" y="268" width="26" height="20" style="--vx-i: 8; --vx-n: 12"/>
<text class="vx-mono" x="647" y="283" text-anchor="middle" style="font-size:11px">A</text>
<rect class="vx-box-strong vx-seq" x="662" y="268" width="26" height="20" style="--vx-i: 9; --vx-n: 12"/>
<text class="vx-mono" x="675" y="283" text-anchor="middle" style="font-size:11px">B</text>
<rect class="vx-box-strong vx-seq" x="690" y="268" width="26" height="20" style="--vx-i: 10; --vx-n: 12"/>
<text class="vx-mono" x="703" y="283" text-anchor="middle" style="font-size:11px">C</text>
<rect class="vx-box-strong vx-seq" x="718" y="268" width="26" height="20" style="--vx-i: 11; --vx-n: 12"/>
<text class="vx-mono" x="731" y="283" text-anchor="middle" style="font-size:11px">D</text>
<text class="vx-text-muted" x="30" y="310">it found four independent instructions, then</text>
<text class="vx-text-muted" x="30" y="326">none: three idle cycles (dashed) until x arrives</text>
<text class="vx-text-muted" x="410" y="310">each cycle it issues for a thread whose next</text>
<text class="vx-text-muted" x="410" y="326">instruction is ready: no idle cycles</text>
<text class="vx-text-accent" x="30" y="356">spends on caches, prediction, look-ahead</text>
<text class="vx-text-accent" x="410" y="356">spends on execution units, per-thread state</text>
</svg>
<figcaption>Figure 1. Twelve cycles of two cores, with a toy latency of eight cycles for a load. The latency-optimized core runs one thread and looks ahead in it: it issues four instructions that do not need <code>x</code>, then has nothing it may issue until <code>x</code> arrives. The throughput-optimized core holds eight threads, issues each one's instructions in order, and fills every cycle by switching to a thread whose next instruction is ready. The numbers are chosen for the drawing, not taken from any chip.</figcaption>
</figure>

How far the latency-optimized core can look is bounded by its buffers (192 micro-operations in LLVM's model of an Apple M1 core, as [P5](../optimize/p5-microarchitecture.md#out-of-order) found), and how much it finds depends on the dependences inside the one thread. The throughput-optimized core looks across threads instead: while thread A waits for its load, B to H issue theirs, and by the time H has issued, A's value is back. Neither design removes the eight cycles. The first hides them with the thread's own spare work, and shortens many of them with its caches; the second hides them with other threads' work. So a GPU is fast only on work that splits into many threads, and the next question is how many are enough.

??? check "Suppose the thread in Figure 1 had only two instructions that do not need `x`. How many of the first eight issue slots would the latency-optimized core leave empty, and how many threads would the throughput-optimized core need to fill every slot?"

    Five: the core issues the load and the two independent instructions in cycles 0 to 2, then waits through cycles 3 to 7 for `x`. The throughput-optimized core still needs eight threads. It issues once per cycle, and a thread can issue again only after its eight-cycle wait, so eight threads cover one wait. How much spare work each thread has does not enter into it.

## Latency and throughput are different numbers

Two numbers describe any pipelined resource, and they have different units. **Latency** is a time: the cycles from starting an operation to its result being usable. **Throughput** is a rate: how many operations the resource completes per cycle when it is busy. [P5](../optimize/p5-microarchitecture.md#latency-throughput-and-the-number-of-chains) met both on a CPU's execution ports.

Vasily Volkov's 2010 talk on GPU performance gives both numbers for both pipes of one GPU, NVIDIA's GTX480, and this chapter uses them throughout. An arithmetic instruction takes about 18 cycles on this chip; a load from memory, in the talk's general figure, takes 400 cycles or more; and latency differs from one kind of operation to another.[^volkov-latency] The whole chip completes 480 multiply-adds per cycle, its 1.3 Tflop/s, and about 32 four-byte loads per cycle, its 177 GB/s.[^volkov-throughput] These figures are from 2010 and describe one chip. They are here because one source gives latency and throughput for both pipes of the same GPU, and because the arithmetic, not the chip, is the lesson.

Volkov warns that the two numbers are often confused. His example is a claim that arithmetic is a hundred times faster than memory, because an arithmetic instruction costs 4 cycles per warp on an older GPU and a memory access 400.[^volkov-throughput] The 4 is a rate, the cycles between two independent instructions; the 400 is a time, the cycles until one load returns. Their ratio compares nothing. Multiplying them, though, gives something useful.

## Little's law: how much work has to be in flight

Queueing theory has a name for that product. **Little's law** says that the average number of items in a system equals the average rate at which they arrive times the average time each one spends inside.[^little] John Little, who proved it in 1961, points out how few assumptions it needs: over any period that begins and ends with the system empty it holds exactly, whatever the pattern of arrivals and whatever order the items are served in. He also finds the law inside computers, with requests to memory as the items and latency as the time each spends in the system.[^little]

Apply it to a pipelined resource that is kept busy. Operations enter at its throughput and each stays for its latency, so the average number inside is

$$\text{work in flight} = \text{latency} \times \text{throughput}$$

**Work in flight** means operations that have started and not finished: instructions issued and not complete, loads sent and not returned. Read the law the other way and it says what a resource can deliver: at most the work in flight divided by the latency, and never more than its peak. P5's rule for a CPU's ports, L × P chains, is this law for one core.

Now the GTX480's memory. Volkov applies the same formula to it as to arithmetic, with a latency of under 800 cycles, which he marks with a question mark.[^volkov-memory] At 32 loads of 4 bytes, 128 bytes per cycle, the memory stays busy only with 800 × 128 = 102,400 bytes in flight: the "100 KB" of his slide. At the 400 cycles quoted above it would be half that. For arithmetic on one SM, 18 cycles × 32 multiply-adds per cycle gives 576 in flight;[^volkov-memory] for the chip's 15 SMs,[^volkov-chip] 8,640. The first example computes these products.

--8<-- "includes/examples/gpu/g1-throughput-machines/little_law.cpp.md"

The second table turns bytes into threads. A thread does not stall when it issues a load, only when an instruction needs the loaded value, so one thread can keep several loads outstanding.[^volkov-memory] Only the total counts: 25,600 threads with one 4-byte load each fill the pipe, and so do 1,024 threads with 100 bytes each, the two cases Volkov gives, rounded, in his talk.[^volkov-memory]

Figure 2 draws the law for this memory system. Below 102,400 bytes in flight, the bytes delivered per cycle rise in a straight line, the bytes in flight divided by 800. Above it they stay at 128 however many more loads wait, because the pipe is already full.

<figure class="vx-figure">
<svg viewBox="0 0 760 410" role="img" aria-label="Bytes delivered per cycle by a GTX480's memory, as a function of the bytes in flight: a straight rise to 128 bytes per cycle at 102,400 bytes in flight, then flat; the 64 by 64 stage 10 launch sits a third of the way up the rise" aria-describedby="g1-f2-desc">
<title id="g1-f2-title">Little's law for a memory system</title>
<desc id="g1-f2-desc">A chart with bytes in flight on the horizontal axis, from 0 to 160 KiB, and bytes delivered per cycle on the vertical axis, from 0 to 128. A solid line rises straight from the origin to 128 bytes per cycle at 102,400 bytes in flight, the product of an 800-cycle latency and 128 bytes per cycle, and stays flat after it. A dashed line rises twice as steeply, for a 400-cycle latency, and reaches the flat at 51,200 bytes. A dot marks the 64 by 64 stage 10 launch: 4,096 threads with 8 bytes each in flight, 32,768 bytes, which on the solid line delivers about 41 bytes per cycle, 32 percent of the bandwidth. An arrow at the right edge marks the 1024 by 1024 launch, with 184,320 bytes in flight, on the flat part. A note says that 25,600 threads with 4 bytes each, or 1,024 threads with 100 bytes each, reach the corner. A small dot travels up the solid line and along the flat.</desc>
<line class="vx-line" x1="110" y1="330" x2="690" y2="330"/>
<line class="vx-line" x1="110" y1="330" x2="110" y2="70"/>
<line class="vx-line" x1="110.0" y1="330" x2="110.0" y2="335"/>
<text class="vx-text-muted" x="110.0" y="350" text-anchor="middle">0 KiB</text>
<line class="vx-line" x1="226.0" y1="330" x2="226.0" y2="335"/>
<text class="vx-text-muted" x="226.0" y="350" text-anchor="middle">32 KiB</text>
<line class="vx-line" x1="342.0" y1="330" x2="342.0" y2="335"/>
<text class="vx-text-muted" x="342.0" y="350" text-anchor="middle">64 KiB</text>
<line class="vx-line" x1="458.0" y1="330" x2="458.0" y2="335"/>
<text class="vx-text-muted" x="458.0" y="350" text-anchor="middle">96 KiB</text>
<line class="vx-line" x1="574.0" y1="330" x2="574.0" y2="335"/>
<text class="vx-text-muted" x="574.0" y="350" text-anchor="middle">128 KiB</text>
<line class="vx-line" x1="690.0" y1="330" x2="690.0" y2="335"/>
<text class="vx-text-muted" x="690.0" y="350" text-anchor="middle">160 KiB</text>
<line class="vx-line" x1="105" y1="330.0" x2="110" y2="330.0"/>
<text class="vx-text-muted" x="101" y="334.0" text-anchor="end">0</text>
<line class="vx-line" x1="105" y1="272.9" x2="110" y2="272.9"/>
<text class="vx-text-muted" x="101" y="276.9" text-anchor="end">32</text>
<line class="vx-line" x1="105" y1="215.7" x2="110" y2="215.7"/>
<text class="vx-text-muted" x="101" y="219.7" text-anchor="end">64</text>
<line class="vx-line" x1="105" y1="158.6" x2="110" y2="158.6"/>
<text class="vx-text-muted" x="101" y="162.6" text-anchor="end">96</text>
<line class="vx-line" x1="105" y1="101.4" x2="110" y2="101.4"/>
<text class="vx-text-muted" x="101" y="105.4" text-anchor="end">128</text>
<text class="vx-text-muted" x="400" y="374" text-anchor="middle">bytes in flight: loads issued and not yet returned, all threads together</text>
<text class="vx-text-muted" x="30" y="205" text-anchor="middle" transform="rotate(-90 30 205)">bytes delivered per cycle</text>
<line class="vx-line" x1="110" y1="330" x2="291.2" y2="101.4" stroke-dasharray="5 4"/>
<line class="vx-line" x1="291.2" y1="98.4" x2="472.5" y2="98.4" stroke-dasharray="5 4"/>
<text class="vx-text-muted" x="251.6" y="130.0" text-anchor="end">latency 400 cycles</text>
<line class="vx-box-accent" x1="110" y1="330" x2="472.5" y2="101.4"/>
<line class="vx-box-accent" x1="472.5" y1="101.4" x2="690" y2="101.4"/>
<text class="vx-text-accent" x="407.4" y="180.0">latency 800 cycles</text>
<text class="vx-text-muted" x="690" y="91.4" text-anchor="end">bandwidth: 128 bytes per cycle</text>
<line class="vx-line" x1="472.5" y1="101.4" x2="472.5" y2="330" stroke-dasharray="3 4"/>
<text class="vx-mono" x="480.5" y="219.3">800 × 128 = 102,400 bytes</text>
<text class="vx-text-muted" x="480.5" y="237.3">25,600 threads × 4 bytes, or</text>
<text class="vx-text-muted" x="480.5" y="253.3">1,024 threads × 100 bytes</text>
<circle class="vx-dot vx-pulse" cx="226.0" cy="256.9" r="6"/>
<text class="vx-text" x="238.0" y="274.9">64 × 64 launch</text>
<text class="vx-text-muted" x="238.0" y="291.9">4,096 threads × 8 bytes = 32 KiB:</text>
<text class="vx-text-muted" x="238.0" y="307.9">41 bytes per cycle, 32%</text>
<line class="vx-line" x1="620" y1="127.4" x2="684" y2="127.4"/>
<polygon class="vx-arrowhead" points="690,127.4 681,122.4 681,132.4"/>
<text class="vx-text-muted" x="690" y="147.4" text-anchor="end">1024 × 1024: 180 KiB, on the flat</text>
<circle class="vx-dot" cx="0" cy="0" r="4"><animateMotion dur="6s" repeatCount="indefinite" path="M110,330 L472.5,101.4 L690,101.4"/></circle>
</svg>
<figcaption>Figure 2. Little's law for the GTX480's memory, from Volkov's figures: 128 bytes per cycle and a latency of up to 800 cycles. Delivered bandwidth rises in proportion to the bytes in flight until 800 × 128 = 102,400 bytes, then stays at the peak; with a 400-cycle latency (dashed) the corner comes at half that. The 64 × 64 stage 10 launch, worked out below, holds 32 KiB in flight and gets at most a third of the bandwidth.</figcaption>
</figure>

Two cautions go with the picture. The model holds latency fixed, but a real memory system's latency grows as it gets busier, so a measured curve bends below the corner; Volkov's later dissertation measures how mean memory latency depends on memory throughput.[^volkov-thesis] And "in flight" counts only the threads the chip holds at once: a thread still waiting for its turn to start has no load outstanding.

??? check "A GPU's memory takes 500 cycles to return a load and delivers 64 bytes per cycle. Each thread keeps two independent 8-byte loads outstanding. How many threads keep the memory busy, and what fraction of the bandwidth do 500 threads reach?"

    Little's law asks for 500 × 64 = 32,000 bytes in flight. At 16 bytes per thread that is 2,000 threads. Five hundred threads hold 8,000 bytes, which deliver at most 8,000 ÷ 500 = 16 bytes per cycle: a quarter of the bandwidth.

## Where the independent work comes from

A CPU has two sources of independent work: each core's look-ahead window, bounded by its buffers and by the dependences in one thread, and the few tens of threads the whole chip runs in parallel.[^pg-gpus] For memory it leans on its caches, which make many loads short, as much as on hiding latency.

A throughput-optimized chip also has two sources, and both belong to the program. It can have more threads, each with a little work outstanding, or more independent work inside each thread: several loads issued before the first is used, or several outputs computed side by side. Volkov shows both working on the GTX480, for arithmetic and for memory.[^volkov-sources] [G5](g5-occupancy.md) names them thread-level and instruction-level parallelism, measures how they trade, and counts how many threads an SM can hold at once. On the GTX480 that limit is 1,536 threads per SM,[^volkov-chip] 23,040 for the chip.

The stage 10 kernel, written as one thread per output, supplies both in fixed amounts. Its arithmetic is one chain per thread: each multiply-add needs the previous sum, and a Vortex compiler may not split the sum into partial sums to make more chains, because that reorders the additions ([P5](../optimize/p5-microarchitecture.md#where-independent-chains-come-from), [decision 56](../decisions/numbers.md#d56)). Its loads are freer. `a[row, k]` and `b[k, column]` do not depend on `sum`, so the two loads of one step can be in flight together: 8 bytes per thread. A compiler that unrolls the loop can issue later steps' loads early and raise that number; the model below assumes it does not.

## The stage 10 kernel on a 2010 GPU

Put the pieces together for the one-thread-per-output kernel at two sizes, using Volkov's GTX480 figures and assuming that no cache catches a repeated load, so that every multiply-add costs 8 bytes of memory traffic. Four **ceilings**, upper limits on its rate, each set by one resource, bound it. Counted in multiply-adds per cycle for the whole chip, they are:

1. The arithmetic peak: 480.
2. Arithmetic latency: one chain per thread in flight, so at most the threads held at once divided by 18.
3. Memory bandwidth: 128 bytes per cycle divided by 8 bytes per multiply-add, 16.
4. Memory latency: the bytes in flight divided by 800 cycles, then by 8 bytes per multiply-add.

At 64 × 64 the launch has 4,096 threads, all of which fit on the chip at once. Ceiling 2 is 4,096 ÷ 18 = 227.6, under half the peak, because the chip needs 8,640 chains in flight and gets 4,096. Ceiling 4 is lower still. The threads hold 4,096 × 8 = 32,768 bytes in flight, a third of the 102,400 the memory needs, so they receive at most 32,768 ÷ 800 = 41 bytes per cycle: 5.12 multiply-adds. That is the smallest ceiling, so it binds. The kernel can reach at most about 1 percent of the arithmetic peak, and even the memory is two-thirds idle. With the 400-cycle latency, ceiling 4 doubles to 10.24 and still binds.

At 1024 × 1024 the launch has over a million threads, far more than the 23,040 the chip holds at once, and those hold 184,320 bytes in flight: more than the memory needs. Ceiling 4 rises to 28.8 and stops binding. Ceiling 3, the bandwidth, binds at 16 multiply-adds per cycle, 3.3 percent of the peak. The second example prints both cases.

--8<-- "includes/examples/gpu/g1-throughput-machines/launch_bounds.cpp.md"

The bigger launch solved the first problem and exposed the second. Nothing about the launch can lift ceiling 3: only fewer bytes per multiply-add can, and that is the roofline's subject.

??? check "On these figures, from what matrix size n does the one-thread-per-output launch stop being bound by memory latency?"

    When n² threads × 8 bytes ÷ 800 cycles ÷ 8 bytes reaches the bandwidth ceiling of 16 multiply-adds per cycle, that is, when n² reaches 12,800, the thread count the first example printed for 8 bytes per thread. So from n = 114: 113² = 12,769 falls 31 short, and 114² = 12,996 does not. Below that size the launch cannot fill the memory; above it the memory is full and bandwidth binds.

## The same ceiling the roofline bound already described

Ceiling 3 is the diagonal of a roofline, and ceiling 1 its flat top ([P3](../optimize/p3-roofline.md#the-roofline-two-lines-and-a-ridge)). The roofline paper states the bound as the smaller of the peak and the bandwidth times the operational intensity, and puts the ridge point where the two meet.[^roofline] In the GTX480's per-cycle terms the peak is 960 flops (480 multiply-adds) and the bandwidth 128 bytes, so the ridge sits at 960 ÷ 128 = 7.5 flops per byte: the chip can do about 15 multiply-adds in the time it takes to bring in one 4-byte word.

That is far to the right of a CPU's. P3's Opteron X2 has its ridge near 1 flop per byte (17.6 GFlop/s over 15 GB/s), and the paper puts its four-core successor, the X4, at 4.4.[^roofline] A throughput design adds arithmetic faster than bandwidth, and its ridge moves right. The paper spells out what that means: when the ridge is far to the right, only kernels with high operational intensity can reach the peak, and the ridge's position hints at how hard peak performance will be for programmers and compiler writers.[^roofline]

The stage 10 kernel with no reuse does 2 flops per 8 bytes, 0.25 flops per byte, thirty times left of the ridge. Figure 3 places it, with the two ceilings that the 64 × 64 launch adds below the machine's roof.

<figure class="vx-figure">
<svg viewBox="0 0 760 420" role="img" aria-label="The roofline of a GTX480 in flops per cycle, with two lower ceilings that the 64 by 64 stage 10 launch adds because it has too little work in flight" aria-describedby="g1-f3-desc">
<title id="g1-f3-title">A GPU roofline, and the ceilings a small launch adds</title>
<desc id="g1-f3-desc">A log-log chart. The horizontal axis is operational intensity, from one sixteenth to 64 flops per byte; the vertical axis is flops per cycle for the whole chip, from 4 to 1024. The machine's roof rises along a diagonal of 128 bytes per cycle times intensity to the ridge point at 7.5 flops per byte and continues flat at 960 flops per cycle. Below it, two dashed lines belong to the 64 by 64 launch: a diagonal of 41 bytes per cycle times intensity, from 32 KiB in flight over 800 cycles of latency, and a flat line at 455 flops per cycle, from 4,096 dependent chains over 18 cycles. The stage 10 kernel with no reuse sits at 0.25 flops per byte: the machine's roof allows 32 flops per cycle there, the launch's dashed diagonal only 10. An arrow along the bottom says that more reuse moves a kernel to the right.</desc>
<line class="vx-line" x1="110" y1="350" x2="690" y2="350"/>
<line class="vx-line" x1="110" y1="350" x2="110" y2="58"/>
<line class="vx-line" x1="110" y1="350" x2="110" y2="355"/>
<text class="vx-text-muted" x="110" y="370" text-anchor="middle">1/16</text>
<line class="vx-line" x1="226" y1="350" x2="226" y2="355"/>
<text class="vx-text-muted" x="226" y="370" text-anchor="middle">1/4</text>
<line class="vx-line" x1="342" y1="350" x2="342" y2="355"/>
<text class="vx-text-muted" x="342" y="370" text-anchor="middle">1</text>
<line class="vx-line" x1="458" y1="350" x2="458" y2="355"/>
<text class="vx-text-muted" x="458" y="370" text-anchor="middle">4</text>
<line class="vx-line" x1="574" y1="350" x2="574" y2="355"/>
<text class="vx-text-muted" x="574" y="370" text-anchor="middle">16</text>
<line class="vx-line" x1="690" y1="350" x2="690" y2="355"/>
<text class="vx-text-muted" x="690" y="370" text-anchor="middle">64</text>
<line class="vx-line" x1="105" y1="350" x2="110" y2="350"/>
<text class="vx-text-muted" x="101" y="354" text-anchor="end">4</text>
<line class="vx-line" x1="105" y1="280" x2="110" y2="280"/>
<text class="vx-text-muted" x="101" y="284" text-anchor="end">16</text>
<line class="vx-line" x1="105" y1="210" x2="110" y2="210"/>
<text class="vx-text-muted" x="101" y="214" text-anchor="end">64</text>
<line class="vx-line" x1="105" y1="140" x2="110" y2="140"/>
<text class="vx-text-muted" x="101" y="144" text-anchor="end">256</text>
<line class="vx-line" x1="105" y1="70" x2="110" y2="70"/>
<text class="vx-text-muted" x="101" y="74" text-anchor="end">1024</text>
<text class="vx-text-muted" x="400" y="394" text-anchor="middle">operational intensity: flops per byte of memory traffic (log scale)</text>
<text class="vx-text-muted" x="30" y="210" text-anchor="middle" transform="rotate(-90 30 210)">flops per cycle, whole chip (log scale)</text>
<line class="vx-box-accent" x1="110" y1="315.0" x2="510.6" y2="73.3"/>
<line class="vx-box-accent" x1="510.6" y1="73.3" x2="690" y2="73.3"/>
<line class="vx-line" x1="510.6" y1="73.3" x2="510.6" y2="350" stroke-dasharray="3 4"/>
<text class="vx-mono" x="510.6" y="47.3" text-anchor="middle">ridge 7.5</text>
<text class="vx-text-muted" x="690" y="65.3" text-anchor="end">peak: 960 flops per cycle</text>
<text class="vx-text-accent" x="300" y="185.3" text-anchor="middle" transform="rotate(-31.1 300 185.3)">machine: 128 bytes per cycle × intensity</text>
<line class="vx-line" x1="147.3" y1="350" x2="543.5" y2="110.9" stroke-dasharray="6 4"/>
<line class="vx-line" x1="543.5" y1="110.9" x2="690" y2="110.9" stroke-dasharray="6 4"/>
<text class="vx-text-muted" x="690" y="128.9" text-anchor="end">64 × 64 launch:</text>
<text class="vx-text-muted" x="690" y="144.9" text-anchor="end">4,096 chains ÷ 18 cycles</text>
<text class="vx-text-muted" x="380" y="225.6" text-anchor="middle" transform="rotate(-31.1 380 225.6)">64 × 64 launch: 41 bytes per cycle × intensity</text>
<line class="vx-line" x1="226.0" y1="245.0" x2="226.0" y2="302.5" stroke-dasharray="2 3"/>
<circle class="vx-box-strong" cx="226.0" cy="245.0" r="5"/>
<circle class="vx-dot vx-pulse" cx="226.0" cy="302.5" r="6"/>
<text class="vx-text-muted" x="238.0" y="261.0">roof: 32</text>
<text class="vx-text" x="238.0" y="320.5">stage 10, no reuse: 10</text>
<line class="vx-line" x1="396.0" y1="334" x2="488.0" y2="334"/>
<polygon class="vx-arrowhead" points="496.0,334 487.0,329 487.0,339"/>
<text class="vx-text-muted" x="390.0" y="338" text-anchor="end">more reuse</text>
</svg>
<figcaption>Figure 3. The GTX480's roofline in per-cycle units (solid), and two lower ceilings that Little's law adds for the 64 × 64 launch (dashed): 4,096 dependent chains keep at most 455 flops per cycle going through 18 cycles of latency, and 32 KiB of loads in flight deliver at most 41 bytes per cycle. At the kernel's 0.25 flops per byte the machine allows 32 flops per cycle and the launch 10. A larger launch lifts the dashed lines to the roof; only more reuse moves the kernel to the right.</figcaption>
</figure>

The figure shows how the two laws fit together. The solid lines belong to the machine and hold for every kernel. The dashed lines belong to one launch, and Little's law puts them there, because the launch has too little in flight to reach the machine's lines. More work in flight lifts the dashed lines until they meet the roof, and no further. Volkov makes the same point about hiding latency: it makes a kernel run faster, but never faster than the peak.[^volkov-throughput]

Only a change in intensity moves a kernel along the roof, and intensity changes when a value fetched once is used many times. On a GPU that reuse comes from **block tiles**. A block of threads that computes a t × t tile of `c` walks `k` in steps of t; at each step it loads a t × t tile of `a` and one of `b` into its shared memory, a small on-chip memory that the threads of one block share, and uses every loaded value t times. [G3](g3-memory-hierarchy.md#why-shared-memory-pays-reuse-counted-by-hand) counts the reads this saves, and [G10](g10-matmul-ladder.md#rung-3-the-block-tile) builds the kernel. It is a different tiling from P3's, where only `b` was reused from a cache: here both inputs are reused, and each is read n / t times. Work out its intensity before you open the next check.

??? check "Ignoring the writes to `c`, what intensity do 16 × 16 and 32 × 32 block tiles give, and on which side of the GTX480's ridge does each land?"

    Each loaded value now serves t multiply-adds, so the traffic per multiply-add falls from 8 bytes to 8 / t, and the intensity rises from 0.25 to t / 4 flops per byte. A 16 × 16 tile gives 4: still left of the ridge at 7.5, bound by bandwidth at 128 × 4 = 512 flops per cycle, a little over half the peak. A 32 × 32 tile gives 8, slightly right of the ridge, where the roof no longer holds it below the peak.

The third example repeats the count with the writes to `c` included, at n = 1024, where the launch is large enough for the machine's roof to be the one that applies.

--8<-- "includes/examples/gpu/g1-throughput-machines/block_tiles.cpp.md"

Crossing the ridge removes one ceiling, not all of them. Volkov's matrix-multiply case study begins with this kind of kernel: NVIDIA's SDK sample, set to 32 × 32 blocks and run on the GTX480 at 1024 × 1024, measured at 242 Gflop/s, under a fifth of the peak. He points to two limits: the bandwidth of shared memory, which caps that kernel at 336 Gflop/s, and blocks so large that only one fits on an SM, so that one block's loads cannot overlap another block's arithmetic.[^volkov-sdk] The first is the roof of another memory level, P3's [which roof](../optimize/p3-roofline.md#which-roof) question; the second is Little's law again. [G5](g5-occupancy.md#when-lower-occupancy-wins) follows the rest of his case study.

## What a compiler can count, and what it must measure

Everything the four ceilings need falls into two groups. The first comes from the program: the number of threads, the multiply-adds per thread, the bytes per multiply-add, and the bytes each thread can have in flight. For a nest with constant bounds, like the stage 10 kernel's, these are known at compile time: Vortex array shapes are constants ([decision 11](../decisions/arrays.md#d11)), and [O8's](../optimize/o8-loops.md#for-vortex) analysis turns constant bounds into trip counts. The second comes from the machine: peak rates, latencies, and how many threads it holds at once. No compiler can derive those from source code. They come from a vendor's documentation, a published measurement with its date, or the reader's own measurement.

Vortex's philosophy draws the same line. Its [second principle](../philosophy.md#2-say-what-to-calculate-then-decide-how-to-run-it) expects a GPU to run a matrix product across thousands of threads; this chapter's arithmetic says why thousands, and when that is still too few. The compiler must explain its performance decisions ([principle 6](../philosophy.md#6-explain-performance-decisions)), and it must never present an unverified performance estimate as a measured result ([responsibilities](../philosophy.md#programmer-and-compiler-responsibilities)). NVIDIA's Best Practices Guide puts this step first in its own method: assess before parallelizing, including an upper bound on what the acceleration can gain.[^bp-apod]

## Measuring it

No GPU figure in this chapter is newer than 2010, and none describes the owner's M4 Pro. Measure your own:

1. Write the kernels by hand, in Metal Shading Language for the M4 Pro or in CUDA on a rented NVIDIA GPU. On the owner's machine, a program that compiles MSL source text at run time works without the offline Metal toolchain (research notes, 2026-09-23).
2. Peak bandwidth: a copy kernel with many threads, each moving several 16-byte values, over arrays much larger than the GPU's caches. Report the effective bandwidth, the bytes read plus the bytes written divided by the median time.
3. Peak arithmetic: many threads, each running several independent chains of multiply-adds, with every result stored so that none is removed.
4. Little's law in reverse: a copy in which each thread walks the array with a stride of the thread count, one 4-byte element at a time, so that it has one load outstanding. Launch it with a growing number of threads, all of which fit on the GPU at once, and plot the bandwidth against the bytes in flight, 4 per thread. Where the curve flattens, the bytes in flight divided by the bandwidth estimate the memory latency. Call it an estimate: the curve bends before its corner.
5. The stage 10 kernel, one thread per output, at n = 64, 128 and 1024. Check each result against the CPU version before timing it: bit for bit when both follow the same floating-point rules, and within a stated tolerance otherwise ([G14](g14-measuring-gpu-code.md#a-launch-is-not-a-kernel)). Compare each rate with the four ceilings your own figures give.
6. Time every kernel under the protocol of [P1](../optimize/p1-measure-first.md): many launches, warm-up discarded, the median with its spread. [G14](g14-measuring-gpu-code.md) covers the tools, including Xcode's for Metal.

| Machine | Date | Peak (GFlop/s) | Bandwidth (GB/s) | Ridge (flops/byte) | Bytes in flight at the knee | Latency estimate (ns) |
| --- | --- | --- | --- | --- | --- | --- |
| | | | | | | |

| n | Threads | Predicted binding ceiling | Measured rate (GFlop/s) | Percent of peak |
| --- | --- | --- | --- | --- |
| 64 | 4,096 | | | |
| 128 | 16,384 | | | |
| 1024 | 1,048,576 | | | |

Record the machine, the operating system or driver version, the compiler and the date with the tables.

## For Vortex

!!! vortex "Exercise"

    Vortex's v0.1 compiler targets CPUs only ([stage 10](../compiler/guide/stage-10-matrix-multiplication.md)), so this is an analysis that prints a report, not code generation.

    **Build** a throughput estimate for a loop nest whose outer loops can run in parallel, mapped one thread per iteration of those loops: how much work the launch keeps in flight, how much a target needs, and which of this chapter's four ceilings binds. Print it as a remark in the stream from [O1](../optimize/o1-optimizer-contract.md#for-vortex), next to the intensity estimate from [P3's exercise](../optimize/p3-roofline.md#for-vortex).

    1. A **target description**, a record of the facts about one machine that the estimate needs: peak multiply-adds per cycle, arithmetic latency, memory bytes per cycle, memory latency, the number of cores and the threads each core holds at once. Each entry records its source, a citation with its date or your measurement with the machine and date, or is marked unknown. Write one for the GTX480 from Volkov's slides, and one from your own "Measuring it" table.
    2. From the nest, using the trip counts from [O8's exercise](../optimize/o8-loops.md#for-vortex): the number of threads (the product of the parallel loops' trip counts), the multiply-adds per thread, and the bytes each multiply-add loads, from the element sizes in your layout document.
    3. The threads in flight, the smaller of the thread count and what the target holds at once, and the four ceilings in multiply-adds per cycle, under this chapter's assumptions: no reuse, one dependent chain per thread, and one step's loads in flight per thread. Name the smallest.
    4. A remark that carries the word "estimate", the binding ceiling and every assumption, such as "estimate: `multiply` launches 4,096 threads with 32,768 bytes in flight of 102,400 needed (GTX480, Volkov 2010); memory latency binds at 5.12 multiply-adds per cycle, 1.1% of peak". A ceiling that needs an unknown figure is printed as unknown, and so is the bound whenever that ceiling could be the smallest.

    **Not yet:** choosing the thread mapping or block shape (the exercises of [G2](g2-simt.md#for-vortex) and [G4](g4-memory-performance.md#for-vortex)); reuse through caches or shared-memory tiles ([G3](g3-memory-hierarchy.md), [G10](g10-matmul-ladder.md)); occupancy limits from registers and shared memory ([G5](g5-occupancy.md)); reading hardware counters ([G14](g14-measuring-gpu-code.md)); generating GPU code ([M12](../mlir/m12-vortex-gpu-path.md) weighs the paths).

    **Proof that it works:**

    - Golden remarks for `multiply` at `[f32; 64, 64]` and at `[f32; 1024, 1024]` against your GTX480 description, with the ceilings this chapter's second example prints: memory latency binds at 5.12 for the first, memory bandwidth at 16 for the second.
    - Golden remarks at `[f32; 113, 113]` and `[f32; 114, 114]` on the same description, one on each side of the answer to the third check question: memory latency binds at 113 and bandwidth at 114.
    - A golden remark for a description whose memory latency is unknown: it names the missing figure and prints no bound.
    - Property tests over a few hundred random descriptions and array sizes: raising a latency, or lowering a peak or a bandwidth, never raises the bound, and growing an array never lowers the threads in flight.
    - A test over every remark your compiler emits that fails if a throughput remark lacks the word "estimate" or contains "measured", as P3's exercise does for intensity.
    - Your second "Measuring it" table, filled in, with the predicted binding ceiling next to each measured rate.

## Key ideas

!!! recap "Questions you can now answer"

    - **What does each design spend its transistors on?** A latency-optimized core spends them on caches, prediction and look-ahead that finish one thread sooner; a throughput-optimized chip spends them on execution units and on keeping thousands of threads' state on chip.
    - **What does Little's law say about a pipe?** Work in flight equals latency times throughput; with less in flight the pipe delivers the work in flight divided by the latency, and with more, its peak.
    - **How much must be in flight to keep a GTX480's memory busy?** Up to about 100 KiB, 800 cycles × 128 bytes per cycle, from many threads with a few bytes each or from fewer threads with more.
    - **Why can a small launch not use a big GPU?** Its threads hold too little work in flight: at 64 × 64 the stage 10 kernel keeps 32 KiB of loads outstanding, a third of what the GTX480's memory needs.
    - **Why does a GPU's ridge point sit far to the right?** It adds arithmetic faster than bandwidth: a GTX480 can do about 15 multiply-adds for each 4-byte word it loads, so only kernels with much reuse reach its peak.
    - **Does hiding latency raise a ceiling?** No. More work in flight lets a kernel reach the lowest ceiling; only more reuse, or another machine, moves a ceiling.
    - **What may a compiler print about all this?** Counts from the program, and bounds computed from target figures with sources, always labelled as estimates.

## Where this comes back

!!! next "You will use this again in"

    - [G2. The SIMT execution model](g2-simt.md): *throughput-optimized design*, *free switching between threads*
    - [G3. The GPU memory hierarchy](g3-memory-hierarchy.md): *memory latency*, *block tile*
    - [G5. Occupancy and latency hiding](g5-occupancy.md): *Little's law*, *work in flight*, *threads held at once*
    - [G10. The GPU matmul ladder](g10-matmul-ladder.md): *block tile*, *ridge point*
    - [G14. Measuring GPU code](g14-measuring-gpu-code.md): *ridge point*, *estimate against measurement*
    - [G15. Beyond GPUs: systolic arrays and accelerators](g15-systolic-arrays.md): *throughput-optimized design*, *work in flight*
    - [M12. Designing Vortex's GPU path](../mlir/m12-vortex-gpu-path.md): *target description*

## Sources and further reading

Read Volkov's talk first: slides 6 to 29 make this chapter's argument, for arithmetic and then for memory, and the rest of the talk is [G5](g5-occupancy.md)'s. Then read sections 1, 2.1 and 4.1 of Little's anniversary paper for the law and its use in computer architecture, and section 3 of the roofline paper for the model [P3](../optimize/p3-roofline.md) built on. Volkov's 2016 dissertation is the long form of this chapter's question: its abstract names the pitfalls of applying Little's law to GPUs and asks how the number of threads needed to hide latency depends on arithmetic intensity.[^volkov-thesis]

[^pg-gpus]: NVIDIA, "CUDA Programming Guide", v13.4.2, section 1.1.2, "The Benefits of Using GPUs". <https://docs.nvidia.com/cuda/cuda-programming-guide/01-introduction/introduction.html#the-benefits-of-using-gpus>
[^pg-hw]: NVIDIA, "CUDA Programming Guide", v13.4.2, section 3.2.2, "Hardware Implementation", and section 3.2.2.1, "SIMT Execution Model": in-order issue with no branch prediction or speculative execution, and threads scheduled in warps of 32. <https://docs.nvidia.com/cuda/cuda-programming-guide/03-advanced/advanced-kernel-programming.html#hardware-implementation>
[^pg-mt]: NVIDIA, "CUDA Programming Guide", v13.4.2, section 3.2.2.2, "Hardware Multithreading": execution context kept on chip for a warp's lifetime, switching at no cost, and the scheduler's choice at each issue cycle. <https://docs.nvidia.com/cuda/cuda-programming-guide/03-advanced/advanced-kernel-programming.html#hardware-multithreading>
[^little]: John D. C. Little, "Little's Law as Viewed on Its 50th Anniversary", Operations Research 59(3), 2011, pages 536 to 549: section 1 (the law), section 2.1 (Theorem LL.1 and the remarks that it holds without stationarity and under any queue discipline) and section 4.1 (requests to memory, with latency as the time in the system). <https://doi.org/10.1287/opre.1110.0940>; copy read at <https://people.cs.umass.edu/~emery/classes/cmpsci691st/readings/OS/Littles-Law-50-Years-Later.pdf>. The original proof is J. D. C. Little, "A Proof for the Queuing Formula: L = λW", Operations Research 9(3), 1961, pages 383 to 387.
[^volkov-latency]: Vasily Volkov, "Better Performance at Lower Occupancy", GPU Technology Conference, 22 September 2010, slide 7 (about 20 cycles for arithmetic, 400 or more for memory), slide 11 (latency varies between types of operation) and slide 28 (about 18 cycles for arithmetic on GF100, the GTX480's chip). <https://www.nvidia.com/content/GTC-2010/pdfs/2238_GTC2010.pdf>
[^volkov-throughput]: Volkov, "Better Performance at Lower Occupancy", GTC 2010, slides 8 and 9: throughput as a rate and latency as a time, the example of the two confused, 1.3 Tflop/s as 480 multiply-adds per cycle and 177 GB/s as about 32 four-byte loads per cycle, and hiding latency as a way to run faster but not faster than the peak. <https://www.nvidia.com/content/GTC-2010/pdfs/2238_GTC2010.pdf>
[^volkov-memory]: Volkov, "Better Performance at Lower Occupancy", GTC 2010, slides 28, 29 and 32: the same formula for arithmetic and for memory (under 800 cycles, under 177 GB/s, under 100 KB in flight), 25,000 threads fetching 4 bytes or 1,000 fetching 100 bytes, and threads that stall on a data dependency, not on the load. <https://www.nvidia.com/content/GTC-2010/pdfs/2238_GTC2010.pdf>
[^volkov-chip]: Volkov, "Better Performance at Lower Occupancy", GTC 2010, slides 15 and 45 (the GTX480's 15 SMs) and slide 58 (at most 1,536 threads per SM). <https://www.nvidia.com/content/GTC-2010/pdfs/2238_GTC2010.pdf>
[^volkov-sources]: Volkov, "Better Performance at Lower Occupancy", GTC 2010, slides 12 to 14 (threads and independent instructions as two sources of parallelism), 15 to 22 (the arithmetic experiment on a GTX480) and 29 to 39 (more or wider fetches per thread, and the copy experiments on a GTX480). <https://www.nvidia.com/content/GTC-2010/pdfs/2238_GTC2010.pdf>
[^volkov-sdk]: Volkov, "Better Performance at Lower Occupancy", GTC 2010, slides 52 to 54: the CUDA SDK's matrix multiply on a GTX480 at 1024 × 1024 with 32 × 32 blocks, 242 Gflop/s, a shared-memory bound of 336 Gflop/s, and one block per SM; slide 58: two smaller blocks fit, because an SM holds at most 1,536 threads. <https://www.nvidia.com/content/GTC-2010/pdfs/2238_GTC2010.pdf>
[^volkov-thesis]: Vasily Volkov, "Understanding Latency Hiding on GPUs", PhD dissertation, University of California, Berkeley, technical report UCB/EECS-2016-143, 12 August 2016: the abstract. <https://www2.eecs.berkeley.edu/Pubs/TechRpts/2016/EECS-2016-143.html>
[^roofline]: Samuel Williams, Andrew Waterman and David Patterson, "Roofline: An Insightful Visual Performance Model for Multicore Architectures", Communications of the ACM 52(4), April 2009, pages 65 to 76, <https://doi.org/10.1145/1498765.1498785>; preprint <https://people.eecs.berkeley.edu/~kubitron/cs252/handouts/papers/RooflineVyNoYellow.pdf>, section 3: the attainable-performance formula, the ridge point and what its position means, and the Opteron X4's ridge at 4.4.
[^bp-apod]: NVIDIA, "CUDA C++ Best Practices Guide", 13.4, section 2.2, "Assess, Parallelize, Optimize, Deploy", and section 2.2.4, "Deploy": the assess step's upper bound on the attainable speedup. <https://docs.nvidia.com/cuda/cuda-c-best-practices-guide/index.html#assess-parallelize-optimize-deploy>

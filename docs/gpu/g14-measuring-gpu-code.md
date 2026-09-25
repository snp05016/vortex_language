# G14. Measuring GPU code

<p class="page-intro">A timing says that a kernel got faster; it does not say why. This chapter shows how to time GPU work honestly, how to read the reports that NVIDIA's, AMD's and Apple's profilers build from hardware counters, and how to explain a change with a timing and the one counter it was meant to move, keeping every estimate apart from every measurement, as Vortex's philosophy demands.</p>

<p class="vx-meta" markdown="1">Level: Intermediate · Reading time: about 45 minutes · Builds on: [G4. Memory performance: coalescing and bank conflicts](g4-memory-performance.md), [P1. Measure first](../optimize/p1-measure-first.md)</p>

???+ remember "Before you start, remember"

    ??? question "Why is a single timed run not a measurement, and what do you report instead?"

        One run is one sample of a noisy process: the scheduler, the clock speed and everything else on the machine change the time without changing the work. Report the median over many launches with a confidence interval, the number of launches, and the setup.

        Introduced in [P1. Measure first](../optimize/p1-measure-first.md#what-to-report-instead-of-one-number).

    ??? question "What does one warp-wide load cost in global memory?"

        The number of distinct aligned 32-byte sectors that the warp's 32 addresses touch, on current NVIDIA GPUs. Neighbouring `f32` addresses cost four sectors; a stride of 32 bytes or more costs one sector per lane.

        Introduced in [G4. Memory performance: coalescing and bank conflicts](g4-memory-performance.md#thirty-two-addresses-one-instruction).

    ??? question "What does the ridge point of a roofline mark?"

        The smallest operational intensity, in flops per byte of memory traffic, at which a kernel can reach the machine's peak arithmetic rate. Left of it, bandwidth sets the bound; right of it, the peak does.

        Introduced in [P3. The roofline model](../optimize/p3-roofline.md#the-roofline-two-lines-and-a-ridge).

    ??? question "Why does a GPU keep many warps resident on each multiprocessor?"

        To hide latency: while one warp waits for a load, the scheduler issues from another. Little's law says how many are needed: latency times the rate at which the machine can start work.

        Introduced in [G5. Occupancy and latency hiding](g5-occupancy.md#how-many-warps-are-enough-littles-law).

!!! goals "In this chapter"

    - Time a GPU kernel correctly, knowing that a launch returns before the kernel finishes.
    - Read a speed-of-light report as two percentages, each the busiest unit of its kind, and say what each rules in or out.
    - Compute percentages of peak and a roofline bound by hand from published figures, and find where they stop explaining a result.
    - Recognize when warp stall reasons are the right next question, and why a high stall count can be harmless.
    - Explain a measured speedup with exactly two facts, a timing and one counter, and label every estimate as one.

## A launch is not a kernel

Start where [P1](../optimize/p1-measure-first.md#a-stopwatch-is-not-a-measurement) started: wrap the work in a clock. On a GPU the first attempt goes wrong in a new way. The CPU does not run the kernel; it asks the GPU to. In CUDA, every kernel launch is **asynchronous**: the call returns control to the CPU before the kernel has finished.[^bp-timing] A clock read on each side of the launch times the launch, not the kernel.

NVIDIA's Best Practices Guide gives two correct ways. A CPU timer works if the program calls `cudaDeviceSynchronize()`, which blocks until every earlier GPU call has finished, immediately before starting the timer and again before stopping it.[^bp-timing] Or the program can record two **events** in the stream of GPU work around the kernel; the GPU writes a timestamp when it reaches each one, on its own clock, and the guide gives the resolution of the elapsed time as about half a microsecond.[^bp-timing]

Metal has the same split. A **command buffer** holds encoded GPU work, and after it completes, its `gpuStartTime` and `gpuEndTime` properties hold the host times at which the GPU started and finished it; both read `0.0` until then.[^mtl-gputime] They time a whole command buffer. To time one dispatch, put it alone in a command buffer, or put `k` copies in one and divide by `k`, as P1 did for calls too short for the clock, and say which you did.

Everything else P1 taught still applies, with GPU-specific causes of noise. Clock speeds change with load and temperature, so the first launches of a program often run at a lower clock than later ones; NVIDIA's profiler guide describes exactly this effect on its own measurements.[^ncu-clock] So discard a few warm-up launches and say how many. Then report the median over launches with its interval, and turn times into rates the reader can compare with the hardware: GFLOP/s for arithmetic, and **effective bandwidth**, the bytes a kernel must read and write divided by its time, for data movement.[^bp-bandwidth]

Before timing anything, check the answer. A GPU compiler may change floating-point results unless told not to: Metal's older `fastMathEnabled` option allows optimizations that may violate IEEE 754 and defaults to true, and its replacement, `mathMode`, has a `safe` setting that forbids transformations that could change results.[^mtl-math] Compare a GPU result with a CPU reference bit for bit only when both follow the same rules; otherwise the gate is a tolerance, and the report says which.

## What a profiler adds

A median time, however carefully taken, is one number for everything the hardware did. A **profiler** runs the kernel and reads back **hardware performance counters**: counters built into the chip that count events as it works, such as instructions issued, bytes moved between two units, or cycles a unit was busy. [P4](../optimize/p4-counters-and-tools.md) introduced them on a CPU. A GPU has far more of them, spread over dozens of multiprocessors, cache slices and memory controllers.

Nsight Compute, NVIDIA's kernel profiler, names its counters after the unit that counts: `sm__inst_executed` counts instructions executed on the **streaming multiprocessors** (SMs), and `l1tex__data_bank_conflicts_pipe_lsu` counts data-bank conflicts in the L1/TEX cache unit. Each counter can be summed, averaged or maximized over all instances of its unit, and turned into a rate or a percentage of the unit's peak, by a suffix such as `.sum` or `.sum.pct_of_peak_sustained_elapsed`.[^ncu-metrics] Every counter has a peak rate in the tool's database, which is what lets it say how close a kernel came to the hardware's limit.[^ncu-metrics]

Nobody reads thousands of counters. Nsight Compute groups them into **sections**, each a report on one question, and sections into sets; the default set collects a small number of high-level metrics, and `--set full` collects every section.[^ncu-sets] The sections are also the order in which an expert reads them, which the rest of this chapter follows (Figure 1).

<figure class="vx-figure">
<svg viewBox="0 0 760 440" role="img" aria-label="The order in which to read a GPU profile: duration, then speed of light, then one of three detailed reports, then one change and a new duration" aria-describedby="g14-f1-desc">
<title id="g14-f1-title">The order in which to read a GPU profile</title>
<desc id="g14-f1-desc">A flow chart. Step 1: time the kernel, the median over launches. Step 2: the speed-of-light report, compute percent and memory percent. From step 2 three arrows lead to three reports. If memory percent is high: Memory Workload Analysis, with sectors per request, bank conflicts and hit rates. If both are low: Occupancy and Scheduler Statistics, asking whether issue slots are skipped, and below it Warp State Statistics and Source Counters, which stall and at which instruction. If compute percent is high: Compute Workload Analysis, with pipe utilization, then Instruction Statistics. All three paths lead to step 3: change one thing, then time it again, because the duration decides.</desc>
<rect class="vx-box-strong" x="270" y="16" width="220" height="50"/>
<text class="vx-text" x="380" y="37" text-anchor="middle">1. Time the kernel</text>
<text class="vx-text-muted" x="380" y="56" text-anchor="middle">median over launches (P1)</text>
<line class="vx-line" x1="380" y1="66" x2="380" y2="92"/>
<polygon class="vx-arrowhead" points="375,90 380,98 385,90"/>
<rect class="vx-box-strong" x="270" y="98" width="220" height="50"/>
<text class="vx-text" x="380" y="119" text-anchor="middle">2. Speed of light</text>
<text class="vx-text-muted" x="380" y="138" text-anchor="middle">compute % and memory %</text>
<line class="vx-line" x1="300" y1="148" x2="130" y2="190"/>
<polygon class="vx-arrowhead" points="131,184 124,192 134,193"/>
<line class="vx-line" x1="380" y1="148" x2="380" y2="186"/>
<polygon class="vx-arrowhead" points="375,184 380,192 385,184"/>
<line class="vx-line" x1="460" y1="148" x2="630" y2="190"/>
<polygon class="vx-arrowhead" points="626,184 636,192 629,193"/>
<text class="vx-text-accent" x="150" y="168" text-anchor="middle">memory % high</text>
<text class="vx-text-accent" x="424" y="178">both low</text>
<text class="vx-text-accent" x="612" y="168" text-anchor="middle">compute % high</text>
<rect class="vx-box" x="20" y="194" width="220" height="80"/>
<text class="vx-text" x="130" y="216" text-anchor="middle">Memory Workload Analysis</text>
<text class="vx-text-muted" x="130" y="238" text-anchor="middle">sectors per request,</text>
<text class="vx-text-muted" x="130" y="256" text-anchor="middle">bank conflicts, hit rates (G4)</text>
<rect class="vx-box" x="270" y="194" width="220" height="80"/>
<text class="vx-text" x="380" y="216" text-anchor="middle">Occupancy, Scheduler</text>
<text class="vx-text" x="380" y="236" text-anchor="middle">Statistics</text>
<text class="vx-text-muted" x="380" y="258" text-anchor="middle">are issue slots skipped? (G5)</text>
<rect class="vx-box" x="520" y="194" width="220" height="80"/>
<text class="vx-text" x="630" y="216" text-anchor="middle">Compute Workload Analysis</text>
<text class="vx-text-muted" x="630" y="238" text-anchor="middle">pipe utilization; then</text>
<text class="vx-text-muted" x="630" y="256" text-anchor="middle">Instruction Statistics</text>
<line class="vx-line" x1="380" y1="274" x2="380" y2="296"/>
<polygon class="vx-arrowhead" points="375,294 380,302 385,294"/>
<rect class="vx-box" x="270" y="302" width="220" height="62"/>
<text class="vx-text" x="380" y="324" text-anchor="middle">Warp State, Source Counters</text>
<text class="vx-text-muted" x="380" y="346" text-anchor="middle">which stall, which instruction</text>
<line class="vx-line" x1="130" y1="274" x2="130" y2="396"/>
<line class="vx-line" x1="130" y1="396" x2="206" y2="396"/>
<polygon class="vx-arrowhead" points="204,391 212,396 204,401"/>
<line class="vx-line" x1="630" y1="274" x2="630" y2="396"/>
<line class="vx-line" x1="630" y1="396" x2="554" y2="396"/>
<polygon class="vx-arrowhead" points="556,391 548,396 556,401"/>
<line class="vx-line" x1="380" y1="364" x2="380" y2="374"/>
<polygon class="vx-arrowhead" points="375,372 380,380 385,372"/>
<rect class="vx-box-strong" x="212" y="380" width="336" height="44"/>
<text class="vx-text" x="380" y="400" text-anchor="middle">3. Change one thing, then time it again</text>
<text class="vx-text-muted" x="380" y="417" text-anchor="middle">the duration decides, not a percentage</text>
</svg>
<figcaption>Figure 1. The reading order this chapter follows, with the names of Nsight Compute's sections. The speed-of-light report chooses which detailed report to open; the loop closes on a new duration, because the profiler's own guide calls duration the ground truth for progress.</figcaption>
</figure>

## Speed of light first

The first section to read is called **GPU Speed Of Light Throughput**. For each unit of the GPU it reports the achieved percentage of that unit's theoretical maximum, and it summarizes them in two headline numbers, one for compute and one for memory.[^ncu-sections] "Speed of light" means the hardware's own ceiling: no change to the code moves a unit's peak rate, so the percentage says how much headroom that unit has left.

Each headline number is a **throughput metric**, and a throughput metric reports the largest percentage among the counters it is built from.[^ncu-metrics] So "memory 70%" does not mean that 70% of the DRAM bandwidth was used. It means that some memory unit, perhaps the L1 cache, perhaps the L2, perhaps DRAM, reached 70% of its own peak, and the breakdown below the headline says which.[^ncu-sections] The same holds for compute, over the SM's pipelines. The peak is a **sustained** rate, the most a unit can keep up over a long run for typical operations, so a percentage can occasionally exceed 100% in edge cases.[^ncu-metrics]

Read the two numbers together, and there are three outcomes:

- **Memory high, compute low.** Some memory unit is the limit. The Memory Workload Analysis section says which, and its tables hold [G4](g4-memory-performance.md)'s quantities: sectors per request in the L1 table and bank conflicts in the shared-memory table.[^ncu-l1]
- **Compute high, memory low.** Some pipeline is the limit. The Compute Workload Analysis section reports the utilization of each pipeline, and the Instruction Statistics section the mix of instructions executed.[^ncu-sections]
- **Both low.** No unit is near its limit, so the hardware spent much of its time waiting. The next sections to read are Occupancy and Scheduler Statistics, then the stalls.

A fourth case, both high, is a kernel near what its hardware can do. This chapter draws the line between high and low at 60% of peak: a teaching threshold, not a value taken from any vendor's documentation.

??? check "A kernel's speed-of-light report shows memory at 85% and DRAM throughput, in the breakdown, at 12%. Which unit do you look at next, and what would you expect to fix?"

    Not DRAM. The headline is the busiest memory unit, and DRAM is far from it, so one of the on-chip units, such as the L1 cache or the shared memory behind it, is near its peak. Open Memory Workload Analysis and look for a high sectors-per-request count or bank conflicts: G4's problems, which waste on-chip bandwidth even when little data reaches DRAM.

## A worked example, by hand

Profiler percentages are ratios of counts to peaks, so you can compute rough ones yourself from published figures. That is worth doing once, because it shows both what the ratios say and where they stop saying anything.

Simon Boehm's worklog of a CUDA matrix multiplication gives every input needed. His GPU, an RTX A6000, is advertised at 30 TFLOP/s of FP32 arithmetic and 768 GB/s of global-memory bandwidth. He multiplies 4092 × 4092 `f32` matrices, which he counts as 137 GFLOP of work and at least 268 MB of traffic: three matrices read and one written.[^boehm] His naive kernel, one thread per output with neighbouring threads on neighbouring rows, reached 309.0 GFLOP/s, and he read 15 GB/s of global-memory throughput from the profiler; his second kernel, which coalesces the loads by giving neighbouring threads neighbouring columns, reached 1,986.5 GFLOP/s and 110 GB/s.[^boehm]

Now the arithmetic. The naive kernel ran at 309 / 30,000 = 1.0% of the FP32 peak and 15 / 768 = 2.0% of the bandwidth. By this chapter's rule, both are low. The coalesced kernel reached 6.6% and 14.3%: both still low, yet it ran 6.4 times faster. The example below does these sums and one more step.

--8<-- "includes/examples/gpu/g14-measuring-gpu-code/napkin_sol.cpp.md"

The more useful step is the traffic. Time is work divided by rate: 137 GFLOP at 309 GFLOP/s is 0.444 seconds, close to the half second Boehm reports.[^boehm] At 15 GB/s for 0.444 seconds, about 6.7 GB crossed the global-memory interface. For the coalesced kernel, 110 GB/s for 0.069 seconds is about 7.6 GB. These are derived numbers: they assume the reported rate held for the whole run, and the program labels them so.

So the change that made the kernel 6.4 times faster did not reduce the traffic to DRAM at all; if anything it grew. Whatever coalescing saved, it saved above DRAM. That is G4's prediction: a sector count is what a warp's request asks of the L1 cache, and for one step of the inner loop the naive mapping asks for 33 sectors where the coalesced one asks for 5, as G4 counted for the same two mappings. The counter that should confirm it is **sectors per request** in the L1/TEX table.[^ncu-l1] Boehm does not report it, so for this pair it stays a hypothesis with a named test.

Nsight Compute can also plot a kernel on a **roofline** chart, the model from [P3](../optimize/p3-roofline.md), which it builds from the GPU's peak rate and bandwidth and the kernel's **arithmetic intensity**, its flops per byte of memory traffic (P3's operational intensity).[^ncu-roofline] Figure 2 draws it for Boehm's figures. The ridge point is 30,000 / 768 = 39.1 FLOP/B.

<figure class="vx-figure">
<svg viewBox="0 0 760 440" role="img" aria-label="A roofline for the advertised RTX A6000 figures, with Boehm's two kernels far below their bounds" aria-describedby="g14-f2-desc">
<title id="g14-f2-title">A roofline for the advertised RTX A6000 figures</title>
<desc id="g14-f2-desc">A log-log chart of GFLOP/s against flops per byte of DRAM traffic. The roof rises diagonally at 768 gigabytes per second until the ridge point at 39.1 flops per byte and 30 teraflops per second, then stays flat. A point on the diagonal at 0.25 flops per byte and 192 gigaflops per second marks the naive kernel if no load were ever reused. Boehm's naive kernel sits at about 20.6 flops per byte and 309 gigaflops per second, and his coalesced kernel at about 18.1 flops per byte and 1987 gigaflops per second; a dotted line from the naive kernel rises to its bound of about 15,800 gigaflops per second on the roof. A mark at 511 flops per byte on the flat roof shows the least possible traffic.</desc>
<line class="vx-line" x1="100" y1="400" x2="700" y2="400"/>
<line class="vx-line" x1="100" y1="400" x2="100" y2="50"/>
<text class="vx-text-muted" x="100" y="420" text-anchor="middle">0.1</text>
<text class="vx-text-muted" x="250" y="420" text-anchor="middle">1</text>
<text class="vx-text-muted" x="400" y="420" text-anchor="middle">10</text>
<text class="vx-text-muted" x="550" y="420" text-anchor="middle">100</text>
<text class="vx-text-muted" x="700" y="420" text-anchor="middle">1000</text>
<text class="vx-text" x="400" y="436" text-anchor="middle">arithmetic intensity (FLOP per byte of DRAM traffic, log scale)</text>
<text class="vx-text-muted" x="92" y="404" text-anchor="end">10</text>
<text class="vx-text-muted" x="92" y="319" text-anchor="end">100</text>
<text class="vx-text-muted" x="92" y="234" text-anchor="end">1,000</text>
<text class="vx-text-muted" x="92" y="149" text-anchor="end">10,000</text>
<text class="vx-text-muted" x="92" y="64" text-anchor="end">100,000</text>
<text class="vx-text" x="20" y="36">GFLOP/s (log scale)</text>
<line class="vx-line" x1="100" y1="324.8" x2="488.8" y2="104.4"/>
<line class="vx-line" x1="488.8" y1="104.4" x2="700" y2="104.4"/>
<text class="vx-text-muted" x="262" y="222" transform="rotate(-29.6 262 222)">768 GB/s</text>
<text class="vx-text-muted" x="560" y="96">30 TFLOP/s peak</text>
<line class="vx-line" x1="488.8" y1="104.4" x2="488.8" y2="400" stroke-dasharray="3 5"/>
<text class="vx-text-accent" x="496" y="392">ridge 39.1</text>
<circle class="vx-dot" cx="159.7" cy="290.9" r="5"/>
<text class="vx-text-muted" x="170" y="306">naive, no reuse: 0.25, 192</text>
<line class="vx-line" x1="447.1" y1="273.3" x2="447.1" y2="128.1" stroke-dasharray="2 4"/>
<circle class="vx-dot" cx="447.1" cy="273.3" r="6"/>
<text class="vx-text" x="458" y="290">kernel 1: 309</text>
<circle class="vx-dot" cx="438.7" cy="204.7" r="6"/>
<text class="vx-text" x="452" y="214">kernel 2: 1,987</text>
<text class="vx-text-muted" x="455" y="150">kernel 1 bound at its</text>
<text class="vx-text-muted" x="455" y="166">intensity: 15,821</text>
<circle class="vx-dot" cx="656.2" cy="104.4" r="5"/>
<text class="vx-text-muted" x="600" y="130">least traffic: 511</text>
</svg>
<figcaption>Figure 2. The roofline from the A6000's advertised peaks, with the points <code>napkin_sol.cpp</code> derives from Boehm's published figures. With no reuse at all, the naive kernel would sit on the roof at 192 GFLOP/s; it reached 309, so the caches caught most of its loads. At their derived intensities both kernels have a bound above 13,000 GFLOP/s and sit far below it: neither DRAM nor the arithmetic peak explains them.</figcaption>
</figure>

Two readings of Figure 2 teach the most. First, the point at 0.25 FLOP/B: Boehm estimates that with no caching at all, each thread would load a whole row and a whole column, 548 GB of traffic in all.[^boehm] At 768 GB/s that would cap the kernel at 192 GFLOP/s. It ran faster than that, so the caches served most of its loads.

Second, the gap: at its derived intensity the naive kernel could reach about 15,800 GFLOP/s before DRAM stopped it, and it reached 309, about 2% of its bound. When a kernel sits far below both roofs, the roofline has said all it can; the limit is elsewhere, in an on-chip unit or in latency. Nsight's hierarchical roofline charts, which add L1 and L2 ceilings to the DRAM one, are the next place to look.[^ncu-roofline]

??? check "Boehm reports that cuBLAS reached 23,249.6 GFLOP/s on the same problem while loading about 500 MB from global memory in total. Where does it sit on Figure 2, and what does that say?"

    Its intensity is 137 GFLOP / 0.5 GB, about 274 FLOP/B, far right of the ridge at 39.1, so its bound is the flat roof, 30,000 GFLOP/s. It reached 23,249.6 / 30,000, about 77.5% of that peak. It is compute bound and close to the advertised speed of light. Remember that the advertised peak is not the profiler's sustained peak, so the profiler's own percentage would differ.

## When neither is busy: issue slots and stalls

A kernel low on both axes spends cycles not doing work. To see why, look at the **warp scheduler**, the unit that picks, each cycle, one warp whose next instruction can issue. Nsight Compute's Scheduler Statistics section uses four words for its warps.[^ncu-sections] The **theoretical** warps are the most the launch configuration allows. **Active** warps are resident on the scheduler. **Eligible** warps are active and not stalled, ready to issue. The **issued** warp is the one chosen. A cycle with no eligible warp is a **skipped issue slot**, and many skipped slots mean poor latency hiding.[^ncu-sections]

**Occupancy**, from [G5](g5-occupancy.md), is the ratio of active warps per multiprocessor to the maximum possible. The Occupancy section shows the **theoretical occupancy**, computed from the launch and the kernel's use of registers and shared memory, next to the **achieved occupancy** measured while it ran; a large gap between them usually means a badly unbalanced workload.[^ncu-sections] The first is a calculation you could do before running anything. The second is a measurement. The profiler shows them side by side, and a careful report keeps them apart.

A warp that is active but not eligible is **stalled**, and the reason matters. Nsight Compute samples it: at a fixed interval, from every 32 cycles on small GPUs to every 2,048 on large ones, each multiprocessor picks a random active warp and records its program counter and its state.[^ncu-sampling] The states have names. **Long scoreboard** means waiting for the result of a load through the L1 cache, **MIO throttle** means the queue for shared-memory and other memory-input/output instructions is full, **barrier** means waiting for other warps at a block barrier, and **not selected** means eligible, but the scheduler picked another warp.[^ncu-stalls]

The guide is blunt about when to read them: only when the schedulers fail to issue every cycle.[^ncu-sections] A stall that another warp covers costs nothing. Its reference adds that "selected" and "not selected" are scheduler states, not true stalls, that the target is issue-slot utilization rather than zero stalls, and that a sample is charged to the instruction waiting for a value, not to the one that produces it.[^ncu-stalls] A long-scoreboard stall usually shows up on an arithmetic instruction; the load that caused it is somewhere above.

The next example makes this concrete with a toy scheduler: each warp repeats a load and one instruction that uses its result, and the load's value arrives 15 cycles later. The latency is invented for the model. Every cycle, the program records every warp's state, so it reports exact counts where a profiler samples, and prints them the way Nsight's Warp State Statistics section does: cycles spent in each state per issued instruction.[^ncu-sections]

--8<-- "includes/examples/gpu/g14-measuring-gpu-code/warp_states.cpp.md"

Read the columns. Long scoreboard is 7.00 cycles per instruction in every row: each load-use pair waits 14 cycles, and adding warps does not shorten the wait. What changes is the issue column. One warp leaves 7 slots of every 8 empty; 16 warps fill them all. By Little's law, 2 instructions per 16 cycles per warp would saturate the scheduler with 8 warps, but warps that happen to become ready together delay each other, so 8 reach 69.6% and it takes 16 to fill every slot. Past that point more warps only raise "not selected", which the guide reads as a sign of more warps than needed.[^ncu-stalls] Figure 3 shows the first 32 cycles.

<figure class="vx-figure">
<svg viewBox="0 0 800 330" role="img" aria-label="Warp states over 32 cycles for 2 warps and for 8 warps, from the toy scheduler" aria-describedby="g14-f3-desc">
<title id="g14-f3-title">Warp states over 32 cycles for 2 and for 8 warps</title>
<desc id="g14-f3-desc">Two grids with one row per warp and one column per cycle. Filled cells are cycles in which that warp was selected and issued, accented cells are cycles in which it was ready but not selected, and plain cells are cycles in which it waited for a load. With 2 warps, the issued row below the grid has marks in 6 of 32 cycles. With 8 warps, it has marks in 24 of 32 cycles, and ready-but-not-selected cells appear in several rows.</desc>
<text class="vx-text" x="20" y="32">2 warps: most issue slots are empty</text>
<text class="vx-text-muted" x="20" y="52">warp 0</text>
<rect class="vx-cell-on" x="150" y="40" width="16" height="14"/>
<rect class="vx-box" x="168" y="40" width="16" height="14"/>
<rect class="vx-box" x="186" y="40" width="16" height="14"/>
<rect class="vx-box" x="204" y="40" width="16" height="14"/>
<rect class="vx-box" x="222" y="40" width="16" height="14"/>
<rect class="vx-box" x="240" y="40" width="16" height="14"/>
<rect class="vx-box" x="258" y="40" width="16" height="14"/>
<rect class="vx-box" x="276" y="40" width="16" height="14"/>
<rect class="vx-box" x="294" y="40" width="16" height="14"/>
<rect class="vx-box" x="312" y="40" width="16" height="14"/>
<rect class="vx-box" x="330" y="40" width="16" height="14"/>
<rect class="vx-box" x="348" y="40" width="16" height="14"/>
<rect class="vx-box" x="366" y="40" width="16" height="14"/>
<rect class="vx-box" x="384" y="40" width="16" height="14"/>
<rect class="vx-box" x="402" y="40" width="16" height="14"/>
<rect class="vx-cell-on" x="420" y="40" width="16" height="14"/>
<rect class="vx-box-accent" x="438" y="40" width="16" height="14"/>
<rect class="vx-cell-on" x="456" y="40" width="16" height="14"/>
<rect class="vx-box" x="474" y="40" width="16" height="14"/>
<rect class="vx-box" x="492" y="40" width="16" height="14"/>
<rect class="vx-box" x="510" y="40" width="16" height="14"/>
<rect class="vx-box" x="528" y="40" width="16" height="14"/>
<rect class="vx-box" x="546" y="40" width="16" height="14"/>
<rect class="vx-box" x="564" y="40" width="16" height="14"/>
<rect class="vx-box" x="582" y="40" width="16" height="14"/>
<rect class="vx-box" x="600" y="40" width="16" height="14"/>
<rect class="vx-box" x="618" y="40" width="16" height="14"/>
<rect class="vx-box" x="636" y="40" width="16" height="14"/>
<rect class="vx-box" x="654" y="40" width="16" height="14"/>
<rect class="vx-box" x="672" y="40" width="16" height="14"/>
<rect class="vx-box" x="690" y="40" width="16" height="14"/>
<rect class="vx-box" x="708" y="40" width="16" height="14"/>
<text class="vx-text-muted" x="20" y="68">warp 1</text>
<rect class="vx-box-accent" x="150" y="56" width="16" height="14"/>
<rect class="vx-cell-on" x="168" y="56" width="16" height="14"/>
<rect class="vx-box" x="186" y="56" width="16" height="14"/>
<rect class="vx-box" x="204" y="56" width="16" height="14"/>
<rect class="vx-box" x="222" y="56" width="16" height="14"/>
<rect class="vx-box" x="240" y="56" width="16" height="14"/>
<rect class="vx-box" x="258" y="56" width="16" height="14"/>
<rect class="vx-box" x="276" y="56" width="16" height="14"/>
<rect class="vx-box" x="294" y="56" width="16" height="14"/>
<rect class="vx-box" x="312" y="56" width="16" height="14"/>
<rect class="vx-box" x="330" y="56" width="16" height="14"/>
<rect class="vx-box" x="348" y="56" width="16" height="14"/>
<rect class="vx-box" x="366" y="56" width="16" height="14"/>
<rect class="vx-box" x="384" y="56" width="16" height="14"/>
<rect class="vx-box" x="402" y="56" width="16" height="14"/>
<rect class="vx-box" x="420" y="56" width="16" height="14"/>
<rect class="vx-cell-on" x="438" y="56" width="16" height="14"/>
<rect class="vx-box-accent" x="456" y="56" width="16" height="14"/>
<rect class="vx-cell-on" x="474" y="56" width="16" height="14"/>
<rect class="vx-box" x="492" y="56" width="16" height="14"/>
<rect class="vx-box" x="510" y="56" width="16" height="14"/>
<rect class="vx-box" x="528" y="56" width="16" height="14"/>
<rect class="vx-box" x="546" y="56" width="16" height="14"/>
<rect class="vx-box" x="564" y="56" width="16" height="14"/>
<rect class="vx-box" x="582" y="56" width="16" height="14"/>
<rect class="vx-box" x="600" y="56" width="16" height="14"/>
<rect class="vx-box" x="618" y="56" width="16" height="14"/>
<rect class="vx-box" x="636" y="56" width="16" height="14"/>
<rect class="vx-box" x="654" y="56" width="16" height="14"/>
<rect class="vx-box" x="672" y="56" width="16" height="14"/>
<rect class="vx-box" x="690" y="56" width="16" height="14"/>
<rect class="vx-box" x="708" y="56" width="16" height="14"/>
<text class="vx-text-muted" x="20" y="88">issued</text>
<rect class="vx-box-strong" x="155" y="79" width="6" height="8"/>
<rect class="vx-box-strong" x="173" y="79" width="6" height="8"/>
<rect class="vx-box-strong" x="425" y="79" width="6" height="8"/>
<rect class="vx-box-strong" x="443" y="79" width="6" height="8"/>
<rect class="vx-box-strong" x="461" y="79" width="6" height="8"/>
<rect class="vx-box-strong" x="479" y="79" width="6" height="8"/>
<text class="vx-text-accent" x="734" y="88">6 of 32</text>
<text class="vx-text" x="20" y="120">8 warps: the scheduler finds work far more often</text>
<text class="vx-text-muted" x="20" y="140">warp 0</text>
<rect class="vx-cell-on" x="150" y="128" width="16" height="14"/>
<rect class="vx-box" x="168" y="128" width="16" height="14"/>
<rect class="vx-box" x="186" y="128" width="16" height="14"/>
<rect class="vx-box" x="204" y="128" width="16" height="14"/>
<rect class="vx-box" x="222" y="128" width="16" height="14"/>
<rect class="vx-box" x="240" y="128" width="16" height="14"/>
<rect class="vx-box" x="258" y="128" width="16" height="14"/>
<rect class="vx-box" x="276" y="128" width="16" height="14"/>
<rect class="vx-box" x="294" y="128" width="16" height="14"/>
<rect class="vx-box" x="312" y="128" width="16" height="14"/>
<rect class="vx-box" x="330" y="128" width="16" height="14"/>
<rect class="vx-box" x="348" y="128" width="16" height="14"/>
<rect class="vx-box" x="366" y="128" width="16" height="14"/>
<rect class="vx-box" x="384" y="128" width="16" height="14"/>
<rect class="vx-box" x="402" y="128" width="16" height="14"/>
<rect class="vx-cell-on" x="420" y="128" width="16" height="14"/>
<rect class="vx-box-accent" x="438" y="128" width="16" height="14"/>
<rect class="vx-box-accent" x="456" y="128" width="16" height="14"/>
<rect class="vx-box-accent" x="474" y="128" width="16" height="14"/>
<rect class="vx-box-accent" x="492" y="128" width="16" height="14"/>
<rect class="vx-box-accent" x="510" y="128" width="16" height="14"/>
<rect class="vx-box-accent" x="528" y="128" width="16" height="14"/>
<rect class="vx-box-accent" x="546" y="128" width="16" height="14"/>
<rect class="vx-cell-on" x="564" y="128" width="16" height="14"/>
<rect class="vx-box" x="582" y="128" width="16" height="14"/>
<rect class="vx-box" x="600" y="128" width="16" height="14"/>
<rect class="vx-box" x="618" y="128" width="16" height="14"/>
<rect class="vx-box" x="636" y="128" width="16" height="14"/>
<rect class="vx-box" x="654" y="128" width="16" height="14"/>
<rect class="vx-box" x="672" y="128" width="16" height="14"/>
<rect class="vx-box" x="690" y="128" width="16" height="14"/>
<rect class="vx-box" x="708" y="128" width="16" height="14"/>
<text class="vx-text-muted" x="20" y="156">warp 1</text>
<rect class="vx-box-accent" x="150" y="144" width="16" height="14"/>
<rect class="vx-cell-on" x="168" y="144" width="16" height="14"/>
<rect class="vx-box" x="186" y="144" width="16" height="14"/>
<rect class="vx-box" x="204" y="144" width="16" height="14"/>
<rect class="vx-box" x="222" y="144" width="16" height="14"/>
<rect class="vx-box" x="240" y="144" width="16" height="14"/>
<rect class="vx-box" x="258" y="144" width="16" height="14"/>
<rect class="vx-box" x="276" y="144" width="16" height="14"/>
<rect class="vx-box" x="294" y="144" width="16" height="14"/>
<rect class="vx-box" x="312" y="144" width="16" height="14"/>
<rect class="vx-box" x="330" y="144" width="16" height="14"/>
<rect class="vx-box" x="348" y="144" width="16" height="14"/>
<rect class="vx-box" x="366" y="144" width="16" height="14"/>
<rect class="vx-box" x="384" y="144" width="16" height="14"/>
<rect class="vx-box" x="402" y="144" width="16" height="14"/>
<rect class="vx-box" x="420" y="144" width="16" height="14"/>
<rect class="vx-cell-on" x="438" y="144" width="16" height="14"/>
<rect class="vx-box-accent" x="456" y="144" width="16" height="14"/>
<rect class="vx-box-accent" x="474" y="144" width="16" height="14"/>
<rect class="vx-box-accent" x="492" y="144" width="16" height="14"/>
<rect class="vx-box-accent" x="510" y="144" width="16" height="14"/>
<rect class="vx-box-accent" x="528" y="144" width="16" height="14"/>
<rect class="vx-box-accent" x="546" y="144" width="16" height="14"/>
<rect class="vx-box-accent" x="564" y="144" width="16" height="14"/>
<rect class="vx-cell-on" x="582" y="144" width="16" height="14"/>
<rect class="vx-box" x="600" y="144" width="16" height="14"/>
<rect class="vx-box" x="618" y="144" width="16" height="14"/>
<rect class="vx-box" x="636" y="144" width="16" height="14"/>
<rect class="vx-box" x="654" y="144" width="16" height="14"/>
<rect class="vx-box" x="672" y="144" width="16" height="14"/>
<rect class="vx-box" x="690" y="144" width="16" height="14"/>
<rect class="vx-box" x="708" y="144" width="16" height="14"/>
<text class="vx-text-muted" x="20" y="172">warp 2</text>
<rect class="vx-box-accent" x="150" y="160" width="16" height="14"/>
<rect class="vx-box-accent" x="168" y="160" width="16" height="14"/>
<rect class="vx-cell-on" x="186" y="160" width="16" height="14"/>
<rect class="vx-box" x="204" y="160" width="16" height="14"/>
<rect class="vx-box" x="222" y="160" width="16" height="14"/>
<rect class="vx-box" x="240" y="160" width="16" height="14"/>
<rect class="vx-box" x="258" y="160" width="16" height="14"/>
<rect class="vx-box" x="276" y="160" width="16" height="14"/>
<rect class="vx-box" x="294" y="160" width="16" height="14"/>
<rect class="vx-box" x="312" y="160" width="16" height="14"/>
<rect class="vx-box" x="330" y="160" width="16" height="14"/>
<rect class="vx-box" x="348" y="160" width="16" height="14"/>
<rect class="vx-box" x="366" y="160" width="16" height="14"/>
<rect class="vx-box" x="384" y="160" width="16" height="14"/>
<rect class="vx-box" x="402" y="160" width="16" height="14"/>
<rect class="vx-box" x="420" y="160" width="16" height="14"/>
<rect class="vx-box" x="438" y="160" width="16" height="14"/>
<rect class="vx-cell-on" x="456" y="160" width="16" height="14"/>
<rect class="vx-box-accent" x="474" y="160" width="16" height="14"/>
<rect class="vx-box-accent" x="492" y="160" width="16" height="14"/>
<rect class="vx-box-accent" x="510" y="160" width="16" height="14"/>
<rect class="vx-box-accent" x="528" y="160" width="16" height="14"/>
<rect class="vx-box-accent" x="546" y="160" width="16" height="14"/>
<rect class="vx-box-accent" x="564" y="160" width="16" height="14"/>
<rect class="vx-box-accent" x="582" y="160" width="16" height="14"/>
<rect class="vx-cell-on" x="600" y="160" width="16" height="14"/>
<rect class="vx-box" x="618" y="160" width="16" height="14"/>
<rect class="vx-box" x="636" y="160" width="16" height="14"/>
<rect class="vx-box" x="654" y="160" width="16" height="14"/>
<rect class="vx-box" x="672" y="160" width="16" height="14"/>
<rect class="vx-box" x="690" y="160" width="16" height="14"/>
<rect class="vx-box" x="708" y="160" width="16" height="14"/>
<text class="vx-text-muted" x="20" y="188">warp 3</text>
<rect class="vx-box-accent" x="150" y="176" width="16" height="14"/>
<rect class="vx-box-accent" x="168" y="176" width="16" height="14"/>
<rect class="vx-box-accent" x="186" y="176" width="16" height="14"/>
<rect class="vx-cell-on" x="204" y="176" width="16" height="14"/>
<rect class="vx-box" x="222" y="176" width="16" height="14"/>
<rect class="vx-box" x="240" y="176" width="16" height="14"/>
<rect class="vx-box" x="258" y="176" width="16" height="14"/>
<rect class="vx-box" x="276" y="176" width="16" height="14"/>
<rect class="vx-box" x="294" y="176" width="16" height="14"/>
<rect class="vx-box" x="312" y="176" width="16" height="14"/>
<rect class="vx-box" x="330" y="176" width="16" height="14"/>
<rect class="vx-box" x="348" y="176" width="16" height="14"/>
<rect class="vx-box" x="366" y="176" width="16" height="14"/>
<rect class="vx-box" x="384" y="176" width="16" height="14"/>
<rect class="vx-box" x="402" y="176" width="16" height="14"/>
<rect class="vx-box" x="420" y="176" width="16" height="14"/>
<rect class="vx-box" x="438" y="176" width="16" height="14"/>
<rect class="vx-box" x="456" y="176" width="16" height="14"/>
<rect class="vx-cell-on" x="474" y="176" width="16" height="14"/>
<rect class="vx-box-accent" x="492" y="176" width="16" height="14"/>
<rect class="vx-box-accent" x="510" y="176" width="16" height="14"/>
<rect class="vx-box-accent" x="528" y="176" width="16" height="14"/>
<rect class="vx-box-accent" x="546" y="176" width="16" height="14"/>
<rect class="vx-box-accent" x="564" y="176" width="16" height="14"/>
<rect class="vx-box-accent" x="582" y="176" width="16" height="14"/>
<rect class="vx-box-accent" x="600" y="176" width="16" height="14"/>
<rect class="vx-cell-on" x="618" y="176" width="16" height="14"/>
<rect class="vx-box" x="636" y="176" width="16" height="14"/>
<rect class="vx-box" x="654" y="176" width="16" height="14"/>
<rect class="vx-box" x="672" y="176" width="16" height="14"/>
<rect class="vx-box" x="690" y="176" width="16" height="14"/>
<rect class="vx-box" x="708" y="176" width="16" height="14"/>
<text class="vx-text-muted" x="20" y="204">warp 4</text>
<rect class="vx-box-accent" x="150" y="192" width="16" height="14"/>
<rect class="vx-box-accent" x="168" y="192" width="16" height="14"/>
<rect class="vx-box-accent" x="186" y="192" width="16" height="14"/>
<rect class="vx-box-accent" x="204" y="192" width="16" height="14"/>
<rect class="vx-cell-on" x="222" y="192" width="16" height="14"/>
<rect class="vx-box" x="240" y="192" width="16" height="14"/>
<rect class="vx-box" x="258" y="192" width="16" height="14"/>
<rect class="vx-box" x="276" y="192" width="16" height="14"/>
<rect class="vx-box" x="294" y="192" width="16" height="14"/>
<rect class="vx-box" x="312" y="192" width="16" height="14"/>
<rect class="vx-box" x="330" y="192" width="16" height="14"/>
<rect class="vx-box" x="348" y="192" width="16" height="14"/>
<rect class="vx-box" x="366" y="192" width="16" height="14"/>
<rect class="vx-box" x="384" y="192" width="16" height="14"/>
<rect class="vx-box" x="402" y="192" width="16" height="14"/>
<rect class="vx-box" x="420" y="192" width="16" height="14"/>
<rect class="vx-box" x="438" y="192" width="16" height="14"/>
<rect class="vx-box" x="456" y="192" width="16" height="14"/>
<rect class="vx-box" x="474" y="192" width="16" height="14"/>
<rect class="vx-cell-on" x="492" y="192" width="16" height="14"/>
<rect class="vx-box-accent" x="510" y="192" width="16" height="14"/>
<rect class="vx-box-accent" x="528" y="192" width="16" height="14"/>
<rect class="vx-box-accent" x="546" y="192" width="16" height="14"/>
<rect class="vx-box-accent" x="564" y="192" width="16" height="14"/>
<rect class="vx-box-accent" x="582" y="192" width="16" height="14"/>
<rect class="vx-box-accent" x="600" y="192" width="16" height="14"/>
<rect class="vx-box-accent" x="618" y="192" width="16" height="14"/>
<rect class="vx-cell-on" x="636" y="192" width="16" height="14"/>
<rect class="vx-box" x="654" y="192" width="16" height="14"/>
<rect class="vx-box" x="672" y="192" width="16" height="14"/>
<rect class="vx-box" x="690" y="192" width="16" height="14"/>
<rect class="vx-box" x="708" y="192" width="16" height="14"/>
<text class="vx-text-muted" x="20" y="220">warp 5</text>
<rect class="vx-box-accent" x="150" y="208" width="16" height="14"/>
<rect class="vx-box-accent" x="168" y="208" width="16" height="14"/>
<rect class="vx-box-accent" x="186" y="208" width="16" height="14"/>
<rect class="vx-box-accent" x="204" y="208" width="16" height="14"/>
<rect class="vx-box-accent" x="222" y="208" width="16" height="14"/>
<rect class="vx-cell-on" x="240" y="208" width="16" height="14"/>
<rect class="vx-box" x="258" y="208" width="16" height="14"/>
<rect class="vx-box" x="276" y="208" width="16" height="14"/>
<rect class="vx-box" x="294" y="208" width="16" height="14"/>
<rect class="vx-box" x="312" y="208" width="16" height="14"/>
<rect class="vx-box" x="330" y="208" width="16" height="14"/>
<rect class="vx-box" x="348" y="208" width="16" height="14"/>
<rect class="vx-box" x="366" y="208" width="16" height="14"/>
<rect class="vx-box" x="384" y="208" width="16" height="14"/>
<rect class="vx-box" x="402" y="208" width="16" height="14"/>
<rect class="vx-box" x="420" y="208" width="16" height="14"/>
<rect class="vx-box" x="438" y="208" width="16" height="14"/>
<rect class="vx-box" x="456" y="208" width="16" height="14"/>
<rect class="vx-box" x="474" y="208" width="16" height="14"/>
<rect class="vx-box" x="492" y="208" width="16" height="14"/>
<rect class="vx-cell-on" x="510" y="208" width="16" height="14"/>
<rect class="vx-box-accent" x="528" y="208" width="16" height="14"/>
<rect class="vx-box-accent" x="546" y="208" width="16" height="14"/>
<rect class="vx-box-accent" x="564" y="208" width="16" height="14"/>
<rect class="vx-box-accent" x="582" y="208" width="16" height="14"/>
<rect class="vx-box-accent" x="600" y="208" width="16" height="14"/>
<rect class="vx-box-accent" x="618" y="208" width="16" height="14"/>
<rect class="vx-box-accent" x="636" y="208" width="16" height="14"/>
<rect class="vx-cell-on" x="654" y="208" width="16" height="14"/>
<rect class="vx-box" x="672" y="208" width="16" height="14"/>
<rect class="vx-box" x="690" y="208" width="16" height="14"/>
<rect class="vx-box" x="708" y="208" width="16" height="14"/>
<text class="vx-text-muted" x="20" y="236">warp 6</text>
<rect class="vx-box-accent" x="150" y="224" width="16" height="14"/>
<rect class="vx-box-accent" x="168" y="224" width="16" height="14"/>
<rect class="vx-box-accent" x="186" y="224" width="16" height="14"/>
<rect class="vx-box-accent" x="204" y="224" width="16" height="14"/>
<rect class="vx-box-accent" x="222" y="224" width="16" height="14"/>
<rect class="vx-box-accent" x="240" y="224" width="16" height="14"/>
<rect class="vx-cell-on" x="258" y="224" width="16" height="14"/>
<rect class="vx-box" x="276" y="224" width="16" height="14"/>
<rect class="vx-box" x="294" y="224" width="16" height="14"/>
<rect class="vx-box" x="312" y="224" width="16" height="14"/>
<rect class="vx-box" x="330" y="224" width="16" height="14"/>
<rect class="vx-box" x="348" y="224" width="16" height="14"/>
<rect class="vx-box" x="366" y="224" width="16" height="14"/>
<rect class="vx-box" x="384" y="224" width="16" height="14"/>
<rect class="vx-box" x="402" y="224" width="16" height="14"/>
<rect class="vx-box" x="420" y="224" width="16" height="14"/>
<rect class="vx-box" x="438" y="224" width="16" height="14"/>
<rect class="vx-box" x="456" y="224" width="16" height="14"/>
<rect class="vx-box" x="474" y="224" width="16" height="14"/>
<rect class="vx-box" x="492" y="224" width="16" height="14"/>
<rect class="vx-box" x="510" y="224" width="16" height="14"/>
<rect class="vx-cell-on" x="528" y="224" width="16" height="14"/>
<rect class="vx-box-accent" x="546" y="224" width="16" height="14"/>
<rect class="vx-box-accent" x="564" y="224" width="16" height="14"/>
<rect class="vx-box-accent" x="582" y="224" width="16" height="14"/>
<rect class="vx-box-accent" x="600" y="224" width="16" height="14"/>
<rect class="vx-box-accent" x="618" y="224" width="16" height="14"/>
<rect class="vx-box-accent" x="636" y="224" width="16" height="14"/>
<rect class="vx-box-accent" x="654" y="224" width="16" height="14"/>
<rect class="vx-cell-on" x="672" y="224" width="16" height="14"/>
<rect class="vx-box" x="690" y="224" width="16" height="14"/>
<rect class="vx-box" x="708" y="224" width="16" height="14"/>
<text class="vx-text-muted" x="20" y="252">warp 7</text>
<rect class="vx-box-accent" x="150" y="240" width="16" height="14"/>
<rect class="vx-box-accent" x="168" y="240" width="16" height="14"/>
<rect class="vx-box-accent" x="186" y="240" width="16" height="14"/>
<rect class="vx-box-accent" x="204" y="240" width="16" height="14"/>
<rect class="vx-box-accent" x="222" y="240" width="16" height="14"/>
<rect class="vx-box-accent" x="240" y="240" width="16" height="14"/>
<rect class="vx-box-accent" x="258" y="240" width="16" height="14"/>
<rect class="vx-cell-on" x="276" y="240" width="16" height="14"/>
<rect class="vx-box" x="294" y="240" width="16" height="14"/>
<rect class="vx-box" x="312" y="240" width="16" height="14"/>
<rect class="vx-box" x="330" y="240" width="16" height="14"/>
<rect class="vx-box" x="348" y="240" width="16" height="14"/>
<rect class="vx-box" x="366" y="240" width="16" height="14"/>
<rect class="vx-box" x="384" y="240" width="16" height="14"/>
<rect class="vx-box" x="402" y="240" width="16" height="14"/>
<rect class="vx-box" x="420" y="240" width="16" height="14"/>
<rect class="vx-box" x="438" y="240" width="16" height="14"/>
<rect class="vx-box" x="456" y="240" width="16" height="14"/>
<rect class="vx-box" x="474" y="240" width="16" height="14"/>
<rect class="vx-box" x="492" y="240" width="16" height="14"/>
<rect class="vx-box" x="510" y="240" width="16" height="14"/>
<rect class="vx-box" x="528" y="240" width="16" height="14"/>
<rect class="vx-cell-on" x="546" y="240" width="16" height="14"/>
<rect class="vx-box-accent" x="564" y="240" width="16" height="14"/>
<rect class="vx-box-accent" x="582" y="240" width="16" height="14"/>
<rect class="vx-box-accent" x="600" y="240" width="16" height="14"/>
<rect class="vx-box-accent" x="618" y="240" width="16" height="14"/>
<rect class="vx-box-accent" x="636" y="240" width="16" height="14"/>
<rect class="vx-box-accent" x="654" y="240" width="16" height="14"/>
<rect class="vx-box-accent" x="672" y="240" width="16" height="14"/>
<rect class="vx-cell-on" x="690" y="240" width="16" height="14"/>
<rect class="vx-box" x="708" y="240" width="16" height="14"/>
<text class="vx-text-muted" x="20" y="272">issued</text>
<rect class="vx-box-strong" x="155" y="263" width="6" height="8"/>
<rect class="vx-box-strong" x="173" y="263" width="6" height="8"/>
<rect class="vx-box-strong" x="191" y="263" width="6" height="8"/>
<rect class="vx-box-strong" x="209" y="263" width="6" height="8"/>
<rect class="vx-box-strong" x="227" y="263" width="6" height="8"/>
<rect class="vx-box-strong" x="245" y="263" width="6" height="8"/>
<rect class="vx-box-strong" x="263" y="263" width="6" height="8"/>
<rect class="vx-box-strong" x="281" y="263" width="6" height="8"/>
<rect class="vx-box-strong" x="425" y="263" width="6" height="8"/>
<rect class="vx-box-strong" x="443" y="263" width="6" height="8"/>
<rect class="vx-box-strong" x="461" y="263" width="6" height="8"/>
<rect class="vx-box-strong" x="479" y="263" width="6" height="8"/>
<rect class="vx-box-strong" x="497" y="263" width="6" height="8"/>
<rect class="vx-box-strong" x="515" y="263" width="6" height="8"/>
<rect class="vx-box-strong" x="533" y="263" width="6" height="8"/>
<rect class="vx-box-strong" x="551" y="263" width="6" height="8"/>
<rect class="vx-box-strong" x="569" y="263" width="6" height="8"/>
<rect class="vx-box-strong" x="587" y="263" width="6" height="8"/>
<rect class="vx-box-strong" x="605" y="263" width="6" height="8"/>
<rect class="vx-box-strong" x="623" y="263" width="6" height="8"/>
<rect class="vx-box-strong" x="641" y="263" width="6" height="8"/>
<rect class="vx-box-strong" x="659" y="263" width="6" height="8"/>
<rect class="vx-box-strong" x="677" y="263" width="6" height="8"/>
<rect class="vx-box-strong" x="695" y="263" width="6" height="8"/>
<text class="vx-text-accent" x="734" y="272">24 of 32</text>
<rect class="vx-cell-on" x="150" y="302" width="16" height="14"/>
<text class="vx-text-muted" x="172" y="314">selected</text>
<rect class="vx-box-accent" x="260" y="302" width="16" height="14"/>
<text class="vx-text-muted" x="282" y="314">ready, not selected</text>
<rect class="vx-box" x="440" y="302" width="16" height="14"/>
<text class="vx-text-muted" x="462" y="314">long scoreboard: waiting for a load</text>
</svg>
<figcaption>Figure 3. The first 32 cycles of <code>warp_states.cpp</code>, drawn from the same model. The waits are as long with 8 warps as with 2; the difference is that the scheduler nearly always has another warp to issue. Stall reasons explain a kernel only when the issued row has gaps.</figcaption>
</figure>

Boehm's worklog shows the method on a real kernel. His third kernel, which stages tiles of both matrices in shared memory, was still far from the peak. He first worked out its theoretical occupancy by hand, 32 of 48 possible warps per SM, about 66%, limited by threads and registers per block, and judged that it did not explain the slowness.[^boehm] The warp-state chart then pointed at MIO throttle, and its "not selected" samples suggested that there were warps enough. The instruction mix was mostly loads from shared memory, so he concluded that the kernel waited on its shared-memory accesses, and made each thread compute several outputs from values held in registers, the next rung of [G10](g10-matmul-ladder.md)'s ladder.[^boehm]

??? check "A kernel's Scheduler Statistics show an eligible warp in 97% of cycles. Its warp-state chart is dominated by long scoreboard. Should you work on the loads?"

    Not because of that chart. The scheduler issues on 97% of cycles, so the waits are being hidden by other warps and removing them could gain at most a few percent. Go back to the speed-of-light report: a kernel that issues nearly every cycle is limited by what it issues, a busy pipeline or too many instructions, not by waiting.

## What the profiler does to your kernel

A profiler's numbers describe a run the profiler arranged, and that run differs from your program's. Knowing how matters when the two disagree.

The GPU can count only a limited number of hardware events at once, so Nsight Compute may **replay** a kernel several times, collecting a different group of counters in each **pass**. Before the first pass it saves all the device memory the kernel can reach, and before each later pass it restores what the kernel wrote, so every pass sees the same inputs.[^ncu-replay] It cannot save the caches, so by default it flushes all GPU caches before every pass, making each pass behave like the kernel run in isolation, from a cold cache.[^ncu-cache] It also serializes kernel launches and, by default, holds the GPU clocks at a fixed frequency.[^ncu-repro][^ncu-clock]

--8<-- "includes/examples/gpu/g14-measuring-gpu-code/replay_passes.cpp.md"

The example collects three metrics in three passes from a kernel that updates an array in place. Without restoring memory, the third pass sees an array updated twice already, and its checksum is wrong. Without flushing, the second pass finds the whole array in the cache and reports no misses. Only with both resets do the three numbers describe one run.

The resets have a cost for interpretation. A kernel whose data a previous kernel left in the L2 cache runs from a warm cache in your program and from a cold one under the default settings, so cache-sensitive metrics and durations differ. The guide suggests `--cache-control none` with application replay when the cache state is part of what you want to measure.[^ncu-cache] It also warns that host timers and CUDA events cannot time a kernel while Nsight Compute profiles it, since they would include the tool's own work, and that NVIDIA's other profiler, Nsight Systems, measures durations differently.[^ncu-durations]

Its advice for comparing two reports is P1's, applied to counters: keep the capture settings identical unless the setting is the variable under test, use an optimized build, restore a representative workload before drawing conclusions, and treat the absolute duration as the ground truth, because a utilization percentage can move either way when the total work changes.[^ncu-repro] Short kernels need care: a ratio such as a hit rate whose two counts came from different passes can be far off when the kernel runs too briefly to reach a steady state, which the guide puts at generally over 20 µs.[^ncu-range]

??? check "A change removes half of a kernel's instructions. Its compute throughput falls from 80% to 55% and its duration falls by a third. Did the change make things worse?"

    No. The duration is the ground truth, and it fell. The throughput percentage fell because the kernel now does less work for the pipeline to be busy with, which is the point of removing instructions. A report that showed only the percentage would suggest the opposite of what happened.

## Three vendors, one method

AMD's profiler for its ROCm software is **rocprofv3**, built on the ROCprofiler-SDK. It does two jobs: application tracing, a timeline of runtime calls and GPU activity such as kernel dispatches and memory copies, and kernel counter collection from the hardware performance counters.[^rocprof-sdk] `rocprofv3 --kernel-trace` records each kernel's execution, and `--pmc` names the counters to collect; `rocprofv3 --list-avail` lists what the GPU supports, since the counters vary between GPUs.[^rocprofv3]

Two details change how you use it. When counters cannot be collected together, each `--pmc` group is a separate pass, and each pass runs the whole application from start to finish rather than replaying one kernel.[^rocprofv3] And on RDNA3 and RDNA4 GPUs the default power mode switches off the clock of the counter hardware in some blocks, so the documentation asks you to set the performance level to `STABLE_STD` before collecting counters.[^rocprofv3] The first is a reason to make the application deterministic; the second, a reason to record the power setting with every result.

On Apple GPUs, Xcode's Metal debugger captures GPU work and shows its counters in a Performance Statistics view, for one command or one pass, with the median, maximum and total across the passes of the capture.[^xcode-counters] For a whole run, Instruments records a timeline that includes its Metal system trace instrument; choosing the Performance Limiters counter set in the recording options adds performance **limiter** and **utilization** counters.[^xcode-limiters] A WWDC22 session on scaling compute work across Apple GPUs uses them as a first diagnosis: a poor memory access pattern shows high limiters for the last-level cache or the memory-management unit (MMU) with low utilization.[^wwdc22] The same session introduces a theoretical occupancy figure in Xcode 14's compiler statistics.[^wwdc22]

The names differ; the method does not. Each tool answers, in order, how long the work took, which unit came closest to its limit, whether the scheduler had work to issue, and where in the code the waiting happened. Only the counters and their peaks belong to the chip.

On the owner's M4 Pro, NVIDIA's tools still help. CUDA 10.2 was the last toolkit to support macOS,[^cuda-mac] but Nsight Compute's user interface has run natively on macOS arm64 since version 2025.1.[^ncu-macos] So a report collected with the `ncu` command line on a rented Linux machine with an NVIDIA GPU can be opened and read on the Mac, or exported as comma-separated text with `ncu --import report.ncu-rep --csv --page raw`.[^ncu-cli] Record the rented machine's GPU, driver and tool versions with the report: next month's rental may be a different machine.

## Two facts per claim

Go back to Boehm's two kernels and write down what was learned. "Kernel 2 is 6.4 times faster" is a ratio, and [P1](../optimize/p1-measure-first.md#the-reporting-rules) already warned that a ratio alone throws away the numbers it came from. "We coalesced the loads" names a cause, but not whether it mattered. A useful claim joins the two: a timing, with its setup, and the one counter the change was meant to move, before and after.

For the coalescing change, the counter is L1 sectors per request, and the prediction is G4's: from 33 sectors per step of the inner loop to 5. If the counter moved and the time fell, the explanation holds up. If the time fell and the counter did not move, the explanation is wrong, whatever the story. If the counter moved and the time did not fall, the kernel was limited somewhere else, and the speed-of-light report says where. A third fact adds nothing unless it rules out a rival explanation; the derived DRAM traffic above did exactly that.

NVIDIA's Best Practices Guide frames the whole activity as a cycle, **APOD**: assess, parallelize, optimize, deploy, repeated, each round starting by finding where the time goes.[^bp-apod] The two-fact claim is how one round ends.

The same discipline separates the two kinds of number this book has produced. G4's sector counts, G5's theoretical occupancy and P3's roofline bound are **estimates**: computed from the program and a model of the machine, before or without running anything. The profiler's sectors per request, achieved occupancy and duration are **measurements**. Vortex's philosophy makes the distinction a rule for the compiler: it must never present "an unverified performance estimate as a measured result".[^philosophy] A remark that says "coalesced: 4 sectors per request" is an estimate until a profiler has read the counter, and it should say so.

## Measuring it

No GPU timings are claimed in this chapter. Collect your own on the M4 Pro:

1. Write the stage 10 kernel for 512 × 512 `f32` matrices in Metal Shading Language twice, once with neighbouring threads on neighbouring rows of the output and once on neighbouring columns, and compile the source at run time from a small host program; on the owner's machine this works without the offline Metal toolchain (checked on 2026-09-23).
2. Compile with `mathMode` set to `safe` and check both outputs bit for bit against a CPU reference that adds in the same order, before timing anything.
3. Time 30 command buffers of each, one dispatch per buffer, with `gpuEndTime − gpuStartTime`, discard the first 5, and report the median with its interval (P1).
4. Capture one dispatch of each in Xcode and record the limiter and utilization counters that differ most between the two.
5. Record the machine, macOS and Xcode versions, the math mode and the date.

| Kernel | Launches kept | Median time | 95% interval | GFLOP/s at the median | Counter that moved most (before, after) |
| --- | --- | --- | --- | --- | --- |
| rows on lanes | | | | | |
| columns on lanes | | | | | |

## For Vortex

!!! vortex "Exercise"

    **Build** a GPU mode for `vortex-bench`, the harness from [P1](../optimize/p1-measure-first.md#for-vortex) that [P4](../optimize/p4-counters-and-tools.md#for-vortex) extended with counters. It stays a tool outside the compiler, in any language you like. Vortex does not generate GPU code yet, so the kernels are hand-written MSL for the stage 10 program's computation.

    1. **A GPU correctness gate.** Run each kernel and compare its output with the CPU result of the stage 10 program: bit for bit when the kernel is compiled in `safe` math mode and adds in the same order, otherwise with a tolerance the report states. Refuse to report any timing when the gate fails.
    2. **GPU-side timing.** Time command buffers with the GPU's start and end times, never a host clock around an asynchronous commit. Record the number of warm-up launches discarded, the dispatches per command buffer, and P1's statistics over the rest.
    3. **Rates.** Report GFLOP/s from the work formula and effective bandwidth from the minimum bytes read and written, each labelled with how it was computed.
    4. **Estimates beside measurements.** For each kernel, the sectors per warp request that G4's model predicts for each access, and the roofline bound from P3 for a target description you can cite. Put them in columns marked "estimate", never in the columns for measured values, and record the source of every peak.
    5. **Imported counters.** Accept a counter file exported from a profiler (for example `ncu --csv --page raw`, or values you copy from Xcode's counters with the metric names as shown), store the names and values unchanged with the tool and its version, and mark them "measured".
    6. **A two-fact claim.** Given two kernels and one counter name, print a line with the ratio of median times and its interval, and the counter before and after, stating for each number whether it was measured or estimated.

    **Not yet:** generating GPU code from Vortex ([M12](../mlir/m12-vortex-gpu-path.md) weighs the paths); running GPU tests in CI, since GPU access on hosted macOS runners is not confirmed; driving a profiler programmatically; any compiler decision made from a measured counter.

    **Proof that it works:**

    - **The asynchronous trap.** Add a deliberately wrong mode that reads a host clock around the commit without waiting for completion, and show that it reports times that do not grow with the matrix size. The correct mode's times must grow.
    - **A planted slowdown.** A kernel that computes the product twice, folding both into the output, must show a ratio interval that excludes 1 and sits near 2 against the original.
    - **A broken kernel.** Change one loop bound in the MSL; the gate must fail and no time be reported.
    - **The label test.** Feed the two-fact printer an estimate in place of a measured counter; the output must say "estimate". Give it two counters from different tools or versions; it must refuse to compare them.
    - **Fill in the table** from "Measuring it", with the machine and the date.

## Key ideas

!!! recap "Questions you can now answer"

    - **Why does a host clock around a kernel launch time the wrong thing?** Launches are asynchronous; time with GPU events or command-buffer timestamps, or synchronize before both clock reads.
    - **What does "memory 70%" in a speed-of-light report mean?** Some memory unit, the busiest one, reached 70% of its own peak; the breakdown says which, and it need not be DRAM.
    - **What do you do when both speed-of-light percentages are low?** Check whether the scheduler skips issue slots; only then read stall reasons, starting from the instruction each stall is charged to.
    - **Why can a kernel with a large stall count be fine?** Other warps may hide the stalls; the target is issue-slot utilization, not zero stalls.
    - **Why do a profiler's numbers differ from your program's?** It replays kernels with memory restored, flushes caches, fixes clocks and serializes launches.
    - **What does a claim about a speedup need?** A timing with its setup, and the one counter the change was meant to move, before and after, each labelled as measured or estimated.

## Where this comes back

!!! next "You will use this again in"

    - [G15. Beyond GPUs: systolic arrays and accelerators](g15-systolic-arrays.md): *roofline*, *speed of light*
    - [M11. End-to-end ML compilers](../mlir/m11-ml-compilers.md): *measured search*, *estimate versus measurement*
    - [M12. Designing Vortex's GPU path](../mlir/m12-vortex-gpu-path.md): *reporting performance decisions*, *estimate versus measurement*

## Sources and further reading

Read the Nsight Compute Profiling Guide's sections on sections and rules, replay and reproducibility first, then the warp stall reference. Boehm's worklog shows the method on one kernel from start to finish.

[^bp-timing]: NVIDIA, "CUDA C++ Best Practices Guide", 13.4, section 9.1, "Timing", with 9.1.1, "Using CPU Timers", and 9.1.2, "Using CUDA GPU Timers". <https://docs.nvidia.com/cuda/cuda-c-best-practices-guide/index.html#timing>
[^bp-bandwidth]: NVIDIA, "CUDA C++ Best Practices Guide", 13.4, section 9.2.2, "Effective Bandwidth Calculation". <https://docs.nvidia.com/cuda/cuda-c-best-practices-guide/index.html#effective-bandwidth-calculation>
[^bp-apod]: NVIDIA, "CUDA C++ Best Practices Guide", 13.4, section 2.2, "Assess, Parallelize, Optimize, Deploy". <https://docs.nvidia.com/cuda/cuda-c-best-practices-guide/index.html#assess-parallelize-optimize-deploy>
[^mtl-gputime]: Apple, Metal documentation, `MTLCommandBuffer` properties `gpuStartTime` and `gpuEndTime`. <https://developer.apple.com/documentation/metal/mtlcommandbuffer/gpustarttime> and <https://developer.apple.com/documentation/metal/mtlcommandbuffer/gpuendtime>
[^mtl-math]: Apple, Metal documentation, `MTLCompileOptions` properties `fastMathEnabled` and `mathMode`, and `MTLMathMode.safe`. <https://developer.apple.com/documentation/metal/mtlcompileoptions/fastmathenabled> and <https://developer.apple.com/documentation/metal/mtlcompileoptions/mathmode>
[^ncu-sets]: NVIDIA, "Nsight Compute: Profiling Guide", v2026.3.1, section 2.2.1, "Sets and Sections". <https://docs.nvidia.com/nsight-compute/ProfilingGuide/index.html#sets-and-sections>
[^ncu-sections]: NVIDIA, "Nsight Compute: Profiling Guide", v2026.3.1, section 2.2.2, "Sections and Rules": the descriptions of Speed Of Light, Memory Workload Analysis, Compute Workload Analysis, Occupancy, Scheduler Statistics, Warp State Statistics and Source Counters. <https://docs.nvidia.com/nsight-compute/ProfilingGuide/index.html#sections-and-rules>
[^ncu-metrics]: NVIDIA, "Nsight Compute: Profiling Guide", v2026.3.1, section 2.3.2, "Metrics Structure": "Metrics Overview", "Metrics Examples" and "Metrics Naming Conventions". <https://docs.nvidia.com/nsight-compute/ProfilingGuide/index.html#metrics-structure>
[^ncu-l1]: NVIDIA, "Nsight Compute: Profiling Guide", v2026.3.1, section 2.11.2, "L1/TEX Cache". <https://docs.nvidia.com/nsight-compute/ProfilingGuide/index.html#memory-tables-l1>
[^ncu-roofline]: NVIDIA, "Nsight Compute: Profiling Guide", v2026.3.1, section 2.9, "Roofline Charts", with 2.9.1, "Overview", and 2.9.2, "Analysis". <https://docs.nvidia.com/nsight-compute/ProfilingGuide/index.html#roofline-charts>
[^ncu-sampling]: NVIDIA, "Nsight Compute: Profiling Guide", v2026.3.1, section 2.5.2, "Warp Sampling". <https://docs.nvidia.com/nsight-compute/ProfilingGuide/index.html#warp-sampling>
[^ncu-stalls]: NVIDIA, "Nsight Compute: Profiling Guide", v2026.3.1, section 2.4.7, "Warp Stall Reasons": the interpretation guidance and the entries for barrier, long scoreboard, MIO throttle and not selected. <https://docs.nvidia.com/nsight-compute/ProfilingGuide/index.html#warp-stall-reasons>
[^ncu-replay]: NVIDIA, "Nsight Compute: Profiling Guide", v2026.3.1, section 2.2.3, "Replay", subsection "Kernel Replay". <https://docs.nvidia.com/nsight-compute/ProfilingGuide/index.html#kernel-replay>
[^ncu-repro]: NVIDIA, "Nsight Compute: Profiling Guide", v2026.3.1, section 2.6, "Reproducibility", with 2.6.1, "Serialization". <https://docs.nvidia.com/nsight-compute/ProfilingGuide/index.html#reproducibility>
[^ncu-clock]: NVIDIA, "Nsight Compute: Profiling Guide", v2026.3.1, section 2.6.2, "Clock Control". <https://docs.nvidia.com/nsight-compute/ProfilingGuide/index.html#clock-control>
[^ncu-cache]: NVIDIA, "Nsight Compute: Profiling Guide", v2026.3.1, section 2.6.3, "Cache Control". <https://docs.nvidia.com/nsight-compute/ProfilingGuide/index.html#cache-control>
[^ncu-range]: NVIDIA, "Nsight Compute: Profiling Guide", v2026.3.1, section 2.3.8, "Range and Precision", subsection "Multi-pass data collection". <https://docs.nvidia.com/nsight-compute/ProfilingGuide/index.html#range-and-precision>
[^ncu-durations]: NVIDIA, "Nsight Compute: Profiling Guide", v2026.3.1, section 2.3.8, "Range and Precision", subsection "Workload Durations". <https://docs.nvidia.com/nsight-compute/ProfilingGuide/index.html#workload-durations>
[^ncu-macos]: NVIDIA, "Nsight Compute: Release Notes", "Updates in 2025.1": native support for macOS arm64. <https://docs.nvidia.com/nsight-compute/ReleaseNotes/index.html>
[^cuda-mac]: NVIDIA, "CUDA Toolkit 10.2 Release Notes": 10.2 is the last release to support macOS. <https://docs.nvidia.com/cuda/archive/10.2/cuda-toolkit-release-notes/index.html>
[^ncu-cli]: NVIDIA, "Nsight Compute CLI" documentation: the `--import`, `--csv` and `--page` options. <https://docs.nvidia.com/nsight-compute/NsightComputeCli/index.html>
[^rocprof-sdk]: AMD, "ROCprofiler-SDK documentation", version 1.3.5, landing page. <https://rocm.docs.amd.com/projects/rocprofiler-sdk/en/latest/>
[^rocprofv3]: AMD, "Application tracing and profiling using rocprofv3", ROCprofiler-SDK 1.3.5: sections "Setting GPU performance level for PMC profiling", "Kernel trace", "Kernel counter collection" and "Multi-pass counter collection". <https://rocm.docs.amd.com/projects/rocprofiler-sdk/en/latest/how-to/using-rocprofv3.html>
[^xcode-counters]: Apple, "Analyzing draw command and compute dispatch performance with GPU counters", Xcode documentation. <https://developer.apple.com/documentation/xcode/analyzing-draw-command-and-compute-dispatch-performance-with-gpu-counters>
[^xcode-limiters]: Apple, "Analyzing the performance of your Metal app", Xcode documentation: recording with the Performance Limiters counter set. <https://developer.apple.com/documentation/xcode/analyzing-the-performance-of-your-metal-app>
[^wwdc22]: Apple, "Scale compute workloads across Apple GPUs", WWDC22 session 10159: GPU limiters in Xcode and Metal System Trace, and theoretical occupancy in Xcode 14. <https://developer.apple.com/videos/play/wwdc2022/10159/>
[^boehm]: Simon Boehm, "How to Optimize a CUDA Matmul Kernel for cuBLAS-like Performance: a Worklog", December 2022: the results table and the sections "Lower Bounding the Fastest Possible Runtime", "Memory Access Pattern of the Naive Kernel", "Kernel 2: Global Memory Coalescing", "Kernel 3: Shared Memory Cache-Blocking" and "Occupancy Calculation for Kernel 3". <https://siboehm.com/articles/22/CUDA-MMM>
[^philosophy]: Vortex, "Language philosophy", section "Programmer and compiler responsibilities". [Read it here](../philosophy.md#programmer-and-compiler-responsibilities).

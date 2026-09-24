# G14. Measuring GPU code

<p class="page-intro">A wall-clock number says a kernel got faster; it never says why. This chapter is about the report a profiler builds instead: two throughput percentages that say what the hardware was doing, a stall breakdown for when neither percentage is high, and the discipline of citing exactly the facts a claim needs, on NVIDIA, AMD and Apple GPUs alike.</p>

<p class="vx-meta" markdown="1">Level: Intermediate · Reading time: about 35 minutes · Builds on: [G4. Memory performance: coalescing and bank conflicts](g4-memory-performance.md), [P1. Measure first](../optimize/p1-measure-first.md)</p>

???+ remember "Before you start, remember"

    ??? question "Why is a single timed run not a measurement?"

        Because a running program shares the machine with a scheduler, a frequency governor and everything else on it. Two back-to-back runs of the same binary on the same input routinely differ; a report needs a distribution, not one reading.

        Introduced in [P1. Measure first](../optimize/p1-measure-first.md#a-stopwatch-is-not-a-measurement).

    ??? question "What should a performance claim report besides a speedup ratio?"

        The costs themselves, in their own units, alongside the ratio, and how many repetitions went into each number. A ratio alone hides whether either side was measured carefully.

        Introduced in [P1. Measure first](../optimize/p1-measure-first.md#what-to-report-instead-of-one-number).

    ??? question "What does one warp-wide load cost in global memory?"

        The number of distinct aligned 32-byte sectors that the warp's 32 addresses touch, not the number of bytes any one lane asked for. A stride of 1 element costs four sectors; a stride of 8 or more costs one sector per lane.

        Introduced in [G4. Memory performance: coalescing and bank conflicts](g4-memory-performance.md#thirty-two-addresses-one-instruction).

    ??? question "What must a Vortex compiler never do with a performance number?"

        Present an unverified estimate as a measured result. A cost model's guess and a profiler's reading are different things, and the compiler's own output must never blur them.

        Introduced in [Philosophy, Programmer and compiler responsibilities](../philosophy.md#programmer-and-compiler-responsibilities).

!!! goals "In this chapter"

    - Read a speed-of-light report as two percentages, and say what each one rules in or out.
    - Recognize when a stall-reason breakdown is the right next question, and when it is not.
    - Explain why Nsight Compute, rocprofv3 and Xcode's Metal tools are one methodology under three names, and where they diverge on the author's own Apple silicon.
    - Write a one-line remark that explains a measured speedup with exactly the facts it needs, no more.
    - Keep a measured counter and an estimated cost apart in every claim you write down, on a GPU exactly as on a CPU.

## A ratio without a reason

Say you take the naive matmul kernel and the coalesced kernel from [G4](g4-memory-performance.md#thirty-two-addresses-one-instruction), time each with [P1](../optimize/p1-measure-first.md)'s protocol, a median over repeated runs with a confidence interval, and get two trustworthy numbers. The second kernel is faster. P1 already earned you that sentence. But it is a strange kind of victory: you know a change helped, and you still cannot say, from the timings alone, *how* it helped. Was the second kernel issuing more instructions per cycle? Moving less data? Waiting less often for memory to answer? A wall-clock reading, however carefully taken, collapses everything the hardware did into one number.

The tool that answers "how" is a **profiler**: a program that runs your kernel and reads back **hardware performance counters**, on-chip registers that a GPU increments as it works, one per cycle, for things like instructions issued, bytes moved past a cache boundary, or cycles a warp spent waiting. A profiler differs from a stopwatch in what it is allowed to measure, not merely in how precisely it measures it: a stopwatch can only ever report elapsed time, however many times you run it, while a set of counters reports the shape of the work the hardware did during that time. NVIDIA's Nsight Compute, AMD's rocprofv3 and Apple's Xcode GPU tools are three implementations of the same idea, reading a different chip's counters through a different interface. The rest of this chapter is about how to read what they report, and how to say, honestly, what a number does and does not tell you.

## Speed of light first

Nsight Compute's first and most-used report is called **GPU Speed Of Light Throughput**: two headline percentages, one for compute and one for memory, each the achieved use of that pipeline's own peak.[^ncu-sol] "Speed of light" is a physics joke, not a marketing claim: the two numbers are ceilings the hardware itself enforces, and nothing you do to the kernel can push either one past 100%. Reading them together, before anything else, tells you which kind of question is worth asking next.

Take four kernels, described only by these two percentages:

--8<-- "includes/examples/gpu/g14-measuring-gpu-code/sol_classify.cpp.md"

The rule the program applies is simple once you see it as two independent axes rather than one score. A kernel high on both axes is close to what its hardware can do; nothing about it needs explaining beyond "well tuned." A kernel high on exactly one axis is bound by that resource: a transpose that never reaches shared memory can be nearly idle on compute while its uncoalesced loads and stores keep the memory pipeline busy, so [G4](g4-memory-performance.md)'s sector-counting rule is exactly what explains its memory percentage. A kernel low on both is the odd case, and the one most often misread: it did not spend its time computing or waiting on memory. Something else stopped it from issuing work at all.

<figure class="vx-figure">
<svg viewBox="0 0 760 460" role="img" aria-label="Where four example kernels land on the two speed-of-light axes" aria-describedby="g14-f1-desc">
<title id="g14-f1-title">Where four example kernels land on the two speed-of-light axes</title>
<desc id="g14-f1-desc">A plot with compute throughput on the horizontal axis and memory throughput on the vertical axis, both zero to one hundred percent of peak. Dashed lines cross each axis at sixty percent, splitting the plot into four regions: latency bound where both axes are low, memory bound where memory is high and compute is low, compute bound where compute is high and memory is low, and near the speed of light where both are high. Four example kernels are plotted: a barely launched kernel near the origin, a transpose without shared memory high on the memory axis only, unrolled elementwise math high on the compute axis only, and a tuned tiled kernel high on both axes.</desc>
<line class="vx-line" x1="100" y1="400" x2="700" y2="400"/>
<line class="vx-line" x1="100" y1="400" x2="100" y2="60"/>
<text class="vx-text-muted" x="100" y="422" text-anchor="middle">0%</text>
<text class="vx-text-muted" x="670" y="422" text-anchor="middle">100%</text>
<text class="vx-text" x="400" y="446" text-anchor="middle">compute throughput (% of peak)</text>
<text class="vx-text-muted" x="70" y="404" text-anchor="end">0%</text>
<text class="vx-text-muted" x="70" y="66" text-anchor="end">100%</text>
<text class="vx-text" x="30" y="230" text-anchor="middle" transform="rotate(-90 30 230)">memory throughput (% of peak)</text>
<line class="vx-line" x1="460" y1="60" x2="460" y2="400" stroke-dasharray="4 4"/>
<line class="vx-line" x1="100" y1="196" x2="700" y2="196" stroke-dasharray="4 4"/>
<text class="vx-text-muted" x="150" y="378" text-anchor="middle">latency bound</text>
<text class="vx-text-muted" x="170" y="80" text-anchor="middle">memory bound</text>
<text class="vx-text-muted" x="560" y="378" text-anchor="middle">compute bound</text>
<text class="vx-text-accent" x="580" y="80" text-anchor="middle">near the speed of light</text>
<circle class="vx-dot" cx="124" cy="390" r="6"/>
<text class="vx-text" x="138" y="376">barely-launched kernel</text>
<circle class="vx-dot" cx="172" cy="159" r="6"/>
<text class="vx-text" x="186" y="145">transpose without shared memory</text>
<circle class="vx-dot" cx="628" cy="349" r="6"/>
<text class="vx-text" x="486" y="335">unrolled elementwise math</text>
<circle class="vx-dot" cx="646" cy="114" r="6"/>
<text class="vx-text" x="510" y="100">tuned tiled kernel</text>
</svg>
<figcaption>Figure 1. The four example kernels from <code>sol_classify.cpp</code>, placed on the two speed-of-light axes. The dashed lines at 60% are the program's own teaching threshold, not a value the tools publish; the shape of the plot, two independent ceilings and four verdicts, is what carries over to any vendor's report.</figcaption>
</figure>

??? check "A kernel reports 8% compute throughput and 6% memory throughput. Is it compute-bound or memory-bound?"

    Neither. Both percentages are low, so neither pipeline was the bottleneck: the hardware spent most of its cycles not computing and not waiting on memory either, which points at a scheduling problem, not a throughput one. The next report to read is a warp state or occupancy report, not this one.

## When neither pipe is busy: occupancy and stalls

A GPU hides memory latency by keeping many warps in flight per core, so that when one warp stalls on a load, the scheduler has another warp ready to issue in the same cycle: a later chapter, [G5](g5-occupancy.md), is about how many warps that takes and what limits the count. This chapter only needs the consequence: when too few warps are resident, or every resident warp is stalled on the same thing at once, the scheduler runs out of ready work and issues nothing for a cycle. That is a stall, and it is the reason a kernel can sit low on both speed-of-light axes: it is not that compute and memory are both slow, it is that the pipeline feeding both of them is frequently empty.

A profiler's stall-reason report exists for exactly that case. It samples the scheduler over many cycles and tallies, for each cycle where nothing issued, why: waiting on a memory reply, waiting at a barrier, eligible but not picked this cycle, and so on. Read it the way [P1](../optimize/p1-measure-first.md) already taught you to read any performance number: as a distribution with named categories, not a single score. And read the tool's own caveat first, because the report matters only when it has something to explain: if the scheduler is issuing on nearly every cycle already, no stall reason is large enough to move the needle, and the report is empty of anything actionable.[^ncu-warpstate]

--8<-- "includes/examples/gpu/g14-measuring-gpu-code/stall_reasons.cpp.md"

The program checks its own issue rate before it prints anything about causes, which is the habit worth keeping: a stall breakdown you compute out of routine, without first asking whether stalls are the problem, is a report about nothing. In the printed example, one in four sampled cycles issued work; the other three in four were split across three reasons, memory replies by far the largest share. That is a kernel worth taking to [G4](g4-memory-performance.md)'s coalescing and bank-conflict tools next, because the stall reason says where the time went, not why the memory system was slow to answer; the sector and bank-conflict counts from that chapter are how you answer the second question.

??? check "A kernel's issue rate is 97%. Its owner wants to know which stall reason is largest. What should you tell them?"

    That the question does not apply here: at a 97% issue rate the scheduler is finding work almost every cycle, so no stall reason accounts for more than a few percent of cycles and none of them explains the kernel's performance. Whatever is limiting this kernel shows up on the speed-of-light axes instead, as low compute or low memory throughput while the pipeline stays full.

## The same report, three tool names

NVIDIA's Nsight Compute, AMD's rocprofv3 and Apple's Xcode GPU tools are not three different methodologies to learn; they are one methodology, speed-of-light first, then a targeted report for whichever pipeline or reason is limiting, wearing three vendors' names. Nsight Compute's `ncu` command line collects a profile and writes an `.ncu-rep` report; opening that report is a separate step, and the viewer that does it runs natively as a macOS arm64 application, so a report collected on a rented NVIDIA box can be opened and read on the author's own M4 Pro.[^ncu-macos] rocprofv3 plays the same two roles on AMD hardware: it traces the runtime API calls and kernel dispatches an application makes, and it collects the same kind of hardware counters, from the command line.[^rocprofv3] On Apple silicon, the same job is split across two tools inside Xcode: the Metal debugger captures counters per draw or dispatch, breaking a compute pass down by limiter,[^xcode-gpu] while Metal System Trace records a timeline across a whole run, which is where an occupancy problem across many dispatches shows up.[^wwdc22]

None of the three tools does your explaining for you. NVIDIA's own best-practices guide frames the whole activity as a cycle it calls **APOD**: Assess, Parallelize, Optimize, Deploy, run repeatedly, where each pass starts by finding the current bottleneck before touching any code.[^bp-apod] The speed-of-light report is the Assess step made concrete: read it before changing anything, so the change you make next is aimed at whichever axis the report actually named.

??? check "You collect a profile on a rented NVIDIA GPU and plan to compare it against a profile you take next month. What two facts does P1's discipline say to write down next to the numbers, and why?"

    The machine and the date. A rented GPU is not a fixed identity the way a chip on your desk is: instance types, driver versions and even the physical GPU behind a rental can change between sessions, and a profile without that context cannot be told apart from one taken on different hardware. This is the same rule P1 already gave for CPU timings; a GPU counter is no more self-explanatory than a wall-clock number.

## Explaining a speedup with exactly two facts

Go back to the naive-to-coalesced change from the opening section. [G4](g4-memory-performance.md#thirty-two-addresses-one-instruction) already gives the structural reason coalescing helps: it lowers the sector count a warp's load or store touches. Boehm's worklog gives measured numbers for that exact change, on a stated machine: 309 GFLOP/s, 1.3% of cuBLAS, for a naive kernel, rising to 1,987 GFLOP/s, 8.5%, once accesses are coalesced, on an RTX A6000 multiplying two 4,092 × 4,092 matrices in FP32, December 2022.[^boehm] Two facts, a measured ratio and a structural cause, are enough to write a remark that actually explains something:

--8<-- "includes/examples/gpu/g14-measuring-gpu-code/remark_from_facts.cpp.md"

Notice what the remark does not do: it does not add a third fact for good measure, and it does not drop either of the two it has. A ratio on its own ("6.4x faster") is the kind of claim [P1](../optimize/p1-measure-first.md) already warned you not to trust without its context; a rule cited without a number ("we coalesced the accesses") does not tell a reader whether the change was worth making. The discipline is symmetric: cite as many facts as the claim needs, never more, never fewer. This is the same standard [G4](g4-memory-performance.md#for-vortex)'s exercise already asked of a Vortex compiler's own remarks, and it is worth carrying forward explicitly, because a GPU report gives you many more numbers to choose from than a CPU one does, and the temptation to paste in five of them instead of naming the one that matters only grows.

??? check "A kernel's GFLOP/s figure rises after a change, and you can also see that its memory speed-of-light percentage rose. Is the percentage rise, on its own, an explanation for the speedup?"

    Not yet. It restates that memory throughput went up, which is a fact about the same measurement, not a cause outside it. An explanation names the structural reason the accesses coalesce better now, such as a changed index order or a new stride, the way this chapter's remark cites G4's coalescing rule rather than only repeating the throughput number.

## Never present an estimate as a measurement

Everything this chapter has covered comes from hardware counters: a profiler reads what the chip actually did. A compiler's optimizer, by contrast, often works from a **cost model**: a formula that guesses how expensive an operation will be, used to decide whether a transformation is worth applying, before the code has ever run. Nsight Compute's own theoretical-occupancy figure and rocprofv3's achieved-occupancy counter make the distinction concrete on GPUs specifically: the theoretical number is computed from a kernel's register and shared-memory usage against fixed hardware limits, exactly the kind of static estimate a compiler could produce on its own, while the achieved number comes from watching real warps schedule and can fall well short of the theoretical ceiling for reasons no static formula sees, an uneven launch grid, or work finishing at different times across blocks.

Vortex's own philosophy already rules on which of these an unlabelled number is allowed to look like: a Vortex compiler must never present "an unverified performance estimate as a measured result."[^philosophy-quote] Everything in this chapter is the measured side of that rule; [O1](../optimize/o1-optimizer-contract.md)'s optimization remarks are the estimated side, and the two must never blur into each other in anything a Vortex compiler prints, on a GPU exactly as on a CPU.

## For Vortex

!!! vortex "Exercise"

    **Build** a written measurement report, by hand, for one kernel on your own machine, plus a checklist that a reviewer could use to reject a report that skips a step. Vortex has no GPU code generator yet ([M12](../mlir/m12-vortex-gpu-path.md) weighs the paths), so the kernel is hand-written, and the deliverable is the report and the checklist, not compiler code.

    1. Translate the stage 10 naive matmul kernel to a short Metal Shading Language kernel by hand, on the fixed shapes the stage 10 test already uses, and run it from a small host program, compiling the MSL source at run time.
    2. Capture the kernel with Xcode's Metal debugger or GPU counters view. Record the two speed-of-light-style percentages it reports, the machine (model and OS version) and the date, in the style P1 already established for CPU timings.
    3. State a verdict, latency bound, compute bound, memory bound or near the speed of light, using this chapter's rule, and justify it from the two percentages you recorded, not from guessing.
    4. Write one remark, in the style of `remark_from_facts.cpp`, that cites exactly the facts your verdict needs: the two percentages if the kernel is clearly bound one way, or the top stall reason and the issue rate if it is latency bound.
    5. Write the checklist a reviewer would use: every claim names its source (a specific counter or report, not "it felt faster"); every number carries the machine and date; no remark states a bound without both of the percentages that justify it; no estimate is printed as if it were measured.

    **Not yet:** generating GPU code from Vortex source ([M12](../mlir/m12-vortex-gpu-path.md) is undecided); automating this capture in CI, since GPU execution tests should stay local or on a verified self-hosted runner until Metal access from CI is confirmed; a compiler-side cost model for GPU kernels, which belongs with [O1](../optimize/o1-optimizer-contract.md)'s remark stream once Vortex actually emits GPU code.

    **Proof that it works:** the filled report, with both percentages, the verdict, the one-fact-or-two remark and the machine and date; the checklist, applied to your own report with every item checked; and a second pass where you deliberately write a bad remark (a bound stated with only one percentage, or a number with no machine or date) and confirm the checklist catches it.

## Key ideas

!!! recap "Questions you can now answer"

    - **What does a profiler give you that a stopwatch cannot?** Hardware performance counters: readings of what the chip's pipelines actually did, not only how long the whole kernel took.
    - **What are the two speed-of-light axes?** Achieved compute throughput and achieved memory throughput, each as a percentage of that pipeline's own peak; a kernel's place on both tells you what to look at next.
    - **When does a stall-reason breakdown matter?** Only when the issue rate is already low; if the scheduler is issuing on nearly every cycle, no stall reason explains the kernel's performance.
    - **What do Nsight Compute, rocprofv3 and Xcode's Metal tools have in common?** The same methodology, speed-of-light first, then a targeted report, under three vendors' names and interfaces; NVIDIA's own APOD cycle names the discipline explicitly.
    - **How many facts does a good performance remark cite?** Exactly as many as the claim needs: a measured ratio and the structural rule that explains it, no more and no fewer.
    - **What must never be confused with a measured counter?** An estimate from a cost model. A Vortex compiler's own remarks must never present one as the other, on a GPU exactly as on a CPU.

## Where this comes back

!!! next "You will use this again in"

    - [G5. Occupancy and latency hiding](g5-occupancy.md): *theoretical vs. achieved occupancy*, *warp scheduling*
    - [G10. The GPU matmul ladder](g10-matmul-ladder.md): *speed-of-light axes*, *explaining a rung with two facts*
    - [M12. Designing Vortex's GPU path](../mlir/m12-vortex-gpu-path.md): *how performance decisions are reported*
    - [P16. Capstone: the ladder, measured](../optimize/p16-capstone.md): *measurement discipline*, *citing exactly the facts a claim needs*

## Sources and further reading

Read the Nsight Compute Profiling Guide's speed-of-light and warp-state sections first; they define the vocabulary the AMD and Apple tools echo under different names.

[^ncu-sol]: NVIDIA, "Nsight Compute: Profiling Guide", section 2.2.2, "Sections and Rules": the `SpeedOfLight` section, "GPU Speed Of Light Throughput", reporting achieved compute and memory throughput as percentages of each pipeline's theoretical maximum. <https://docs.nvidia.com/nsight-compute/ProfilingGuide/index.html>
[^ncu-warpstate]: NVIDIA, "Nsight Compute: Profiling Guide", section 2.2.2, "Sections and Rules": the `WarpStateStats` section, "Warp State Statistics", reporting the average cycles per issued instruction spent in each warp state. <https://docs.nvidia.com/nsight-compute/ProfilingGuide/index.html>
[^ncu-macos]: NVIDIA, "Nsight Compute: Release Notes": the Nsight Compute host UI runs natively on macOS arm64, connecting to a profile collected on a separate target GPU. <https://docs.nvidia.com/nsight-compute/ReleaseNotes/index.html>
[^bp-apod]: NVIDIA, "CUDA C++ Best Practices Guide", section 2.2, "APOD: A Design Cycle": Assess, Parallelize, Optimize, Deploy, applied repeatedly. <https://docs.nvidia.com/cuda/cuda-c-best-practices-guide/index.html>
[^rocprofv3]: AMD, "ROCprofiler-SDK documentation": rocprofv3 traces runtime API calls and GPU activity, such as kernel dispatches, and collects hardware performance counters. <https://rocm.docs.amd.com/projects/rocprofiler-sdk/en/latest/>
[^xcode-gpu]: Apple, "Analyzing draw command and compute dispatch performance with GPU counters", Xcode documentation. <https://developer.apple.com/documentation/xcode/analyzing-draw-command-and-compute-dispatch-performance-with-gpu-counters>
[^wwdc22]: Apple, "Scale compute workloads across Apple GPUs", WWDC22 session 10159: covers Metal System Trace and occupancy across dispatches. <https://developer.apple.com/videos/play/wwdc2022/10159/>
[^boehm]: Simon Boehm, "How to Optimize a CUDA Matmul Kernel for cuBLAS-like Performance: a Worklog", December 2022: the results table, kernels 1 and 2, on an RTX A6000, 4,092 × 4,092 FP32. <https://siboehm.com/articles/22/CUDA-MMM>
[^philosophy-quote]: Vortex philosophy, "Programmer and compiler responsibilities". <../philosophy.md#programmer-and-compiler-responsibilities>

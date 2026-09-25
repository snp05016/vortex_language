# P4. Seeing inside the CPU: counters and tools

<p class="page-intro">A stopwatch says that one version of a loop is slower than another; it cannot say why. This chapter teaches the tools that can: the processor's own event counters, read by counting and by sampling, the top-down method that sorts lost cycles into four causes, and llvm-mca, which predicts a loop's speed without running it. For Vortex, they turn each rung of the matmul ladder from "it got faster" into a reason that can be checked.</p>

<p class="vx-meta" markdown="1">Level: Intermediate · Reading time: about 45 minutes · Builds on: [P1. Measure first](p1-measure-first.md)</p>

???+ remember "Before you start, remember"

    ??? question "Why is one timed run not a measurement?"

        It is one sample from a noisy process. Scheduling, clock speed and the choice of core all move it, and a single sample shows none of that spread.

        Introduced in [P1. Measure first](p1-measure-first.md#a-stopwatch-is-not-a-measurement).

    ??? question "At which level should you repeat a measurement?"

        At the level where the variation lives: separate process launches, not only iterations inside one launch, with warm-up left out.

        Introduced in [P1. Measure first](p1-measure-first.md#repeat-at-the-right-level).

    ??? question "Besides the number, what must a performance report record?"

        The complete setup: the machine, queried at run time rather than typed from memory, the software and its versions, the date, and how the statistics were computed.

        Introduced in [P1. Measure first](p1-measure-first.md#the-reporting-rules).

    ??? question "Why does walking down a column of a row-major matrix cost more than walking along a row?"

        A cache moves whole lines. Along a row, consecutive elements share a line, so one fetch serves many reads. Down a column, each step jumps a full row, and once that stride reaches the line size nearly every read needs a new line.

        Introduced in [P2. The memory hierarchy](p2-memory-hierarchy.md#cache-lines-and-why-order-matters).

!!! goals "In this chapter"

    - Explain what a hardware performance counter counts, and split a running time into instructions, cycles per instruction and clock rate.
    - Read a counter report from `perf stat` or Instruments, including the scaled estimates that multiplexing produces, and decide which ratios can be trusted.
    - Explain how a sampling profiler finds where events happen, and recognize two ways it misleads: aliasing and skid.
    - Sort a program's pipeline slots into the four top-down categories by hand, and name the tool that does it on your machine.
    - Predict a loop's instructions per cycle with llvm-mca, and say what a gap between that prediction and a measurement means.

## Two loops, one answer, two speeds

The first example multiplies two 512 by 512 matrices of `f32` twice, in two loop orders, and checks that the answers agree bit for bit:

--8<-- "includes/examples/optimize/p4-counters-and-tools/loop_orders.cpp.md"

The first order, `ijk`, is the order of the [stage 10](../compiler/guide/stage-10-matrix-multiplication.md#the-program-the-milestone-asks-for) kernel: for each element of C, run down a row of A and a column of B. The second, `ikj`, swaps the two inner loops, so the innermost loop runs along a row of B and a row of C. Each element of C still receives its products in increasing `k`, one rounded multiplication and one rounded addition at a time, which is why the bits match. (The example compiles with `-ffp-contract=off`, so that no multiplication and addition fuse, as [decision 56](../decisions/numbers.md#d56) requires of Vortex.)

Suppose you time the two orders by [P1](p1-measure-first.md)'s rules and `ikj` wins, as [P2](p2-memory-hierarchy.md#cache-lines-and-why-order-matters)'s account of cache lines predicts. The stopwatch reports the difference; it does not explain it. At least three explanations fit a slower loop:

- It runs **more instructions**. A compiler that vectorizes one order and not the other would do this.
- It runs the same instructions, but **waits longer between them**: for data from memory, or for the result of the previous instruction.
- The processor ran it at a **lower clock rate**, or on a slower core.

Each explanation leads to a different fix, and the running time alone cannot tell them apart. To decide, we need to count what the processor did.

## Counters: the processor counts for you

Every modern processor has a **performance monitoring unit** (PMU), a block of hardware that watches the rest of the core. Its **performance monitoring counters** are registers that add one each time a chosen **event** happens: a clock cycle passes, an instruction completes, a load misses the level 1 data cache, a branch is mispredicted.[^perfbook-pmu] Software in the operating system programs the counters and reads them; a user program reaches them through a tool.

There are few counters and many events. Some counters are **fixed**: they always count one event, such as cycles or instructions. The rest are **programmable**: software picks the event for each.

The perf wiki lists 6 generic counters and no fixed ones for Arm's Cortex-A and X series and for Neoverse N1 and N2, and 4 generic plus 3 fixed for Intel Skylake.[^perfwiki] The events themselves differ between processors, in which ones exist and in exactly what they count, and Bakhvalov notes that on Arm chips they are not well standardized: the vendors that build cores to Arm's architecture vary in which events they support and in what the events mean.[^perfbook-pmu] Read an event's definition for your processor before you build a conclusion on it.

Two events are special because everything else is divided by them.

- **Cycles** counts ticks of the core's clock. The cycles event runs at the core's actual frequency, so when the clock speeds up or slows down, the count of cycles for the same work stays put while the time changes. Bakhvalov recommends comparing two versions of a small piece of code in cycles rather than nanoseconds for exactly this reason.[^perfbook-cycles]
- **Instructions**, more precisely **instructions retired**. A modern core runs instructions ahead of time on a guess, for example past a branch it has not yet resolved. It **retires** an instruction, making its result final, only once every guess it depends on has proved right. Instructions from a wrong guess are executed and then discarded, and the counter does not count them.[^perfbook-retired]

Divide one by the other and you get the most used number in this field. **Instructions per cycle** (IPC) is the average number of instructions retired in each cycle; its inverse, **cycles per instruction** (CPI), is the average number of cycles each one took.[^perfbook-ipc]

With these, a running time splits into three factors:

$$
\text{time} = \text{instructions} \times \frac{\text{cycles}}{\text{instruction}} \times \frac{\text{seconds}}{\text{cycle}}
= \frac{\text{instructions}}{\text{IPC} \times \text{frequency}}
$$

The three explanations of a slow loop are the three factors. More instructions is the first. Waiting is a lower IPC. A slower clock is the third. Two counters and a clock tell you which one moved.

IPC has a ceiling. A core can accept only so many operations per cycle, its **width**, and the width caps IPC, apart from rare cases such as two instructions that the core fuses into one operation. Bakhvalov lists Apple's M1 and M2 designs as 8 wide and Intel Skylake as 4 wide, and advises suspicion when a measured IPC comes out above a core's width.[^perfbook-slots] The other end is informative too: he gives memory-intensive programs an IPC between 0 and 1, and compute-intensive ones between 4 and 6.[^perfbook-ipc]

??? check "A change removes a third of a loop's instructions, and its IPC falls from 2.0 to 1.5 at the same clock rate. Did the loop get faster or slower, and by how much?"

    Faster. Cycles are instructions divided by IPC. Before: I / 2.0 = 0.5 I cycles. After: (2/3) I / 1.5 ≈ 0.44 I cycles, a ratio of 9/8. A lower IPC is not a slower program when the instruction count fell further. This happens often after vectorization, which replaces several scalar instructions by one wider instruction, so the instruction count can fall faster than the cycles. Judge by cycles or time, and use IPC to explain them, never the other way round.

## Counting: one number per event per run

The simplest way to use the counters is **counting**: set them to zero, run the program, read them at the end. On Linux, `perf stat` does this.[^perfwiki] A command such as

```text
perf stat -e '{cycles,instructions}' ./loop_orders ikj
```

counts the two events for one run of one loop order and prints each total. The braces make the two events a **group**, which the next section explains. An event name can carry **modifiers** after a colon: `cycles:u` counts only while the program runs its own code, and `cycles:k` only while the kernel runs on its behalf.[^perf-list] Linux restricts what may be counted through a setting, `/proc/sys/kernel/perf_event_paranoid`; its default since Linux 4.6 allows only user-space measurements.[^perf-event-open]

### Reading a report by hand

The perf wiki's tutorial shows `perf stat` counting `dd` copying zeros for about two seconds, on a machine with two kinds of core, which perf names `cpu_core` and `cpu_atom`.[^perfwiki] Three of its lines, for the `cpu_core` events, are enough to practice on:

| Event | Count | perf's note |
| --- | --- | --- |
| `task-clock` | 2,040.80 msec | 0.9 CPUs utilized |
| `cpu_core/cpu-cycles/` | 8,057,111,968 | 3.9 GHz, (86.46%) |
| `cpu_core/instructions/` | 9,568,892,085 | 1.2 insn per cycle, (86.46%) |

Derive perf's notes yourself:

- **IPC** is instructions divided by cycles: 9,568,892,085 / 8,057,111,968 ≈ 1.19, which perf rounds to 1.2. The CPI is its inverse, about 0.84.
- **Clock rate** is cycles divided by the time the task ran: 8,057,111,968 cycles in 2.0408 seconds is about 3.95 billion cycles per second, perf's 3.9 GHz.
- **The percentage in parentheses** is the share of the run during which the event held a counter. It is below 100% here, so both counts are estimates, and the next section shows how perf made them.

`perf stat -r n` runs the command `n` times and prints the mean of each count with its standard deviation.[^perf-stat] That is repetition at the right level, whole launches, but it summarizes with a mean. [P1](p1-measure-first.md#what-to-report-instead-of-one-number) asked for medians with intervals, so a harness is better off running `perf stat` once per launch and doing its own statistics.

### Counting on a Mac

macOS has no `perf`. Apple's tool is Instruments, and its **CPU Counters** instrument reads the same hardware. Apple's WWDC25 session on CPU performance describes it with a guided configuration, a set of preset **modes**, and a method it calls bottleneck analysis, which the section on top-down analysis below returns to.[^wwdc25] The command-line form is `xctrace`:

```text
xcrun xctrace record --template 'CPU Counters' --launch -- ./loop_orders ikj
```

On the owner's M4 Pro, this command recorded a `.trace` file for `loop_orders` (xctrace 27.0, 2026-09-24). Its default settings, printed with `--show-recording-options`, count at `EL0`, the processor's user level, and start in the mode Instruments names CPU Bottlenecks. `xcrun xctrace export --input <file> --toc` lists the tables inside the recording, among them counter metrics per process, per thread and per core, and the core type each process ran on. The last matters on a chip with two kinds of core: record it beside every count, as P1 asked for the QoS class.

One difference in kind from `perf stat`: the WWDC session says that the CPU Counters instrument works by sampling the workload, and so it profiles a test that repeats the work many times rather than a single call.[^wwdc25] In the recording above, the trace's table of contents gives a sample interval of 1,000 microseconds. Sampling is the subject of the section after next.

## When there are more events than counters

Ask for more events than the PMU has counters and the kernel takes turns. The perf wiki describes it: the events wait on a list, each slice of time programs as many as there are counters, and the list rotates, so every event gets some of the run. At the end, perf scales each count up to the whole run.[^perfwiki]

$$
\text{final count} = \text{raw count} \times \frac{\text{time enabled}}{\text{time running}}
$$

**Time enabled** is how long the event was requested; **time running** is how long it held one. The kernel reports both with each count, for exactly this estimate.[^perf-event-open] This is **multiplexing**, and the perf wiki is blunt about the result: it is an estimate, not a count, and a workload that changes as it runs can hide in the blind spots between turns.[^perfwiki]

The perf wiki's second hand example shows the size of the correction. Counting `cycles` three times over, for one run of a busy loop, each copy is printed "scaled from" about 75%.[^perfwiki] The first copy's estimate is 2,809,725,593 cycles from 74.98% of the run, so its raw count was about 2,809,725,593 × 0.7498 ≈ 2.11 billion, and perf multiplied it by 1/0.7498 to cover the quarter it missed. When the same loop was counted with two copies, neither was scaled.

The second example shows how the estimate goes wrong. It models a run of eight time slices on a core with two counters, asked for four events, with a program that spends two slices in a memory-bound phase and two in a compute phase, over and over:

--8<-- "includes/examples/optimize/p4-counters-and-tools/multiplexing_model.cpp.md"

Every event ran in half of the slices, so every raw count was doubled. For cycles, misses and branch misses the doubling worked, because each got a fair share of both phases. Instructions was unlucky: its turns fell only on memory slices, so its estimate describes the memory phase alone, and the IPC computed from the scaled counts, 0.50, belongs to no part of the program. Figure 1 shows the schedule.

<figure class="vx-figure">
<svg viewBox="0 0 760 330" role="img" aria-label="Eight time slices with two counters and four events: instructions is counted only in memory-bound slices, so its scaled estimate is a third of the truth" aria-describedby="p4-f1-desc">
<title id="p4-f1-title">Multiplexing four events on two counters</title>
<desc id="p4-f1-desc">A grid with eight columns, time slices 0 to 7, and a phase row above them: slices 0 and 1 are memory phase, 2 and 3 compute, 4 and 5 memory, 6 and 7 compute. Four event rows follow, and a cell is filled where the event held a counter. Cycles is filled in slices 0, 3, 4 and 7. Instructions in slices 0, 1, 4 and 5, all of them memory slices, and its row is highlighted and pulses. L1 misses in slices 1, 2, 5 and 6. Branch misses in slices 2, 3, 6 and 7. Every column has exactly two filled cells, one per counter. On the right, each row lists the true count and the scaled estimate: cycles 8000 and 8000, instructions 12000 and 4000, L1 misses 164 and 164, branch misses 16 and 16.</desc>
<text class="vx-text" x="20" y="28">slice</text>
<text class="vx-mono" x="182" y="28" text-anchor="middle">0</text>
<text class="vx-mono" x="232" y="28" text-anchor="middle">1</text>
<text class="vx-mono" x="282" y="28" text-anchor="middle">2</text>
<text class="vx-mono" x="332" y="28" text-anchor="middle">3</text>
<text class="vx-mono" x="382" y="28" text-anchor="middle">4</text>
<text class="vx-mono" x="432" y="28" text-anchor="middle">5</text>
<text class="vx-mono" x="482" y="28" text-anchor="middle">6</text>
<text class="vx-mono" x="532" y="28" text-anchor="middle">7</text>
<text class="vx-text" x="20" y="62">phase</text>
<rect class="vx-box-strong" x="160" y="44" width="94" height="28" rx="3"/>
<text class="vx-text-muted" x="207" y="63" text-anchor="middle">memory</text>
<rect class="vx-box" x="260" y="44" width="94" height="28" rx="3"/>
<text class="vx-text-muted" x="307" y="63" text-anchor="middle">compute</text>
<rect class="vx-box-strong" x="360" y="44" width="94" height="28" rx="3"/>
<text class="vx-text-muted" x="407" y="63" text-anchor="middle">memory</text>
<rect class="vx-box" x="460" y="44" width="94" height="28" rx="3"/>
<text class="vx-text-muted" x="507" y="63" text-anchor="middle">compute</text>
<text class="vx-text-accent" x="600" y="100">true</text>
<text class="vx-text-accent" x="680" y="100">scaled</text>
<text class="vx-mono" x="20" y="133">cycles</text>
<rect class="vx-cell-on" x="162" y="116" width="40" height="26" rx="3"/>
<rect class="vx-box" x="212" y="116" width="40" height="26" rx="3"/>
<rect class="vx-box" x="262" y="116" width="40" height="26" rx="3"/>
<rect class="vx-cell-on" x="312" y="116" width="40" height="26" rx="3"/>
<rect class="vx-cell-on" x="362" y="116" width="40" height="26" rx="3"/>
<rect class="vx-box" x="412" y="116" width="40" height="26" rx="3"/>
<rect class="vx-box" x="462" y="116" width="40" height="26" rx="3"/>
<rect class="vx-cell-on" x="512" y="116" width="40" height="26" rx="3"/>
<text class="vx-mono" x="600" y="133">8000</text>
<text class="vx-mono" x="680" y="133">8000</text>
<g class="vx-pulse">
<text class="vx-mono" x="20" y="175">instructions</text>
<rect class="vx-cell-on" x="162" y="158" width="40" height="26" rx="3"/>
<rect class="vx-cell-on" x="212" y="158" width="40" height="26" rx="3"/>
<rect class="vx-box" x="262" y="158" width="40" height="26" rx="3"/>
<rect class="vx-box" x="312" y="158" width="40" height="26" rx="3"/>
<rect class="vx-cell-on" x="362" y="158" width="40" height="26" rx="3"/>
<rect class="vx-cell-on" x="412" y="158" width="40" height="26" rx="3"/>
<rect class="vx-box" x="462" y="158" width="40" height="26" rx="3"/>
<rect class="vx-box" x="512" y="158" width="40" height="26" rx="3"/>
<text class="vx-mono" x="600" y="175">12000</text>
<text class="vx-text-accent" x="680" y="175">4000</text>
</g>
<text class="vx-mono" x="20" y="217">l1-misses</text>
<rect class="vx-box" x="162" y="200" width="40" height="26" rx="3"/>
<rect class="vx-cell-on" x="212" y="200" width="40" height="26" rx="3"/>
<rect class="vx-cell-on" x="262" y="200" width="40" height="26" rx="3"/>
<rect class="vx-box" x="312" y="200" width="40" height="26" rx="3"/>
<rect class="vx-box" x="362" y="200" width="40" height="26" rx="3"/>
<rect class="vx-cell-on" x="412" y="200" width="40" height="26" rx="3"/>
<rect class="vx-cell-on" x="462" y="200" width="40" height="26" rx="3"/>
<rect class="vx-box" x="512" y="200" width="40" height="26" rx="3"/>
<text class="vx-mono" x="600" y="217">164</text>
<text class="vx-mono" x="680" y="217">164</text>
<text class="vx-mono" x="20" y="259">branch-misses</text>
<rect class="vx-box" x="162" y="242" width="40" height="26" rx="3"/>
<rect class="vx-box" x="212" y="242" width="40" height="26" rx="3"/>
<rect class="vx-cell-on" x="262" y="242" width="40" height="26" rx="3"/>
<rect class="vx-cell-on" x="312" y="242" width="40" height="26" rx="3"/>
<rect class="vx-box" x="362" y="242" width="40" height="26" rx="3"/>
<rect class="vx-box" x="412" y="242" width="40" height="26" rx="3"/>
<rect class="vx-cell-on" x="462" y="242" width="40" height="26" rx="3"/>
<rect class="vx-cell-on" x="512" y="242" width="40" height="26" rx="3"/>
<text class="vx-mono" x="600" y="259">16</text>
<text class="vx-mono" x="680" y="259">16</text>
<rect class="vx-cell-on" x="20" y="296" width="22" height="14" rx="3"/>
<text class="vx-text-muted" x="50" y="308">event held a counter in this slice</text>
<rect class="vx-box" x="330" y="296" width="22" height="14" rx="3"/>
<text class="vx-text-muted" x="360" y="308">event waited; its count is filled in by scaling</text>
</svg>
<figcaption>Figure 1. The schedule of the multiplexing example. Each column has two filled cells, one per counter, and the list of events rotates by one after each slice. Instructions happens to hold a counter only in memory-bound slices, so scaling its raw count by two gives 4000 instead of 12000. The other three events see both phases equally and their estimates come out right, which is luck, not a property of scaling.</figcaption>
</figure>

There are two defenses. The first is to ask for no more events than there are counters, the perf wiki's own advice for avoiding scaling, and to run the program again for the next set.[^perfwiki] The second run of the example does that and gets the true IPC. The second defense is a **group**: events written inside braces, which the kernel schedules onto the counters only together. The `perf_event_open` manual page explains why that matters: the members of a group have counted events for the same instructions, so their values can be meaningfully divided.[^perf-event-open] The perf-list manual recommends groups for exactly this, metrics computed from formulas over several events.[^perf-list]

A group does not make a count complete. If a group is multiplexed with others, its ratio describes the slices in which it ran, which may still miss a phase. It only guarantees that the numerator and the denominator describe the same stretch of the program.

??? check "A report shows `instructions` and `cycles`, each marked as counted for 50% of the run, and not grouped. Each estimate might be close to its true total, and the IPC you compute from them can still be wrong. How, and what do you change?"

    The two events may have held counters in different slices. Each estimate is then extrapolated from a different half of the run, and if the program changes between phases, the ratio mixes the instructions of one half with the cycles of the other. Put the two in one group, `'{cycles,instructions}'`, so they are always counted together, and request no more events than there are counters; count the other events in another run.

## Sampling: where the events happen

A count says how many. To find where, you need **sampling**. The PMU can raise an interrupt when a counter overflows. A sampling profiler presets the counter so that it overflows after a chosen number of events, the **sample period**, and on each interrupt the kernel records where the program was, above all the address of the instruction it was running. The histogram of those addresses, per instruction, per line or per function, is the profile.[^perfwiki][^perfbook-sampling]

Sampling on cycles answers "where does the time go". Sampling on another event answers a sharper question: sample on level 1 data cache misses, and the histogram shows which loads miss. `perf record` samples on cycles by default, and it sets the period to reach an average rate, 1,000 samples per second by default, unless `-c` fixes the period in events.[^perfwiki] Instruments' CPU Counters instrument has sampling modes of the same kind; the WWDC25 session uses one that points at the exact instructions behind a bottleneck, and another for L1 data cache misses.[^wwdc25]

A profile is a statistical estimate, and two effects distort it.

### Aliasing: sampling in step with the program

The third example models a program whose main loop spends 10 cycles in bookkeeping, `tick`, and 90 in real work, and samples it with several periods:

--8<-- "includes/examples/optimize/p4-counters-and-tools/sampling_model.cpp.md"

Counting gives the truth, 10% and 90%. With a period of 1,000 cycles, exactly ten trips, every sample lands at the same point of a trip, the start, where `tick` runs, and the profile blames `tick` for everything. With 997 or 1,009, each sample lands a little earlier or later in the trip than the one before, the samples drift across the whole trip, and the profile comes out right. This is **aliasing**: a sampling period in step with something periodic in the program sees one phase of it again and again (Figure 2).

<figure class="vx-figure">
<svg viewBox="0 0 760 300" role="img" aria-label="A repeating program of a short tick and a long work phase, sampled two ways: a period in step with the loop lands every sample on tick, a period out of step spreads the samples over the trip" aria-describedby="p4-f2-desc">
<title id="p4-f2-title">Aliasing: a sampling period in step with the loop</title>
<desc id="p4-f2-desc">Two copies of the same timeline, each showing eight trips of a loop. Each trip is a short box labelled tick followed by a long box labelled work. In the top timeline, labelled period equal to a whole number of trips, the sample marks light up one after another, and each falls at the start of a trip, inside tick. A note on the right says: 8 of 8 samples in tick, profile says 100 percent tick. In the bottom timeline, labelled period not a multiple of the trip, the sample marks also light up in turn, but each falls at a different point of its trip, most of them inside work and one inside tick. A note says: 1 of 8 samples in tick, close to the true 10 percent. A line at the bottom says the drift is drawn larger than it is with a period of 997 cycles, so that it can be seen.</desc>
<defs><marker id="p4-f2-head" viewBox="0 0 10 10" refX="10" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker></defs>
<text class="vx-text" x="20" y="28">Period equal to a whole number of trips</text>
<g>
<rect class="vx-box-accent" x="20" y="44" width="8" height="30"/><rect class="vx-box" x="28" y="44" width="67" height="30"/>
<rect class="vx-box-accent" x="95" y="44" width="8" height="30"/><rect class="vx-box" x="103" y="44" width="67" height="30"/>
<rect class="vx-box-accent" x="170" y="44" width="8" height="30"/><rect class="vx-box" x="178" y="44" width="67" height="30"/>
<rect class="vx-box-accent" x="245" y="44" width="8" height="30"/><rect class="vx-box" x="253" y="44" width="67" height="30"/>
<rect class="vx-box-accent" x="320" y="44" width="8" height="30"/><rect class="vx-box" x="328" y="44" width="67" height="30"/>
<rect class="vx-box-accent" x="395" y="44" width="8" height="30"/><rect class="vx-box" x="403" y="44" width="67" height="30"/>
<rect class="vx-box-accent" x="470" y="44" width="8" height="30"/><rect class="vx-box" x="478" y="44" width="67" height="30"/>
<rect class="vx-box-accent" x="545" y="44" width="8" height="30"/><rect class="vx-box" x="553" y="44" width="67" height="30"/>
</g>
<path class="vx-line vx-seq" style="--vx-i:0; --vx-n:8" d="M24 112 L24 78" marker-end="url(#p4-f2-head)"/>
<path class="vx-line vx-seq" style="--vx-i:1; --vx-n:8" d="M99 112 L99 78" marker-end="url(#p4-f2-head)"/>
<path class="vx-line vx-seq" style="--vx-i:2; --vx-n:8" d="M174 112 L174 78" marker-end="url(#p4-f2-head)"/>
<path class="vx-line vx-seq" style="--vx-i:3; --vx-n:8" d="M249 112 L249 78" marker-end="url(#p4-f2-head)"/>
<path class="vx-line vx-seq" style="--vx-i:4; --vx-n:8" d="M324 112 L324 78" marker-end="url(#p4-f2-head)"/>
<path class="vx-line vx-seq" style="--vx-i:5; --vx-n:8" d="M399 112 L399 78" marker-end="url(#p4-f2-head)"/>
<path class="vx-line vx-seq" style="--vx-i:6; --vx-n:8" d="M474 112 L474 78" marker-end="url(#p4-f2-head)"/>
<path class="vx-line vx-seq" style="--vx-i:7; --vx-n:8" d="M549 112 L549 78" marker-end="url(#p4-f2-head)"/>
<text class="vx-text" x="636" y="56">8 of 8 in tick</text>
<text class="vx-text-muted" x="636" y="74">profile: 100% tick</text>
<text class="vx-text" x="20" y="150">Period not a multiple of the trip</text>
<g>
<rect class="vx-box-accent" x="20" y="166" width="8" height="30"/><rect class="vx-box" x="28" y="166" width="67" height="30"/>
<rect class="vx-box-accent" x="95" y="166" width="8" height="30"/><rect class="vx-box" x="103" y="166" width="67" height="30"/>
<rect class="vx-box-accent" x="170" y="166" width="8" height="30"/><rect class="vx-box" x="178" y="166" width="67" height="30"/>
<rect class="vx-box-accent" x="245" y="166" width="8" height="30"/><rect class="vx-box" x="253" y="166" width="67" height="30"/>
<rect class="vx-box-accent" x="320" y="166" width="8" height="30"/><rect class="vx-box" x="328" y="166" width="67" height="30"/>
<rect class="vx-box-accent" x="395" y="166" width="8" height="30"/><rect class="vx-box" x="403" y="166" width="67" height="30"/>
<rect class="vx-box-accent" x="470" y="166" width="8" height="30"/><rect class="vx-box" x="478" y="166" width="67" height="30"/>
<rect class="vx-box-accent" x="545" y="166" width="8" height="30"/><rect class="vx-box" x="553" y="166" width="67" height="30"/>
</g>
<path class="vx-line vx-seq" style="--vx-i:0; --vx-n:8" d="M24 234 L24 200" marker-end="url(#p4-f2-head)"/>
<path class="vx-line vx-seq" style="--vx-i:1; --vx-n:8" d="M108 234 L108 200" marker-end="url(#p4-f2-head)"/>
<path class="vx-line vx-seq" style="--vx-i:2; --vx-n:8" d="M192 234 L192 200" marker-end="url(#p4-f2-head)"/>
<path class="vx-line vx-seq" style="--vx-i:3; --vx-n:8" d="M276 234 L276 200" marker-end="url(#p4-f2-head)"/>
<path class="vx-line vx-seq" style="--vx-i:4; --vx-n:8" d="M360 234 L360 200" marker-end="url(#p4-f2-head)"/>
<path class="vx-line vx-seq" style="--vx-i:5; --vx-n:8" d="M444 234 L444 200" marker-end="url(#p4-f2-head)"/>
<path class="vx-line vx-seq" style="--vx-i:6; --vx-n:8" d="M528 234 L528 200" marker-end="url(#p4-f2-head)"/>
<path class="vx-line vx-seq" style="--vx-i:7; --vx-n:8" d="M612 234 L612 200" marker-end="url(#p4-f2-head)"/>
<text class="vx-text" x="636" y="178">1 of 8 in tick</text>
<text class="vx-text-muted" x="636" y="196">close to the true 10%</text>
<rect class="vx-box-accent" x="20" y="258" width="14" height="16"/><text class="vx-text-muted" x="40" y="271">tick, 10 cycles</text>
<rect class="vx-box" x="160" y="258" width="30" height="16"/><text class="vx-text-muted" x="196" y="271">work, 90 cycles</text>
<text class="vx-text-muted" x="330" y="271">arrows: samples (drift drawn larger than 997 gives)</text>
</svg>
<figcaption>Figure 2. The same loop sampled two ways. Top: the period is a whole number of trips, so every sample lands at the same point, inside <code>tick</code>, and the profile shows <code>tick</code> as the whole program. Bottom: a period slightly off gives each sample a different position in its trip, and the samples spread over the trip in proportion to where the time goes.</figcaption>
</figure>

Real programs have periodic parts: a timer interrupt, a loop that polls, a garbage collector. Brendan Gregg samples at 99 Hz rather than 100 Hz so as not to sample in lockstep with some periodic activity.[^gregg] Apple's WWDC25 session describes the same problem in Instruments' Time Profiler, which samples on a timer, and recommends its CPU Profiler instead: it samples each CPU independently on that CPU's own cycle counter, which also stops a fast core from being under-represented next to a slower one.[^wwdc25]

The last line of the example shows the other limit. With about a tenth as many samples, the estimate for `tick` is 9.1% instead of 10%. A share p estimated from n independent samples has a standard error of $\sqrt{p(1-p)/n}$, which for p = 0.1 and n = 99 is about 3 percentage points. A function that took 1% of a short run may not appear in its profile at all.

### Skid: the sample lands after the event

The interrupt does not arrive at the instruction that made the counter overflow. By the time the core stops, it has moved on, and the recorded address is where the program was interrupted. The perf wiki calls the distance **skid** and says it can reach dozens of instructions or more when branches are taken in between.[^perfwiki] A sample on cache misses can therefore point at the instruction after the load that missed, or into the next block.

Some processors can do better: the `perf_event_open` interface lets a tool ask for constant skid or zero skid, through a setting called `precise_ip`, where the hardware supports it.[^perf-event-open] When a hot spot sits on an instruction that cannot cause the event, such as a miss sample on an addition, look a few instructions back.

??? check "A profile taken at exactly 1,000 samples per second on a timer shows a small helper function at 40% of the time, which surprises you. Name two explanations, and a way to tell them apart."

    Either the helper is that hot, or it runs periodically, in step with the sampling timer, and the samples keep landing on it. Sample again at a rate that shares no factor with likely periods, such as 997 or 99 per second, or sample on the cycle counter instead of a timer; if the helper's share drops sharply, it was aliasing. A count of calls, or of the cycles spent inside it with a counter read at entry and exit, gives an independent answer.

### Tracing: every branch, for a short time

Sampling sees a fraction of the run. A **trace** records all of it. Apple's Processor Trace instrument, introduced in Instruments 16.3, sets up the CPU to record every branch decision along with cycle counts and times, and Instruments rebuilds from them the exact sequence of calls, for user-space code on Macs and iPad Pros with M4 chips or later and iPhones with A18 or later.[^wwdc25]

The price is data: the session mentions gigabytes per second for a multithreaded app, and advises tracing for only a few seconds. It must also be enabled first, in the Mac's Privacy & Security settings under Developer Tools. Linux reaches similar hardware through perf, for example Intel's Processor Trace; the perf wiki notes that such features do not use up the general-purpose counters.[^perfwiki]

## Top-down: sorting the lost cycles

IPC says how much work each cycle did. It does not say why a cycle did less than it could. Yasin's **top-down** method, published in 2014, answers with a small, fixed set of categories that every lost opportunity falls into.[^yasin]

The unit is the **pipeline slot**: the resources a core needs to accept one micro-operation into its execution machinery. A core that can accept four per cycle has four slots per cycle, and over a run of C cycles it has 4C slots.[^perfbook-slots] Each slot ends in one of four categories:[^perfbook-tma]

- **Retiring**: the slot held a micro-operation that went on to retire. This is useful work.
- **Bad speculation**: the slot held a micro-operation from a wrong guess, which was thrown away.
- **Frontend bound**: the slot stayed empty because the front of the pipeline, which fetches and decodes instructions, did not deliver one.
- **Backend bound**: the slot stayed empty because the back of the pipeline, which executes operations, was too busy to accept another, for example because earlier operations were still waiting for memory.

Figure 3 sorts the 24 slots of a made-up four-wide core over six cycles.

<figure class="vx-figure">
<svg viewBox="0 0 760 290" role="img" aria-label="A grid of 24 pipeline slots, 4 per cycle over 6 cycles, each marked retiring, backend bound, frontend bound or bad speculation, with the counts totalled on the right" aria-describedby="p4-f3-desc">
<title id="p4-f3-title">Top-down level 1 on a four-wide core</title>
<desc id="p4-f3-desc">A grid of six columns, cycles 1 to 6, and four rows, slots 1 to 4. Cycle 1: all four slots retiring, marked R. Cycle 2: two retiring, two frontend bound, marked F. Cycles 3 and 4: all four slots backend bound, marked B, and these eight cells pulse. Cycle 5: two bad speculation, marked S, and two retiring. Cycle 6: two retiring and two frontend bound. On the right, a tally: retiring 10 of 24 slots, 42 percent; backend bound 8, 33 percent; frontend bound 4, 17 percent; bad speculation 2, 8 percent. A legend shows the four cell styles.</desc>
<text class="vx-text" x="20" y="28">cycle</text>
<text class="vx-mono" x="135" y="28" text-anchor="middle">1</text>
<text class="vx-mono" x="195" y="28" text-anchor="middle">2</text>
<text class="vx-mono" x="255" y="28" text-anchor="middle">3</text>
<text class="vx-mono" x="315" y="28" text-anchor="middle">4</text>
<text class="vx-mono" x="375" y="28" text-anchor="middle">5</text>
<text class="vx-mono" x="435" y="28" text-anchor="middle">6</text>
<text class="vx-text-muted" x="20" y="62">slot 1</text>
<text class="vx-text-muted" x="20" y="106">slot 2</text>
<text class="vx-text-muted" x="20" y="150">slot 3</text>
<text class="vx-text-muted" x="20" y="194">slot 4</text>
<rect class="vx-box-accent" x="110" y="42" width="50" height="34" rx="3"/><text class="vx-mono" x="135" y="64" text-anchor="middle">R</text>
<rect class="vx-box-accent" x="110" y="86" width="50" height="34" rx="3"/><text class="vx-mono" x="135" y="108" text-anchor="middle">R</text>
<rect class="vx-box-accent" x="110" y="130" width="50" height="34" rx="3"/><text class="vx-mono" x="135" y="152" text-anchor="middle">R</text>
<rect class="vx-box-accent" x="110" y="174" width="50" height="34" rx="3"/><text class="vx-mono" x="135" y="196" text-anchor="middle">R</text>
<rect class="vx-box-accent" x="170" y="42" width="50" height="34" rx="3"/><text class="vx-mono" x="195" y="64" text-anchor="middle">R</text>
<rect class="vx-box-accent" x="170" y="86" width="50" height="34" rx="3"/><text class="vx-mono" x="195" y="108" text-anchor="middle">R</text>
<rect class="vx-box" x="170" y="130" width="50" height="34" rx="3"/><text class="vx-mono" x="195" y="152" text-anchor="middle">F</text>
<rect class="vx-box" x="170" y="174" width="50" height="34" rx="3"/><text class="vx-mono" x="195" y="196" text-anchor="middle">F</text>
<g class="vx-pulse">
<rect class="vx-box-strong" x="230" y="42" width="50" height="34" rx="3"/><text class="vx-mono" x="255" y="64" text-anchor="middle">B</text>
<rect class="vx-box-strong" x="230" y="86" width="50" height="34" rx="3"/><text class="vx-mono" x="255" y="108" text-anchor="middle">B</text>
<rect class="vx-box-strong" x="230" y="130" width="50" height="34" rx="3"/><text class="vx-mono" x="255" y="152" text-anchor="middle">B</text>
<rect class="vx-box-strong" x="230" y="174" width="50" height="34" rx="3"/><text class="vx-mono" x="255" y="196" text-anchor="middle">B</text>
<rect class="vx-box-strong" x="290" y="42" width="50" height="34" rx="3"/><text class="vx-mono" x="315" y="64" text-anchor="middle">B</text>
<rect class="vx-box-strong" x="290" y="86" width="50" height="34" rx="3"/><text class="vx-mono" x="315" y="108" text-anchor="middle">B</text>
<rect class="vx-box-strong" x="290" y="130" width="50" height="34" rx="3"/><text class="vx-mono" x="315" y="152" text-anchor="middle">B</text>
<rect class="vx-box-strong" x="290" y="174" width="50" height="34" rx="3"/><text class="vx-mono" x="315" y="196" text-anchor="middle">B</text>
</g>
<rect class="vx-box-bad" x="350" y="42" width="50" height="34" rx="3"/><text class="vx-mono" x="375" y="64" text-anchor="middle">S</text>
<rect class="vx-box-bad" x="350" y="86" width="50" height="34" rx="3"/><text class="vx-mono" x="375" y="108" text-anchor="middle">S</text>
<rect class="vx-box-accent" x="350" y="130" width="50" height="34" rx="3"/><text class="vx-mono" x="375" y="152" text-anchor="middle">R</text>
<rect class="vx-box-accent" x="350" y="174" width="50" height="34" rx="3"/><text class="vx-mono" x="375" y="196" text-anchor="middle">R</text>
<rect class="vx-box-accent" x="410" y="42" width="50" height="34" rx="3"/><text class="vx-mono" x="435" y="64" text-anchor="middle">R</text>
<rect class="vx-box-accent" x="410" y="86" width="50" height="34" rx="3"/><text class="vx-mono" x="435" y="108" text-anchor="middle">R</text>
<rect class="vx-box" x="410" y="130" width="50" height="34" rx="3"/><text class="vx-mono" x="435" y="152" text-anchor="middle">F</text>
<rect class="vx-box" x="410" y="174" width="50" height="34" rx="3"/><text class="vx-mono" x="435" y="196" text-anchor="middle">F</text>
<text class="vx-text" x="500" y="62">retiring</text><text class="vx-mono" x="640" y="62">10</text><text class="vx-text-muted" x="690" y="62">42%</text>
<text class="vx-text" x="500" y="106">backend bound</text><text class="vx-mono" x="640" y="106">8</text><text class="vx-text-muted" x="690" y="106">33%</text>
<text class="vx-text" x="500" y="150">frontend bound</text><text class="vx-mono" x="640" y="150">4</text><text class="vx-text-muted" x="690" y="150">17%</text>
<text class="vx-text" x="500" y="194">bad speculation</text><text class="vx-mono" x="640" y="194">2</text><text class="vx-text-muted" x="690" y="194">8%</text>
<path class="vx-line" d="M500 212 L730 212"/>
<text class="vx-text" x="500" y="232">slots</text><text class="vx-mono" x="640" y="232">24</text>
<rect class="vx-box-accent" x="20" y="252" width="22" height="16" rx="3"/><text class="vx-text-muted" x="48" y="265">R retiring</text>
<rect class="vx-box-strong" x="170" y="252" width="22" height="16" rx="3"/><text class="vx-text-muted" x="198" y="265">B backend bound</text>
<rect class="vx-box" x="350" y="252" width="22" height="16" rx="3"/><text class="vx-text-muted" x="378" y="265">F frontend bound</text>
<rect class="vx-box-bad" x="530" y="252" width="22" height="16" rx="3"/><text class="vx-text-muted" x="558" y="265">S bad speculation</text>
</svg>
<figcaption>Figure 3. Top-down level 1 on a made-up four-wide core over six cycles. Every slot falls in exactly one category, so the four shares add up to 100%. Cycles 3 and 4 are the kind a column walk produces: nothing new can enter while earlier loads wait for memory.</figcaption>
</figure>

Work it by hand. Ten of the 24 slots retired, so 42% of the core's capacity did useful work, and the program retired 10 micro-operations in 6 cycles, about 1.7 per cycle, against a ceiling of 4. The largest loss is backend bound, 8 slots, all in two cycles in which nothing new could start. That points at the next question: which part of the back end? The method is hierarchical: each category splits into finer ones at level 2 and below, and you follow the largest share down.[^perfbook-tma]

Tools do the arithmetic from counters, and they should. Bakhvalov notes that the formulas differ from one processor generation to the next and advises relying on the tools rather than computing the metrics yourself.[^perfbook-tma] The perf wiki's `dd` output above also carried a level 1 breakdown, in lines headed `TopdownL1`, and `perf stat --topdown` asks for one on processors that support it.[^perfwiki][^perf-stat] Arm's "Topdown" method covers its own Neoverse cores; Bakhvalov reported in late 2023 that Apple-designed processors did not support it.[^perfbook-arm]

On the owner's M4 Pro, Instruments' CPU Bottlenecks mode splits the run four ways. The recording above named the four metrics Instruction Delivery Bottleneck, Discarded Bottleneck, Instruction Processing Bottleneck and Useful (xctrace 27.0, 2026-09-24). Read them as delivery for frontend bound, discarded for bad speculation, processing for backend bound and useful for retiring; that correspondence is this chapter's reading of the names, and each mode's own documentation in Instruments is the authority. Its other modes, delivery, processing, discarded sampling and L1D miss sampling among them, are the level below, and the WWDC25 session walks through following a suggested next mode from one to the next.[^wwdc25]

## Predicting without running: llvm-mca

The tools so far measure a program that ran. **llvm-mca**, LLVM's machine code analyzer, predicts instead. Given a sequence of assembly instructions and one of LLVM's **scheduling models**, the per-processor tables of how long each instruction takes and which parts of the core it uses, it simulates the sequence as a loop run many times and estimates IPC and the pressure on each hardware resource.[^llvm-mca] Its value is that it can explain a number: it shows which instruction waits for which.

The fourth example is the innermost loop of each matrix order, written by hand in AArch64 assembly, with comments that mark each loop as a region for llvm-mca:

--8<-- "includes/examples/optimize/p4-counters-and-tools/inner_loops.s.md"

On the owner's M4 Pro, `llvm-mca -mcpu=apple-m1 -iterations=1000 inner_loops.s` reported this (llvm-mca 18.1.8, 2026-09-24; LLVM 18 has no model newer than `apple-m1` for this chip, as [P5](p5-microarchitecture.md#when-the-model-and-the-machine-disagree) explains):

| Region | Instructions per trip | Total cycles, 1,000 trips | Cycles per trip | IPC | Block RThroughput |
| --- | --- | --- | --- | --- | --- |
| `ijk` | 7 | 4,011 | 4.0 | 1.75 | 1.3 |
| `ikj` | 8 | 1,517 | 1.5 | 5.27 | 1.5 |

(These are predictions from a model, not measurements.)

**Block RThroughput** is the model's cycles per trip if nothing carried over from one trip to the next.[^llvm-mca] For `ikj` the prediction and that bound agree, about 1.5 cycles: every trip works on its own element of C, so trips overlap freely. For `ijk` the prediction is three times the bound. Every trip adds into the same `s0`, and each addition must wait for the one before it; the model charges about 4 cycles per trip for that chain. [P5](p5-microarchitecture.md#latency-throughput-and-the-number-of-chains) takes chains like this apart.

Now look at what the table does not contain. In `ijk`, the step down a column of B is in register `x3`. These are the same bytes whether the matrix has 64 columns or 1,024, so llvm-mca's answer is the same for both, while the running time is not, as P2's column walk predicts. The llvm-mca documentation says why: its model of loads and stores knows nothing of the cache hierarchy and does not try to predict whether an access hits or misses the L1 cache; every load gets the model's optimistic latency, which usually matches an L1 hit. It does not model instruction fetch and decode or branch prediction either.[^llvm-mca]

So llvm-mca answers a narrower question than a counter: how fast could this loop run if every load hit the L1 cache and every branch were predicted? That makes the two useful together:

- **Measured IPC close to predicted**: the core is doing what the model says, and the loop's limit is in the instructions: their number, their chains, the ports they use.
- **Measured IPC well below predicted**: the time went somewhere the model does not look, most often memory. Count cache misses next.

Figure 4 puts the chapter's tools side by side: one reads the program, the others watch it run.

<figure class="vx-figure">
<svg viewBox="0 0 760 330" role="img" aria-label="The assembly of a loop goes two ways: to llvm-mca, which predicts from a model that assumes cache hits, and to the real core, whose counters are read by counting, sampling and tracing; the prediction and the measurement meet in one comparison" aria-describedby="p4-f4-desc">
<title id="p4-f4-title">Four tools, two kinds of answer</title>
<desc id="p4-f4-desc">On the left, a box labelled the loop's assembly. An upper arrow leads to a box labelled llvm-mca with a scheduling model, whose output is an estimate of cycles per trip and IPC; a note under it lists what it does not see: caches, branch prediction, instruction fetch. A lower arrow, with moving dashes, leads to a box labelled the core runs it, from which three arrows lead to three boxes: counting, totals per run, with perf stat and CPU Counters; sampling, where the events happen, with perf record and CPU Profiler; tracing, every branch for a short run, with Processor Trace. On the right, the estimate and the counting box both lead into a box labelled compare: measured IPC against predicted IPC, with the note that a large gap points at memory, branches or fetch.</desc>
<defs><marker id="p4-f4-head" viewBox="0 0 10 10" refX="10" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker></defs>
<rect class="vx-box-strong" x="20" y="140" width="120" height="44" rx="4"/>
<text class="vx-text" x="80" y="160" text-anchor="middle">the loop's</text>
<text class="vx-text" x="80" y="176" text-anchor="middle">assembly</text>
<rect class="vx-box" x="200" y="30" width="200" height="50" rx="4"/>
<text class="vx-mono" x="300" y="52" text-anchor="middle">llvm-mca</text>
<text class="vx-text-muted" x="300" y="70" text-anchor="middle">+ scheduling model</text>
<text class="vx-text-muted" x="200" y="100">assumes: L1 hits, no branch</text>
<text class="vx-text-muted" x="200" y="116">or fetch effects</text>
<rect class="vx-box" x="200" y="190" width="160" height="44" rx="4"/>
<text class="vx-text" x="280" y="217" text-anchor="middle">the core runs it</text>
<path class="vx-line" d="M140 150 L199 60" marker-end="url(#p4-f4-head)"/>
<path class="vx-flow" d="M140 174 L199 210" marker-end="url(#p4-f4-head)"/>
<rect class="vx-box-accent" x="420" y="140" width="176" height="44" rx="4"/>
<text class="vx-text" x="432" y="158">counting: totals</text>
<text class="vx-text-muted" x="432" y="175">perf stat, CPU Counters</text>
<rect class="vx-box" x="420" y="200" width="176" height="44" rx="4"/>
<text class="vx-text" x="432" y="218">sampling: where</text>
<text class="vx-text-muted" x="432" y="235">perf record, CPU Profiler</text>
<rect class="vx-box" x="420" y="260" width="176" height="44" rx="4"/>
<text class="vx-text" x="432" y="278">tracing: every branch</text>
<text class="vx-text-muted" x="432" y="295">Processor Trace</text>
<path class="vx-line" d="M360 205 L419 164" marker-end="url(#p4-f4-head)"/>
<path class="vx-line" d="M360 212 L419 222" marker-end="url(#p4-f4-head)"/>
<path class="vx-line" d="M360 222 L419 280" marker-end="url(#p4-f4-head)"/>
<rect class="vx-box-accent" x="616" y="64" width="128" height="80" rx="4"/>
<text class="vx-text" x="628" y="86">compare</text>
<text class="vx-text-muted" x="628" y="104">measured IPC</text>
<text class="vx-text-muted" x="628" y="120">against the</text>
<text class="vx-text-muted" x="628" y="136">estimate</text>
<path class="vx-line" d="M400 50 L680 50 L680 63" marker-end="url(#p4-f4-head)"/>
<text class="vx-text-accent" x="500" y="43">estimate</text>
<path class="vx-line" d="M596 156 L615 130" marker-end="url(#p4-f4-head)"/>
<text class="vx-text-accent" x="602" y="176">measured</text>
<text class="vx-text-muted" x="616" y="206">a large gap:</text>
<text class="vx-text-muted" x="616" y="222">memory, branches</text>
<text class="vx-text-muted" x="616" y="238">or fetch</text>
</svg>
<figcaption>Figure 4. What each tool sees. llvm-mca reads the instructions and a model of the core, and never runs anything. Counting, sampling and tracing watch the real core, from the cheapest answer (one total per event) to the most detailed (every branch, for a few seconds). The useful comparison is between the two kinds: a measured IPC well below the model's prediction points at what the model leaves out.</figcaption>
</figure>

Two cautions. First, the hand-written `ikj` loop above is scalar, but Apple clang 21 vectorizes `multiply_ikj` in the first example at `-O2` (checked 2026-09-24), so the loop that ran has fewer, wider instructions. Always run llvm-mca on the assembly the compiler produced. The documentation shows how to mark a region in compiled code, with comments that the compiler passes through to the assembly.[^llvm-mca] Second, a prediction is an estimate, and Vortex's [philosophy](../philosophy.md#programmer-and-compiler-responsibilities) forbids presenting an unverified estimate as a measured result. Every llvm-mca number in a report carries its tool version, its `-mcpu`, and the word "estimate".

??? check "llvm-mca predicts the same cycles per trip for the `ijk` inner loop whether the matrices are 64 by 64 or 1,024 by 1,024. Is it wrong?"

    No; it answers a different question. The instructions are identical, since the column stride lives in a register, and llvm-mca assumes every load hits the L1 cache. At 64 by 64 the three matrices take 3 × 16 KiB = 48 KiB, which fits in the M4 Pro performance core's 128 KiB L1 data cache that P2 queried; at 1,024 by 1,024 each matrix is 4 MiB and the column walk misses. The gap between prediction and measurement at the large size is the memory cost, and a cache-miss counter is the tool that confirms it.

## Your turn: explaining the two loop orders

Here is the measurement plan for the first example at `n = 512`, with the tools of this chapter, one row per loop order. The llvm-mca row is filled in from the table above; everything else you measure, by P1's rules, on your own machine. Two of the "expect" cells are filled in:

| Quantity | Tool | `ijk` | `ikj` | Expect |
| --- | --- | --- | --- | --- |
| Time, median and interval | vortex-bench ([P1](p1-measure-first.md#for-vortex)) | | | `ikj` faster |
| Instructions | counter | | | ? |
| Cycles | counter, same group | | | ? |
| IPC | derived | | | ? |
| L1 data cache misses per 1,000 instructions | counter, second run | | | ? |
| Largest top-down category | Instruments or `perf stat` | | | ? |
| Predicted IPC, scalar loop by hand (estimate) | llvm-mca 18.1.8, `apple-m1` | 1.75 | 5.27 | `ijk` below its prediction |
| Predicted IPC, compiler's own loop (estimate) | llvm-mca | | | ? |

Machine, core type, date, compiler and flags: ________.

Decide what to expect in each `?` cell before you measure, and write down why. Then measure. Where the measurement disagrees with your expectation, the disagreement is the interesting part.

??? check "What should each counter row show, and why?"

    - **Instructions**: fewer for `ikj`. Apple clang 21 vectorizes its whole inner loop, while in `ijk` it can pair up the multiplications but must add the products into the sum one at a time, in order (checked in the `-O2` assembly on 2026-09-24). This is an instructions effect, separate from memory.
    - **Cycles**: fewer for `ikj`; this is the time, in cycles, without the clock rate.
    - **IPC**: no firm prediction. `ijk` should be well below its llvm-mca prediction, because its column walk misses. `ikj` retires fewer, wider instructions, which can lower its IPC even as it gets faster, the check question in the section on counters.
    - **Misses per 1,000 instructions**: far higher for `ijk`, whose B accesses are 2 KiB apart, beyond a 128-byte line; `ikj` walks every array with a 4-byte stride. Divide by instructions so that the vectorized loop's smaller count does not flatter it, and check both absolute counts too.
    - **Top-down**: backend bound, or processing on Apple's names, should dominate `ijk`; `ikj` should retire a larger share.
    - **Compiler's loop under llvm-mca**: for `ikj`, a vector loop, so more work per instruction than the hand-written scalar one.

    If a result contradicts this list, check the setup before the theory: the event's definition on your processor, whether the count was scaled, and which core type the run landed on.

## For Vortex

!!! vortex "Exercise"

    **Build** counters and a static estimate into `vortex-bench`, the harness from [P1](p1-measure-first.md#for-vortex). It stays a tool outside the compiler, in any language you like.

    1. **A counter mode.** For each launch, run the program under `perf stat` on Linux or `xctrace record --template 'CPU Counters'` on macOS. Request cycles and instructions as one group, and other events, such as L1 data cache misses, in separate launches, so that no launch asks for more events than the core has counters. Store every raw count with the tool's own event name and its time running over time enabled.
    2. **Derived metrics per launch**, then P1's statistics across launches: IPC, cycles per innermost trip (cycles divided by n³ for an n by n multiplication), and misses per 1,000 instructions. Refuse to divide two counts that were not counted in one group, and mark every result computed from a scaled count.
    3. **A static estimate.** Give your compiler a way to write the assembly it generates, if it has none, and mark the stage 10 kernel's innermost loop as an llvm-mca region. Have the harness run llvm-mca on it with an `-mcpu` it records, and report predicted cycles per trip and IPC in their own columns, labelled as estimates with the tool version.
    4. **The setup in the JSON**: the tool and its version, the core type each launch ran on where the tool reports it, and everything P1 already records.

    **Not yet:** sampling profiles and attributing samples to source lines; automatic top-down drill-down; Processor Trace; programming the PMU yourself through `perf_event_open` or private frameworks; any compiler decision driven by counts, such as profile-guided optimization; a cache simulator.

    **Proof that it works:**

    - **Calibrate on a known answer.** Run the counter mode on `loop_orders` from this chapter, `ijk` against `ikj` at `n = 512`. The harness must report more L1 data cache misses per 1,000 instructions for `ijk`. A harness that cannot see this effect will not see a subtler one.
    - **A planted doubling.** P1's copy of the stage 10 program that multiplies twice must show an instructions ratio near 2 against the original, with its interval.
    - **The grouping guard.** Ask the harness for more events than your core has counters, in one launch. It must mark the scaled counts, and it must refuse to compute IPC from cycles and instructions that were not grouped.
    - **Estimate against measurement.** Fill in this table for the stage 10 kernel, with the machine and the date. If the measured cycles per trip differ from the estimate by more than the measurement's interval, write one sentence on which side you believe and why:

    | Program | Launches | Measured IPC (median, interval) | Measured cycles per inner trip | L1D misses per 1,000 instructions | llvm-mca IPC (estimate) | llvm-mca cycles per trip (estimate) | `-mcpu`, core type |
    | --- | --- | --- | --- | --- | --- | --- | --- |
    | stage 10, 64 by 64 | | | | | | | |
    | stage 10, planted doubling | | | | | | | |

## Key ideas

!!! recap "Questions you can now answer"

    - **What do cycles and instructions tell you that a stopwatch cannot?** Their ratio, IPC: whether a change made the program run fewer instructions or the same instructions with less waiting, independent of the clock rate.
    - **Why can a program get faster while its IPC falls?** Time is instructions divided by IPC and clock rate; vectorization, for one, cuts instructions by more than it cuts IPC.
    - **When is a counter value an estimate?** When the core had fewer counters than requested events, so the kernel multiplexed them and scaled each count by time enabled over time running.
    - **Why group events?** Grouped events are always counted together, so their ratio describes the same instructions; ungrouped ones may each have sampled a different phase.
    - **How can a sampling profile mislead?** By aliasing, when the period falls in step with periodic work, and by skid, when the recorded address lies past the instruction that caused the event.
    - **What does top-down analysis add to IPC?** It sorts every pipeline slot into retiring, bad speculation, frontend bound or backend bound, which says where the missing IPC went.
    - **What does llvm-mca predict, and what does it ignore?** Cycles and IPC for a loop from a scheduling model, assuming L1 hits and no branch or fetch effects; a measured IPC well below its prediction points at what it ignores.

## Where this comes back

!!! next "You will use this again in"

    - [P5. The microarchitecture shelf](p5-microarchitecture.md): *IPC*, *llvm-mca and scheduling models*, *model against measurement*
    - [P7. Loop transformations](p7-loop-transformations.md): *counters that confirm an interchange*
    - [P8. Cache blocking](p8-cache-blocking.md): *misses per tile size*
    - [P10. Vectorization](p10-vectorization.md): *fewer instructions, lower IPC*
    - [P13. Multithreading](p13-multithreading.md): *per-core counts*, *which core type ran the work*
    - [P16. Capstone: the ladder, measured](p16-capstone.md): *counters beside every time*, *estimates labelled as estimates*
    - [C6. Instruction scheduling](../backend/c6-scheduling.md): *scheduling models*
    - [E3. LLVM's allocator, scheduler and MC layer](../backend/e3-llvm-allocator-scheduler-mc.md): *llvm-mca*
    - [G14. Measuring GPU code](../gpu/g14-measuring-gpu-code.md): *counters and speed of light on a GPU*

## Sources and further reading

Start with the perf wiki's tutorial: the sections on counting, multiplexing and sampling are short, and each makes one of this chapter's warnings with real output. Then read chapters 4 to 6 of Bakhvalov's book, which is free, for the metrics, sampling and top-down analysis across Intel, AMD and Arm. For a Mac, watch Apple's WWDC25 session 308, whose example follows a slow function from a profile through Processor Trace to counter modes. Finish with the llvm-mca guide's section "How llvm-mca works".

[^perfwiki]: perf wiki, "Tutorial", sections "Counting with perf stat", "multiplexing and scaling events", "Event-based sampling overview", "Default event: cycle counting" and "Period and rate". <https://perfwiki.github.io/main/tutorial/>
[^perf-list]: Linux man-pages, perf-list(1), sections "EVENT MODIFIERS" and "EVENT GROUPS". <https://man7.org/linux/man-pages/man1/perf-list.1.html>
[^perf-stat]: Linux man-pages, perf-stat(1), options `-r` (`--repeat`) and `--topdown`. <https://man7.org/linux/man-pages/man1/perf-stat.1.html>
[^perf-event-open]: Linux man-pages, perf_event_open(2): the description of `group_fd`, the fields `precise_ip`, `time_enabled` and `time_running`, and the file `/proc/sys/kernel/perf_event_paranoid`. <https://man7.org/linux/man-pages/man2/perf_event_open.2.html>
[^gregg]: Brendan Gregg, "Linux perf Examples", the section on CPU profiling at 99 Hertz. <https://www.brendangregg.com/perf.html>
[^wwdc25]: Apple, "Optimize CPU performance with Instruments", WWDC25 session 308, transcript: the sections on CPU Profiler, Processor Trace and bottleneck analysis. <https://developer.apple.com/videos/play/wwdc2025/308/>
[^llvm-mca]: LLVM Project, "llvm-mca - LLVM Machine Code Analyzer", sections "Description", "Using Markers to Analyze Specific Code Blocks", "How llvm-mca Works", "Instruction Flow" and "Load/Store Unit and Memory Consistency Model". <https://llvm.org/docs/CommandGuide/llvm-mca.html>
[^yasin]: Ahmad Yasin, "A Top-Down Method for Performance Analysis and Counters Architecture", *IEEE International Symposium on Performance Analysis of Systems and Software (ISPASS)*, 2014. The paper is paywalled; this chapter's account of the method follows Bakhvalov's chapter on it. <https://doi.org/10.1109/ISPASS.2014.6844459>
[^perfbook-pmu]: Denis Bakhvalov et al., *Performance Analysis and Tuning on Modern CPUs*, section "Performance Monitoring Unit" and its subsection "Performance Monitoring Counters". <https://github.com/dendibakh/perf-book>
[^perfbook-retired]: Bakhvalov et al., *Performance Analysis and Tuning on Modern CPUs*, section "Retired vs. Executed Instruction". <https://github.com/dendibakh/perf-book>
[^perfbook-ipc]: Bakhvalov et al., *Performance Analysis and Tuning on Modern CPUs*, section "CPI and IPC". <https://github.com/dendibakh/perf-book>
[^perfbook-slots]: Bakhvalov et al., *Performance Analysis and Tuning on Modern CPUs*, section "Pipeline Slot". <https://github.com/dendibakh/perf-book>
[^perfbook-cycles]: Bakhvalov et al., *Performance Analysis and Tuning on Modern CPUs*, section "Core vs. Reference Cycles". <https://github.com/dendibakh/perf-book>
[^perfbook-sampling]: Bakhvalov et al., *Performance Analysis and Tuning on Modern CPUs*, section "Sampling", subsection "Finding Hotspots". <https://github.com/dendibakh/perf-book>
[^perfbook-tma]: Bakhvalov et al., *Performance Analysis and Tuning on Modern CPUs*, section "Top-down Microarchitecture Analysis". <https://github.com/dendibakh/perf-book>
[^perfbook-arm]: Bakhvalov et al., *Performance Analysis and Tuning on Modern CPUs*, section "TMA On Arm Platforms". <https://github.com/dendibakh/perf-book>

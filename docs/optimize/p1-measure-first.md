# P1. Measure first

<p class="page-intro">How to time code so the number means something, before anything is changed. Every later chapter in this book compares a "before" against an "after"; this chapter is what makes that comparison trustworthy.</p>

<p class="vx-meta" markdown="1">Level: Foundations · Reading time: about 20 minutes · Builds on: [10. Matrix multiplication](../compiler/guide/stage-10-matrix-multiplication.md)</p>

???+ remember "Before you start, remember"

    ??? question "Why must the naive matmul kernel from stage 10 add each row-times-column term to `sum` in increasing order of `k`, with no fused multiply-add?"

        Because the known answer it is checked against was computed the same way. Reordering the sum, or fusing a multiply with the add that follows it, can change the last bit of an `f32` result, so a kernel that "looks right" could print a different answer on a different machine.

        Introduced in [10. Matrix multiplication, Comparing with a known answer](../compiler/guide/stage-10-matrix-multiplication.md#comparing-with-a-known-answer).

    ??? question "According to Vortex's roadmap, what has to exist before any performance optimization work begins?"

        A correct, unoptimized baseline: the naive triple loop from stage 10, compiled without optimization. Its output is the known answer every faster version must reproduce, and its running time is the number every later chapter's claims are measured against.

        Introduced in [10. Matrix multiplication, Why speed can wait](../compiler/guide/stage-10-matrix-multiplication.md#why-speed-can-wait).

    ??? question "What does a good optimization remark let a reader check that a changelog entry does not?"

        Exactly which rule let a transformation fire, or exactly which rule stopped it, at a named source location, so the claim can be checked against the IR instead of taken on faith.

        Introduced in [O1. The optimizer's contract, What a good remark says](o1-optimizer-contract.md#what-a-good-remark-says).

    ??? question "What must a Vortex compiler never do with a performance number?"

        Present an unverified estimate as a measured result. A cost model's guess and a stopwatch's reading are different things, and the compiler's output must never blur them.

        Introduced in [Philosophy, Programmer and compiler responsibilities](../philosophy.md#programmer-and-compiler-responsibilities).

!!! goals "In this chapter"

    - Explain why a single timed run is not a measurement, and name the two ways the same binary can report two different numbers without anyone changing a line of code.
    - Recognize the difference between a point estimate (the best of a few runs) and a distribution with a confidence interval, and say which one a performance claim needs.
    - Build a compiler barrier that stops an optimizer from quietly doing less work than a benchmark loop asks for.
    - Apply a short list of reporting habits (state costs, not only ratios; do not assume symmetric noise; say how many repetitions at each level) to a number before writing it down.
    - Connect every one of these habits to the discipline Vortex's own philosophy already demands of its compiler.

## A stopwatch is not a measurement

Say you have finished the naive matmul kernel from [stage 10](../compiler/guide/stage-10-matrix-multiplication.md), and you want to know whether it is fast. The obvious thing to do is wrap it in a clock:

```text
start = now()
multiply(a, b, c)
elapsed = now() - start
print(elapsed)
```

Run this twice, back to back, on the same binary, on the same machine, and the two numbers will usually differ. Not because the kernel does different work the second time: `multiply` reads the same `a` and `b` and writes the same answer into `c` every time, by the correctness rule stage 10 already established. The two numbers differ because a running program shares the machine with everything else on it. The operating system scheduler can preempt it mid-loop to run something else. The processor's clock frequency can step up or down between the two runs. On the author's machine, macOS assigns each thread a **quality of service (QoS) class**, an operating-system hint about how much and how soon a piece of work matters, and work with a low QoS class tends to land on the machine's efficiency cores rather than its performance cores.[^apple-tuning] None of this shows up in the source code. All of it shows up in the clock.

Call this **noise**: variation in a timing that comes from the environment the code ran in, not from the code itself. A single timed run is one sample drawn from a noisy process, and a sample of size one tells you almost nothing about the process it came from. Two runs that differ by 20% could mean the second run did 20% less work, or it could mean nothing happened to your code and everything happened to the scheduler. Looking at one number cannot tell the two apart. That is the first thing this chapter asks you to stop doing: treating a single reading as if it were the truth about a program's speed.

## Where the noise comes from: measurement bias

Noise from scheduling and frequency scaling is at least visible: run the kernel enough times and you will notice the spread. A more unsettling source of variation is not. In 2009, Mytkowicz, Diwan, Hauswirth and Sweeney surveyed 133 papers from systems conferences and found that essentially none of them controlled for it: **measurement bias**, meaning that something about *how* a program was built and launched, and not the algorithm itself, changed how fast it ran.[^mytkowicz] Two culprits they identified were the size of the process's environment variables (which shifts where the stack starts, and therefore how later data structures line up with cache lines) and the order in which object files were linked (which shifts where functions land in memory, and therefore which ones alias in the instruction cache). Neither of these is a property of the algorithm. Both can change a benchmark's reported winner.

Their conclusion was not "control for those two things specifically." Link order and environment size were the two biases they happened to demonstrate; the general lesson is that a single compiled binary, run once, is only one sample from a whole space of possible memory layouts, and the sample you happened to build might not be a typical one. Curtsinger and Berger's STABILIZER made this concrete a few years later by repeatedly re-randomizing a running program's code and data layout during measurement, specifically so that a change in the reported time reflects the algorithm and not an accident of where the linker happened to put things.[^stabilizer] You do not need STABILIZER's tooling to take the lesson: before trusting that version B of a kernel is faster than version A, ask whether A and B differ only in the code you meant to change, or also in something about how the two binaries happened to be built and started.

This is a case where the honest thing to write down is a method, not a number, because no single run on one machine on one day settles the question:

| What varied | Median with version A | Median with version B | Did the ranking change? |
| --- | --- | --- | --- |
| baseline (no change) | | | |
| environment padded with an unused 512-byte variable | | | |
| object files linked in reverse order | | | |

Fill this in yourself, for a kernel you have changed, before you believe a speedup. If the ranking between A and B flips depending on a column that has nothing to do with either version's algorithm, the "speedup" you measured first was measurement bias, not a result.

??? check "Two builds of `vortex-bench`, compiled from identical source with identical flags, report medians that differ by more than either build's confidence interval. What is the most likely explanation, and what does Curtsinger and Berger's argument say to do about it?"

    Something about the build or launch environment differs between the two builds even though the source and flags do not: a different `PATH`, a different working directory, a different link order chosen by the linker on that run. Neither build is "wrong". The fix is not to pick whichever run looks better; it is to vary the things you are not testing (environment size, layout) across many runs, so that a real difference between the two versions stands out from that noise instead of being drowned by it.

## What to report instead of one number

If one reading is not a measurement, the fix is not "read it twice and take the smaller one." That is worse: the minimum of a few noisy readings is a biased estimate, because it only ever reports the luckiest run, never a typical one. Georges, Buytaert and Eeckhout made the same argument for managed-language benchmarking: report a distribution, with a confidence interval, not a single "best" number chosen after the fact.[^georges] Kalibera and Jones extended this into a full protocol, arguing that a benchmark has repetitions at more than one level (how many times you rebuild, how many processes you launch, how many iterations each process runs), and that each level needs its own accounting before the result can be trusted.[^kalibera]

The example below takes a small, fixed set of ten timing-shaped numbers (arbitrary ticks, not a real measurement of anything) and computes three summaries from it: the minimum (what a "best of N" report would show), the median, and a **bootstrap confidence interval**, an interval built by resampling the data itself, with replacement, many times, and taking the spread of a statistic (here the median) across those resamples as a stand-in for the spread you would see if you had gathered many more real samples.[^kalibera]

--8<-- "includes/examples/optimize/p1-measure-first/repeated_measurements.cpp.md"

The minimum, 96, is the smallest of the ten numbers: exactly the value a "run it a few times and keep the best" report would publish. The median, 102, sits close to seven of the ten readings. The 95% interval, [98, 231], is wide, because two of the ten readings (231 and 275) are far above the rest: something happened during those two runs that did not happen during the other eight. A single "best" number hides that split entirely. A median with an interval cannot.

<figure class="vx-figure">
<svg viewBox="0 0 760 330" role="img" aria-label="A single best-of-ten bar next to a strip plot of all ten readings with a median and 95 percent confidence interval" aria-describedby="p1-f1-desc">
<desc id="p1-f1-desc">Left panel: one bar, labelled &quot;best of 10, one run&quot;, reaching to 96 on a shared vertical scale from 0 to 300+. Right panel: ten dots at their individual values, scattered horizontally to avoid overlap, mostly clustered between 96 and 104 with two dots far higher, at 231 and 275, and one at 305. A shaded band covers the 95 percent bootstrap interval from 98 to 231, with a dashed line through it at the median, 102.</desc>
<line class="vx-line" x1="60" y1="280" x2="700" y2="280" stroke-dasharray="1 0"/>
<line class="vx-line" x1="60" y1="211.25" x2="700" y2="211.25" stroke-dasharray="2 4"/>
<line class="vx-line" x1="60" y1="142.5" x2="700" y2="142.5" stroke-dasharray="2 4"/>
<line class="vx-line" x1="60" y1="73.75" x2="700" y2="73.75" stroke-dasharray="2 4"/>
<text class="vx-text-muted" x="50" y="284" text-anchor="end">0</text>
<text class="vx-text-muted" x="50" y="215" text-anchor="end">100</text>
<text class="vx-text-muted" x="50" y="146" text-anchor="end">200</text>
<text class="vx-text-muted" x="50" y="78" text-anchor="end">300</text>
<text class="vx-text-muted" x="30" y="160" text-anchor="middle" transform="rotate(-90 30 160)">ticks (lower is faster)</text>

<text class="vx-text" x="165" y="30" text-anchor="middle">one reading</text>
<rect class="vx-box-bad" x="110" y="214" width="110" height="66" rx="3"/>
<text class="vx-mono" x="165" y="204" text-anchor="middle">96</text>
<text class="vx-text-muted" x="165" y="298" text-anchor="middle">best of 10, one run</text>

<text class="vx-text" x="560" y="30" text-anchor="middle">ten readings, same binary</text>
<rect class="vx-box-accent" x="480" y="121.2" width="160" height="91.4" rx="3"/>
<line class="vx-line" x1="480" y1="209.9" x2="640" y2="209.9" stroke-dasharray="4 3"/>
<text class="vx-text-accent" x="650" y="125" text-anchor="start">95% CI</text>
<text class="vx-text-accent" x="650" y="213" text-anchor="start">median</text>
<circle class="vx-dot" cx="536" cy="208.5" r="4"/>
<circle class="vx-dot" cx="572" cy="212.6" r="4"/>
<circle class="vx-dot" cx="554" cy="121.2" r="4"/>
<circle class="vx-dot" cx="548" cy="210.6" r="4"/>
<circle class="vx-dot" cx="584" cy="213.3" r="4"/>
<circle class="vx-dot" cx="560" cy="70.3" r="4"/>
<circle class="vx-dot" cx="560" cy="211.9" r="4"/>
<circle class="vx-dot" cx="524" cy="209.2" r="4"/>
<circle class="vx-dot" cx="596" cy="214.0" r="4"/>
<circle class="vx-dot" cx="566" cy="90.9" r="4"/>
<text class="vx-text-muted" x="560" y="298" text-anchor="middle">10 runs, one binary</text>
</svg>
<figcaption>Figure 1. The same ten readings, shown two ways. Left: what a "best of 10" report publishes, a single bar at the fastest run. Right: the full spread, with the 95% bootstrap interval for the median shaded and the two slow outliers visible as separate dots. Which one would you publish?</figcaption>
</figure>

??? check "A teammate reports: 'I ran the kernel once before my change and once after; it went from 210 to 185 ticks, a 12% speedup.' What is missing before that number means anything?"

    A distribution for each side, not one reading each. With only one "before" and one "after" reading, a 12% difference is completely consistent with ordinary noise; nothing rules out the possibility that running "before" a second time would also have printed something other than 210. Ask for a median and a confidence interval on both sides, from independent runs, before treating 12% as a property of the change rather than of that one pair of runs.

## Keeping the compiler from helping too much

There is a second way a benchmark can lie, and it comes from the thing this book spends its middle chapters praising: the optimizer. [O1](o1-optimizer-contract.md#the-as-if-rule) explains the **as-if rule**: a compiler may do anything it wants to a program's execution, as long as the observable behavior is as if it had run the program exactly as written. A benchmark loop that computes a value and never uses it satisfies that rule perfectly if the optimizer deletes the loop, because "not doing the work" and "doing the work and throwing the answer away" are observably identical. A benchmark loop that calls a pure function with an input that never changes satisfies the rule just as well if the optimizer calls the function once and reuses the answer four more times, because from the outside, five identical calls and one call plus four reuses look the same.

Both of these are exactly what you want an optimizer to do to real code, and exactly what you do not want it to do to a timing loop, because a timing loop's whole purpose is to make the same work happen repeatedly so a clock has something to measure. If the optimizer quietly turns five iterations of real work into one iteration and four no-ops, the clock will faithfully report how long one iteration took, and you will report it as the cost of five.

The fix is a small, ugly piece of code whose only job is to tell the optimizer "I might have read or written this, and I cannot prove that to you, so keep the work." The google/benchmark library calls its two versions of this idea `DoNotOptimize` and `ClobberMemory`;[^gbench-ug] the example below reimplements the same idea from its description, using the same underlying mechanism, an inline-assembly statement with no instructions and a `"memory"` clobber, which tells the compiler that anything it currently believes about memory contents might now be false.

--8<-- "includes/examples/optimize/p1-measure-first/escape_and_clobber.cpp.md"

`escape(input)` at the top of the loop stops the compiler from proving that `input` is the same value it was in a previous iteration, so it cannot hoist the call to `work` out of the loop. `escape(result)` stops the compiler from proving that `result` is unused, so it cannot delete the call to `work` entirely. `clobberMemory()` stops it from reordering the accumulation across that point. Open this file with the "Open in Compiler Explorer" link on this page, compile it at `-O2` with and without the three escape lines, and compare how many times `work`'s body appears in the generated assembly. The printed total does not change either way; what changes is whether the machine did the work five times or only once.

??? check "With the three escape calls deleted, `main`'s loop still runs five times, and the printed total does not change. Why might the compiler still only call `work` once in the generated code, and why is that exactly the failure a benchmark loop cannot afford?"

    `work` is pure and its input, 42, never changes between iterations, so the compiler can prove all five calls return the same value and replace the loop with one call whose result is used five times, satisfying the as-if rule. The visible output is identical either way, which is exactly the problem: a benchmark that times this loop believing it does five units of work is really timing one unit, and every number it reports is off by a factor the source code gives no hint of.

## The reporting rules

Hoefler and Belli distilled a set of twelve rules for reporting performance results from scientific computing practice; three of them are the ones this chapter has been building toward, and they are worth stating on their own.[^hoefler]

- **Summarize costs, not ratios.** "1.3x faster" throws away the two numbers it was computed from. Report the before and after costs, each with its own interval, and let the ratio follow from them; a reader who only sees the ratio cannot tell whether it came from a 130-second run against a 100-second run or a 13-millisecond run against a 10-millisecond run, and cannot recompute it if either baseline turns out to be wrong.
- **Do not assume the noise is symmetric.** A confidence interval built by assuming a normal distribution can extend below zero for a quantity, like time, that cannot be negative. The bootstrap interval used earlier makes no assumption about the shape of the noise; it only resamples the data you actually collected.
- **State how many repetitions you used, at every level that has one.** "We ran it 100 times" is ambiguous between 100 iterations inside one process and 100 independent process launches; Kalibera and Jones showed that these can have different noise characteristics, and a reader cannot judge a result without knowing which one produced it.[^kalibera]

Alongside the rule for what to report, google/benchmark's own guidance and LLVM's benchmarking notes converge on a short list of what to control before measuring anything: disable address space layout randomization for the run if you are comparing memory-layout-sensitive code, disable CPU frequency scaling or at least measure it, and pin the process to specific cores so the scheduler is not free to move it mid-run.[^gbench-var][^llvm-bench]

??? check "Why is 'we ran it 100 times and it was always faster' weaker evidence than a median and a 95% confidence interval computed from the same 100 runs?"

    "Always faster" only tells you the two distributions do not overlap in the range you happened to sample; it says nothing about how far apart they are or how much either one varies. A confidence interval carries a size and a location, so it can be compared numerically to another interval, added to a table, or checked against a threshold. "Always" is a fact about 100 specific numbers; a median with an interval is a claim about the process that generated them.

## A moving target: hardware you do not fully control

Even a well-built benchmark is measuring a machine you only partly control. On macOS, as noted above, the operating system's QoS classes steer work toward performance or efficiency cores depending on how urgent the system believes it to be, and background activity on the machine competes for both.[^apple-tuning] The right response is not to memorize your machine's core counts and clock speeds and bake them into a report; the research notes behind this book make that point explicitly, and it generalizes: hardware facts belong in a query, not in a comment, because the next machine that runs your benchmark will have different ones. `sysctl`, `/proc/cpuinfo`, and their equivalents exist so a benchmark harness can record what it ran on instead of assuming.

## For Vortex

!!! vortex "Exercise"

    Build `vortex-bench`, a small harness for timing a Vortex kernel honestly.

    **Build:**

    - A correctness gate that runs first: compare the kernel's output against a known answer (the same comparison stage 10 already uses) and refuse to report a timing at all if it fails. A fast wrong answer is not a result.
    - Multiple independent invocations of the kernel, not only multiple iterations inside one process, with the repetition count at each level printed alongside the result.
    - A median and a bootstrap confidence interval (or another method you can name and justify) computed from those invocations, plus the minimum, so a reader can see both what you are reporting and what a "best of N" report would have said instead.
    - Output as JSON, including the machine facts a reader would need to judge whether your numbers apply to their machine: at minimum, the CPU model and how you queried it.
    - A compiler barrier around the timed section so that compiling the kernel and the harness together at a high optimization level cannot make the harness itself measure less work than it asks for.

    **Do not build yet:** layout randomization across runs, automatic outlier rejection, or any comparison against another tool's numbers. Those are later chapters' problems ([P4](p4-counters-and-tools.md), [P16](p16-capstone.md)); `vortex-bench`'s job here is to produce one number you can defend, not to explain why it moved.

    **The test that proves it works:** run `vortex-bench` twice, back to back, on the same unmodified kernel binary. The two confidence intervals should overlap. Then break the correctness gate on purpose (change one loop bound) and confirm the harness reports a failure instead of a timing. A benchmark harness that cannot fail its own correctness check is not trustworthy when it reports a speed.

## Key ideas

!!! recap "You should now be able to answer"

    - Why is a single timed run not a measurement? Because it is one sample from a noisy process, and noise from scheduling, frequency scaling and core assignment can be larger than the effect you are trying to see.
    - What is measurement bias, and why is it worse than ordinary noise? It is variation caused by something about how a program was built or launched (environment size, link order), not by the algorithm; it can flip which of two versions looks faster, and a benchmark that never varies those factors will never notice.
    - Why is "best of N" a worse summary than a median with a confidence interval? The minimum only ever reports the luckiest run; a median with an interval reports where most of the readings sit, and how sure you can be of that.
    - What does a compiler barrier like `DoNotOptimize` protect a benchmark from? From the optimizer legally, correctly, applying the as-if rule to a timing loop and doing less real work than the loop appears to ask for, while leaving the visible output unchanged.
    - What should a performance report state instead of a bare speedup ratio? The costs it was computed from, each with its own interval, plus how many repetitions were used at every level that has one.
    - What must `vortex-bench` do before it reports any timing at all? Check the kernel's output against a known answer, and refuse to report a number when that check fails.

## Where this comes back

!!! next "You will use this again in"

    - [P3. The roofline model](p3-roofline.md): a *measured GFLOP/s* value, with its confidence interval, is exactly what gets plotted against the roofline's ceilings.
    - [P4. Seeing inside the CPU: counters and tools](p4-counters-and-tools.md): hardware performance counters need the same repetition discipline, and profilers add their own sampling noise on top.
    - [P10. Vectorization](p10-vectorization.md): the `DoNotOptimize` barrier reappears wherever a microbenchmark isolates one loop from the program around it.
    - [P16. Capstone: the ladder, measured](p16-capstone.md): every rung of the CPU matmul ladder is judged by the median and interval this chapter's method produces, not by a single best run.

## Sources and further reading

[^mytkowicz]: Todd Mytkowicz, Amer Diwan, Matthias Hauswirth, Peter F. Sweeney, "Producing Wrong Data Without Doing Anything Obviously Wrong!", *ASPLOS 2009*. <https://doi.org/10.1145/1508244.1508275>
[^georges]: Andy Georges, Dries Buytaert, Lieven Eeckhout, "Statistically Rigorous Java Performance Evaluation", *OOPSLA 2007*. <https://doi.org/10.1145/1297027.1297033>
[^kalibera]: Tomas Kalibera, Richard Jones, "Rigorous Benchmarking in Reasonable Time", *ISMM 2013*. <https://doi.org/10.1145/2464157.2464160>
[^hoefler]: Torsten Hoefler, Roberto Belli, "Scientific Benchmarking of Parallel Computing Systems", *SC 2015*. <https://doi.org/10.1145/2807591.2807644>
[^stabilizer]: Charlie Curtsinger, Emery D. Berger, "STABILIZER: Statistically Sound Performance Evaluation for Systems", *ASPLOS 2013*. <https://doi.org/10.1145/2451116.2451141>
[^gbench-ug]: google/benchmark, "User Guide", section "Preventing Optimization". <https://github.com/google/benchmark/blob/main/docs/user_guide.md>
[^gbench-var]: google/benchmark, "Reducing Variance". <https://github.com/google/benchmark/blob/main/docs/reducing_variance.md>
[^llvm-bench]: LLVM Project, "Benchmarking tips and tricks". <https://llvm.org/docs/Benchmarking.html>
[^apple-tuning]: Apple, "Tuning your code's performance for Apple silicon". <https://developer.apple.com/documentation/apple-silicon/tuning-your-code-s-performance-for-apple-silicon>

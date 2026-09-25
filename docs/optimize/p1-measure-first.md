# P1. Measure first

<p class="page-intro">How to time code so that the number means something: what to repeat, what to report, how to compare two versions, and how to stop the optimizer from emptying the thing being timed. Every later chapter in this book compares a "before" with an "after"; this chapter is what makes that comparison worth believing, and it ends with the harness Vortex's own measurements will run on.</p>

<p class="vx-meta" markdown="1">Level: Foundations · Reading time: about 40 minutes · Builds on: [10. Matrix multiplication](../compiler/guide/stage-10-matrix-multiplication.md)</p>

???+ remember "Before you start, remember"

    ??? question "Where should the known answer for a matrix multiplication test come from?"

        From somewhere other than the compiler under test: a hand calculation for small cases, or a trusted tool for larger ones. Stage 10 picks small whole-number inputs so that every product and sum is exact and the result can be compared with `==`.

        Introduced in [10. Matrix multiplication](../compiler/guide/stage-10-matrix-multiplication.md#comparing-with-a-known-answer).

    ??? question "Why does stage 10 leave speed for later?"

        Performance work begins after v0.1, and an optimization needs something to be measured against. The naive program is that baseline: its output is the known answer every faster version must reproduce.

        Introduced in [10. Matrix multiplication](../compiler/guide/stage-10-matrix-multiplication.md#why-speed-can-wait).

    ??? question "What may a compiler change under the as-if rule?"

        Anything, provided the program's observable behavior stays the same. Which instructions run, and whether a computation happens at all, is not observable.

        Introduced in [O1. The optimizer's contract](o1-optimizer-contract.md#the-as-if-rule).

    ??? question "Which performance statement must a Vortex compiler never make?"

        It must never present an unverified performance estimate as a measured result. The programmer, for their part, is responsible for measuring performance claims on representative inputs and hardware.

        Introduced in [Philosophy](../philosophy.md#programmer-and-compiler-responsibilities).

!!! goals "In this chapter"

    - Explain why one timed run is not a measurement, and tell **noise** (spread you can see by repeating) from **measurement bias** (a shift that repeating cannot show).
    - Choose where to repeat: builds, process launches or iterations inside one launch, and which runs to leave out as warm-up.
    - Compute, by hand and in code, a median with a confidence interval from ranks and by bootstrap, and compare two versions with a ratio and its interval.
    - Recognize when the optimizer has emptied a timing loop, and keep a benchmark's work alive without hiding what the compiler did to the work itself.
    - Report a result the way Hoefler and Belli's rules ask: costs before ratios, rates from times, the setup recorded.

## A stopwatch is not a measurement

Say you have finished the naive matmul program from [stage 10](../compiler/guide/stage-10-matrix-multiplication.md), and you want to know how fast it is. The obvious move is to wrap it in a clock:

```text
start = now()
multiply(a, b, c)
elapsed = now() - start
print(elapsed)
```

Run this twice, back to back, on the same binary and the same machine, and the two numbers will usually differ. The kernel did the same work both times: it read the same `a` and `b` and wrote the same `c`, which the known-answer test checks. The difference comes from everything else on the machine.

The operating system can pause the program to run something else. The processor can change its clock speed between runs. On macOS, the system uses a thread's **quality-of-service (QoS) class**, a label saying how important the work is, when it decides where the thread runs, and Apple's tuning guide says that it is more likely to run background work on lower-performance cores.[^apple-tuning] None of this is in the source code. All of it is in the clock.

Call this **noise**: variation in a timing that comes from the environment the code ran in, not from the code. One timed run is a single sample from a noisy process, and a sample of one tells you nothing about the spread. If a second run is 20% faster, the program may have done less work, or the scheduler may have left it alone that time; one number each cannot tell the two apart.

Two practical details come before any statistics. First, the clock. Use a **monotonic clock**, one that never goes backwards, such as C++'s `std::chrono::steady_clock`, which cppreference describes as the clock best suited to measuring intervals.[^cppref-steady] A wall clock can jump when the system corrects its time. Second, the clock itself has a cost and a resolution. Hoefler and Belli suggest keeping the cost of reading the timer under 5% of the interval being measured, and a timer precision ten times finer than that interval. When one call is too short to meet that, time a batch of `k` calls and divide by `k`, and say so, because your statistics then describe batches, not single calls.[^hb]

??? check "A kernel call takes somewhere around a microsecond, and your clock's reading costs about as long as the call. What goes wrong if you time each call on its own, and what do you do instead?"

    Each measurement is then roughly half clock and half kernel, so the result says as much about the clock as about the code, and it breaks Hoefler and Belli's suggestion that timer overhead stay under 5% of the interval. Time a batch of many calls between two clock readings, divide by the batch size, and record the batch size with the result.

## Bias: when repeating does not help

Noise at least announces itself: run the program ten times and you see the spread. Its more dangerous relative does not. **Measurement bias** is a shift in one direction that comes from the experimental setup and is the same on every repetition, so repeating a biased experiment gives a tight cluster of wrong answers.

Mytkowicz, Diwan, Hauswirth and Sweeney showed how ordinary it is.[^mytk] They changed two things that have nothing to do with any algorithm. One was the total size of the UNIX environment variables, which is loaded into memory before the call stack, so it moves the stack and with it the alignment of local variables. For a tiny loop compiled without optimization on a Core 2 machine, changing the size of an unused environment variable changed the run time "frequently by about 33% and once by almost 300%".

The other was the order in which object files were given to the linker, which moves code and data. Built with 33 different link orders, the SPEC benchmark `perlbench` showed a speedup of `-O3` over `-O2` anywhere from 0.92 to 1.10 on the Core 2: a slowdown or a speedup, depending on the link order.

<figure class="vx-figure">
<svg viewBox="0 0 760 300" role="img" aria-label="Noise compared with bias: one setup with a wide spread of run times, and three setups each with a tight cluster at a different place" aria-describedby="p1-f2-desc">
<desc id="p1-f2-desc">Two panels share a horizontal axis labelled run time. Left panel, noise: one setup, twelve dots spread widely along the axis; a bracket under them says repeating shows this spread. Right panel, bias: three rows, one per setup, labelled environment size A, B and C. Each row has a tight cluster of dots, and the three clusters sit at different places along the axis. A dashed line marks where the average over all three setups falls. A note says that each setup on its own looks precise, and only varying the setup reveals the shift.</desc>
<text class="vx-text" x="20" y="28">Noise: one setup, many runs</text>
<line class="vx-line" x1="20" y1="200" x2="340" y2="200"/>
<text class="vx-text-muted" x="180" y="222" text-anchor="middle">run time →</text>
<circle class="vx-dot" cx="70" cy="150" r="5"/>
<circle class="vx-dot" cx="98" cy="150" r="5"/>
<circle class="vx-dot" cx="122" cy="150" r="5"/>
<circle class="vx-dot" cx="140" cy="150" r="5"/>
<circle class="vx-dot" cx="155" cy="150" r="5"/>
<circle class="vx-dot" cx="170" cy="150" r="5"/>
<circle class="vx-dot" cx="170" cy="136" r="5"/>
<circle class="vx-dot" cx="186" cy="150" r="5"/>
<circle class="vx-dot" cx="204" cy="150" r="5"/>
<circle class="vx-dot" cx="226" cy="150" r="5"/>
<circle class="vx-dot" cx="258" cy="150" r="5"/>
<circle class="vx-dot" cx="300" cy="150" r="5"/>
<path class="vx-line" d="M70 172 L70 180 L300 180 L300 172"/>
<text class="vx-text-muted" x="185" y="250" text-anchor="middle">repeating shows the spread;</text>
<text class="vx-text-muted" x="185" y="268" text-anchor="middle">statistics can measure it</text>
<text class="vx-text" x="400" y="28">Bias: three setups, a few runs each</text>
<line class="vx-line" x1="400" y1="200" x2="740" y2="200"/>
<text class="vx-text-muted" x="570" y="222" text-anchor="middle">run time →</text>
<text class="vx-text-muted" x="400" y="72">env. size A</text>
<circle class="vx-dot" cx="500" cy="68" r="5"/>
<circle class="vx-dot" cx="508" cy="68" r="5"/>
<circle class="vx-dot" cx="516" cy="68" r="5"/>
<circle class="vx-dot" cx="522" cy="68" r="5"/>
<text class="vx-text-muted" x="400" y="117">env. size B</text>
<circle class="vx-dot" cx="636" cy="113" r="5"/>
<circle class="vx-dot" cx="644" cy="113" r="5"/>
<circle class="vx-dot" cx="650" cy="113" r="5"/>
<circle class="vx-dot" cx="658" cy="113" r="5"/>
<text class="vx-text-muted" x="400" y="162">env. size C</text>
<circle class="vx-dot" cx="560" cy="158" r="5"/>
<circle class="vx-dot" cx="567" cy="158" r="5"/>
<circle class="vx-dot" cx="574" cy="158" r="5"/>
<circle class="vx-dot" cx="582" cy="158" r="5"/>
<line class="vx-line" x1="576" y1="48" x2="576" y2="192" stroke-dasharray="4 3"/>
<text class="vx-text-accent" x="581" y="46">average over setups</text>
<text class="vx-text-muted" x="570" y="250" text-anchor="middle">each row alone looks precise; only</text>
<text class="vx-text-muted" x="570" y="268" text-anchor="middle">changing the setup shows the shift</text>
</svg>
<figcaption>Figure 1. Noise and bias. Left: a single setup whose runs spread out, which repetition reveals. Right: three setups that differ only in something irrelevant, such as the size of the environment; each gives a tight cluster, in a different place. A measurement taken in one setup reports its own cluster with confidence, whichever one it happened to be.</figcaption>
</figure>

The survey in the same paper is the sobering part: of 133 recent papers from ASPLOS, PACT, PLDI and CGO, none of those with experimental results considered measurement bias adequately.[^mytk] The authors propose two remedies. **Setup randomization** runs the experiment in many different setups (environment sizes, link orders) and summarizes the whole distribution, so no single lucky layout decides the answer. **Causal analysis** tests a conclusion by changing its suspected cause on purpose and checking that the effect moves as the explanation predicts.

Curtsinger and Berger put the layout problem in one sentence: a single binary is only one sample from the space of possible memory layouts, however many times it runs.[^stab] Their tool, Stabilizer, randomizes the placement of code, stack frames and heap objects and keeps re-randomizing while the program runs, so one experiment samples many layouts. With it, they found that on SPEC CPU2006 the effect of LLVM's `-O3` over `-O2` could not be told apart from random noise. The LLVM project's own benchmarking notes put the conclusion plainly: low noise is required, but it is not sufficient, because it does not exclude measurement bias.[^llvm-bench]

You do not need Stabilizer to take the lesson. Before believing that version B beats version A, vary something that neither version should care about and see whether the answer survives. The table is a method, not a result; fill it in for a change of your own:

| What varied | Median, version A | Median, version B | Same winner? |
| --- | --- | --- | --- |
| nothing (the baseline setup) | | | |
| an unused environment variable of 512 bytes added | | | |
| another 512 bytes (1,024 in all) | | | |
| object files linked in reverse order | | | |

If the winner changes with a row that has nothing to do with either version, the first "speedup" was the setup.

??? check "You compare two builds of a kernel. Each was launched 30 times from the same terminal, and the two 95% intervals are narrow and far apart. A colleague reruns both from a different directory and the order flips. Which kind of problem is this, and why did 30 launches not protect you?"

    Measurement bias. The 30 launches of each build shared one setup (the same environment, including the working directory, and the same link order), so they sampled the noise of that setup but never its bias: the narrow intervals were precise about the wrong thing. The remedy is to vary the setup on purpose, as the table above does, and report whether the conclusion survives.

## Repeat at the right level

Variation enters at more than one level. A rebuild can change the layout, through the link order for instance. Each **process launch** starts the program afresh, at addresses the operating system may choose differently each time. And the **iterations** inside one launch differ from one another too. Kalibera and Jones make this the core of their method: choose how many repetitions to run at each level (build, launch, iteration), and report the result as an **effect size**, how much faster, with a confidence interval.[^kj] Repetitions pay off at the level where the variation is: a thousand iterations inside one launch say nothing about how much the next launch will differ.

<figure class="vx-figure">
<svg viewBox="0 0 760 300" role="img" aria-label="Three nested levels of repetition: builds contain process launches, launches contain iterations, and the first iterations of each launch are warm-up" aria-describedby="p1-f3-desc">
<desc id="p1-f3-desc">Two large boxes, build 1 with link order 1 and build 2 with link order 2. Inside each are three launch boxes. Inside each launch box is a row of eight small iteration squares; the first two are drawn in the muted style and labelled warm-up, the other six are outlined as steady. Arrows lead from each build down to a row at the bottom labelled one summary per launch, which holds six dots, one for each launch, and a bracket over all six dots is labelled the interval is computed across launches.</desc>
<defs><marker id="p1-f3-head" viewBox="0 0 10 10" refX="10" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker></defs>
<rect class="vx-box" x="20" y="20" width="350" height="190" rx="6"/>
<text class="vx-text" x="34" y="42">build 1 (link order 1)</text>
<rect class="vx-box" x="390" y="20" width="350" height="190" rx="6"/>
<text class="vx-text" x="404" y="42">build 2 (link order 2)</text>
<g>
<rect class="vx-box-strong" x="36" y="56" width="318" height="40" rx="4"/>
<text class="vx-text-muted" x="44" y="80">launch</text>
<rect class="vx-box-bad" x="100" y="66" width="22" height="20" rx="2"/><rect class="vx-box-bad" x="126" y="66" width="22" height="20" rx="2"/>
<rect class="vx-box-accent" x="152" y="66" width="22" height="20" rx="2"/><rect class="vx-box-accent" x="178" y="66" width="22" height="20" rx="2"/><rect class="vx-box-accent" x="204" y="66" width="22" height="20" rx="2"/><rect class="vx-box-accent" x="230" y="66" width="22" height="20" rx="2"/><rect class="vx-box-accent" x="256" y="66" width="22" height="20" rx="2"/><rect class="vx-box-accent" x="282" y="66" width="22" height="20" rx="2"/>
<rect class="vx-box-strong" x="36" y="104" width="318" height="40" rx="4"/>
<text class="vx-text-muted" x="44" y="128">launch</text>
<rect class="vx-box-bad" x="100" y="114" width="22" height="20" rx="2"/><rect class="vx-box-bad" x="126" y="114" width="22" height="20" rx="2"/>
<rect class="vx-box-accent" x="152" y="114" width="22" height="20" rx="2"/><rect class="vx-box-accent" x="178" y="114" width="22" height="20" rx="2"/><rect class="vx-box-accent" x="204" y="114" width="22" height="20" rx="2"/><rect class="vx-box-accent" x="230" y="114" width="22" height="20" rx="2"/><rect class="vx-box-accent" x="256" y="114" width="22" height="20" rx="2"/><rect class="vx-box-accent" x="282" y="114" width="22" height="20" rx="2"/>
<rect class="vx-box-strong" x="36" y="152" width="318" height="40" rx="4"/>
<text class="vx-text-muted" x="44" y="176">launch</text>
<rect class="vx-box-bad" x="100" y="162" width="22" height="20" rx="2"/><rect class="vx-box-bad" x="126" y="162" width="22" height="20" rx="2"/>
<rect class="vx-box-accent" x="152" y="162" width="22" height="20" rx="2"/><rect class="vx-box-accent" x="178" y="162" width="22" height="20" rx="2"/><rect class="vx-box-accent" x="204" y="162" width="22" height="20" rx="2"/><rect class="vx-box-accent" x="230" y="162" width="22" height="20" rx="2"/><rect class="vx-box-accent" x="256" y="162" width="22" height="20" rx="2"/><rect class="vx-box-accent" x="282" y="162" width="22" height="20" rx="2"/>
</g>
<g>
<rect class="vx-box-strong" x="406" y="56" width="318" height="40" rx="4"/>
<text class="vx-text-muted" x="414" y="80">launch</text>
<rect class="vx-box-bad" x="470" y="66" width="22" height="20" rx="2"/><rect class="vx-box-bad" x="496" y="66" width="22" height="20" rx="2"/>
<rect class="vx-box-accent" x="522" y="66" width="22" height="20" rx="2"/><rect class="vx-box-accent" x="548" y="66" width="22" height="20" rx="2"/><rect class="vx-box-accent" x="574" y="66" width="22" height="20" rx="2"/><rect class="vx-box-accent" x="600" y="66" width="22" height="20" rx="2"/><rect class="vx-box-accent" x="626" y="66" width="22" height="20" rx="2"/><rect class="vx-box-accent" x="652" y="66" width="22" height="20" rx="2"/>
<rect class="vx-box-strong" x="406" y="104" width="318" height="40" rx="4"/>
<text class="vx-text-muted" x="414" y="128">launch</text>
<rect class="vx-box-bad" x="470" y="114" width="22" height="20" rx="2"/><rect class="vx-box-bad" x="496" y="114" width="22" height="20" rx="2"/>
<rect class="vx-box-accent" x="522" y="114" width="22" height="20" rx="2"/><rect class="vx-box-accent" x="548" y="114" width="22" height="20" rx="2"/><rect class="vx-box-accent" x="574" y="114" width="22" height="20" rx="2"/><rect class="vx-box-accent" x="600" y="114" width="22" height="20" rx="2"/><rect class="vx-box-accent" x="626" y="114" width="22" height="20" rx="2"/><rect class="vx-box-accent" x="652" y="114" width="22" height="20" rx="2"/>
<rect class="vx-box-strong" x="406" y="152" width="318" height="40" rx="4"/>
<text class="vx-text-muted" x="414" y="176">launch</text>
<rect class="vx-box-bad" x="470" y="162" width="22" height="20" rx="2"/><rect class="vx-box-bad" x="496" y="162" width="22" height="20" rx="2"/>
<rect class="vx-box-accent" x="522" y="162" width="22" height="20" rx="2"/><rect class="vx-box-accent" x="548" y="162" width="22" height="20" rx="2"/><rect class="vx-box-accent" x="574" y="162" width="22" height="20" rx="2"/><rect class="vx-box-accent" x="600" y="162" width="22" height="20" rx="2"/><rect class="vx-box-accent" x="626" y="162" width="22" height="20" rx="2"/><rect class="vx-box-accent" x="652" y="162" width="22" height="20" rx="2"/>
</g>
<text class="vx-text-muted" x="100" y="206">warm-up</text>
<text class="vx-text-muted" x="200" y="206">steady iterations</text>
<path class="vx-line" d="M320 76 L360 76 L360 250 L300 250" marker-end="url(#p1-f3-head)"/>
<path class="vx-line" d="M690 76 L735 76 L735 250 L470 250" marker-end="url(#p1-f3-head)"/>
<circle class="vx-dot" cx="250" cy="250" r="5"/><circle class="vx-dot" cx="280" cy="250" r="5"/><circle class="vx-dot" cx="290" cy="250" r="5"/>
<circle class="vx-dot" cx="480" cy="250" r="5"/><circle class="vx-dot" cx="500" cy="250" r="5"/><circle class="vx-dot" cx="515" cy="250" r="5"/>
<text class="vx-text-muted" x="20" y="254">one summary per launch</text>
<path class="vx-line" d="M250 270 L250 278 L515 278 L515 270"/>
<text class="vx-text-accent" x="382" y="296" text-anchor="middle">the interval is computed across launches</text>
</svg>
<figcaption>Figure 2. Three levels of repetition. Builds differ in layout; each launch starts afresh; inside a launch, the first iterations run while caches and other hardware state settle (warm-up, left out of the statistics) and the rest are the steady state. Each launch is boiled down to one number, and the uncertainty is computed across those numbers.</figcaption>
</figure>

**Warm-up** is the first of those choices. The first executions of a kernel are often slower: its data is not yet in the caches, and the processor has not yet learned its branches. Georges, Buytaert and Eeckhout separate **start-up performance**, the cost of the first run, from **steady-state performance**, the cost once the program has settled; they are answers to different questions, and a report must say which one it gives.[^georges] Google Benchmark lets a benchmark ask for a minimum warm-up time before measurement starts.[^gbench-ug] How many runs to discard is not a constant: look at a plot of iteration time against iteration number for your kernel on your machine, and decide from it.

Iterations inside one launch share one layout and one warm cache, so they are not independent samples of the thing you care about. Treat the **launch** as the unit: boil each launch down to one number (for example the median of its steady iterations) and compute the uncertainty across launches. When you compare versions, **interleave** them, running A, B, A, B (or a shuffled order) rather than all of A and then all of B, so that slow drift, such as a machine heating up, lands on both. Google Benchmark offers random interleaving of repetitions for this reason.[^gbench-ug]

## What to report instead of one number

If one reading is not a measurement, the fix is not to read it several times and keep the smallest. The minimum answers a real question, how fast the code runs when nothing interferes, but it is the one run least like the others, and it hides everything the other runs saw. Georges and his coauthors show where that leads.[^georges] Comparing Java garbage collectors, the best of 30 runs called two collectors about equal when the confidence intervals showed a clear difference, and declared a winner between two others whose intervals overlapped: that collector had one unusually good run among many ordinary ones.

Here is a worked example. Fifteen launches of one kernel, in made-up ticks, not measured: thirteen ran undisturbed and two were interrupted.

```text
104  98  231  101  97  99  103  96  275  100  102  99  105  98  101
```

Sorted, they read `96 97 98 98 99 99 100 101 101 102 103 104 105 231 275`. Three summaries come straight off that line:

- the **minimum**, 96: what a "best of 15" report publishes;
- the **mean**, 1809 / 15 = 120.6: pulled up by the two slow launches to a value no launch came near;
- the **median**, the middle (8th) of the 15 sorted values, 101: one or two slow launches cannot move it far.

A median alone still lacks its uncertainty. A **confidence interval** is a range computed so that, if the whole experiment were repeated many times, 95% of such ranges would contain the true median. Timing data is rarely the symmetric bell curve many textbook formulas assume. Hoefler and Belli observe that most system effects make runs slower, so measured times tend to be skewed to the right, often with several peaks.[^hb] They give an interval for the median that assumes nothing about the shape, built from ranks (after Le Boudec). With `z = 1.96` for 95%:

$$
\text{lower rank} = \left\lfloor \frac{n - z\sqrt{n}}{2} \right\rfloor,
\qquad
\text{upper rank} = \left\lceil 1 + \frac{n + z\sqrt{n}}{2} \right\rceil
$$

For `n = 15`, $z\sqrt{15} \approx 7.59$, so the lower rank is $\lfloor 3.70 \rfloor = 3$ and the upper rank is $\lceil 12.30 \rceil = 13$. The 3rd and 13th sorted values are 98 and 105: the median is in [98, 105] with 95% confidence. The minimum, 96, lies outside it.

The second common method is the **bootstrap**: draw 15 launches from the 15 you have, with replacement (so some appear twice and some not at all), take the median of that resample, and repeat a few thousand times; the 2.5th and 97.5th percentiles of those medians form the interval. It needs no formula for the statistic, which is why it also works for a ratio of two medians, below. The example does both.

--8<-- "includes/examples/optimize/p1-measure-first/repeated_measurements.cpp.md"

The two intervals differ, [98, 105] from ranks and [98, 103] from the bootstrap. Neither is wrong: they are two estimates from fifteen numbers, and a report names the method it used. The bootstrap uses random numbers, so the example writes its own small generator and fixes its seed; `std::uniform_int_distribution` is allowed to produce different values on different standard libraries, which would make the result depend on the compiler.

<figure class="vx-figure">
<svg viewBox="0 0 760 290" role="img" aria-label="The fifteen launch times as a dot plot, with the minimum, mean and median marked and the two confidence intervals for the median drawn below" aria-describedby="p1-f1-desc">
<desc id="p1-f1-desc">A horizontal axis of ticks from 90 to 125, a break, then 220 to 290. Fifteen dots sit above the axis: thirteen between 96 and 105, stacked where two launches share a value (98, 99 and 101), and two far to the right, past the break, at 231 and 275. Markers below the axis: the minimum at 96, the median at 101 and the mean at 120.6, which sits in an empty stretch of the axis with no launch near it. Under the markers, two bars: the rank interval from 98 to 105, and the bootstrap interval from 98 to 103. The minimum lies to the left of both bars.</desc>
<line class="vx-line" x1="70" y1="150" x2="510" y2="150"/>
<line class="vx-line" x1="550" y1="150" x2="730" y2="150"/>
<text class="vx-text-muted" x="530" y="155" text-anchor="middle">//</text>
<text class="vx-text-muted" x="80" y="170" text-anchor="middle">90</text>
<text class="vx-text-muted" x="200" y="170" text-anchor="middle">100</text>
<text class="vx-text-muted" x="320" y="170" text-anchor="middle">110</text>
<text class="vx-text-muted" x="440" y="170" text-anchor="middle">120</text>
<text class="vx-text-muted" x="560" y="170" text-anchor="middle">220</text>
<text class="vx-text-muted" x="720" y="170" text-anchor="middle">290</text>
<text class="vx-text-muted" x="400" y="195" text-anchor="middle">ticks (made up), lower is faster</text>
<circle class="vx-dot" cx="152" cy="138" r="5"/>
<circle class="vx-dot" cx="164" cy="138" r="5"/>
<circle class="vx-dot" cx="176" cy="138" r="5"/><circle class="vx-dot" cx="176" cy="126" r="5"/>
<circle class="vx-dot" cx="188" cy="138" r="5"/><circle class="vx-dot" cx="188" cy="126" r="5"/>
<circle class="vx-dot" cx="200" cy="138" r="5"/>
<circle class="vx-dot" cx="212" cy="138" r="5"/><circle class="vx-dot" cx="212" cy="126" r="5"/>
<circle class="vx-dot" cx="224" cy="138" r="5"/>
<circle class="vx-dot" cx="236" cy="138" r="5"/>
<circle class="vx-dot" cx="248" cy="138" r="5"/>
<circle class="vx-dot" cx="260" cy="138" r="5"/>
<circle class="vx-dot" cx="585" cy="138" r="5"/>
<circle class="vx-dot" cx="686" cy="138" r="5"/>
<text class="vx-text-muted" x="636" y="110" text-anchor="middle">two interrupted launches</text>
<line class="vx-line" x1="152" y1="60" x2="152" y2="118" stroke-dasharray="3 3"/>
<text class="vx-text" x="152" y="52" text-anchor="middle">min 96</text>
<line class="vx-line" x1="212" y1="82" x2="212" y2="112"/>
<text class="vx-text-accent" x="226" y="78">median 101</text>
<line class="vx-line" x1="447" y1="60" x2="447" y2="146" stroke-dasharray="3 3"/>
<text class="vx-text" x="447" y="52" text-anchor="middle">mean 120.6</text>
<rect class="vx-box-accent" x="176" y="214" width="84" height="16" rx="3"/>
<text class="vx-text-muted" x="272" y="227">95% interval from ranks: [98, 105]</text>
<rect class="vx-box-accent" x="176" y="244" width="60" height="16" rx="3"/>
<text class="vx-text-muted" x="272" y="257">95% bootstrap interval: [98, 103]</text>
</svg>
<figcaption>Figure 3. The fifteen made-up launches from the example. The minimum, the number a "best of" report would publish, lies outside both intervals for the median. The mean lands in an empty stretch of the axis, where no launch was. The median and its interval describe where the undisturbed launches sit, and how sure fifteen launches let you be.</figcaption>
</figure>

Now a half-finished one to complete in your head. With six launches, the rank formula gives a lower rank of $\lfloor 0.60 \rfloor = 0$, which is not a rank at all; the approximation fails for tiny samples, and the interval has to come from the exact reasoning instead. Each launch lands above or below the true median with probability one half. What is the chance that all six land on the same side, so that even [minimum, maximum] misses the median?

??? check "Finish the six-launch case: does [minimum, maximum] of six launches make a 95% interval for the median? And of five?"

    All six land on one side with probability $2 \times (1/2)^6 = 1/32$, about 3.1%, so [minimum, maximum] covers the median with probability about 96.9%, which is at least 95%. With five launches the chance is $2 \times (1/2)^5 = 1/16$, 6.25%, so the coverage is 93.75%, short of 95%. Six launches is the fewest that can support a 95% interval for a median at all, and then only the widest possible one.

## Comparing two versions

The question behind most measurements is comparative: is B faster than A, and by how much? Kalibera and Jones ask for the answer as an effect size with a confidence interval, "B is faster than A by x%, with this interval", not as a yes or no.[^kj] A natural effect size for time is the **ratio of medians**, $S = \tilde{t}_A / \tilde{t}_B$, where $\tilde{t}$ is a median time; $S > 1$ means B is faster. The bootstrap gives its interval: resample A's launches and B's launches, each on its own, compute $S$ from the pair, repeat.

--8<-- "includes/examples/optimize/p1-measure-first/compare_variants.cpp.md"

Read the three lines in order. The first launch of each says A took 97 ticks and B took 99, so B looks slower. The medians say the opposite: A at 101, B at 94, a ratio of 1.074. The interval, [1.042, 1.108], excludes 1, so on these made-up data B is faster by roughly 4% to 11%. One pair of launches got the direction wrong, because A's first launch happened to be one of its fastest. The ratio interval also answers more than an overlap test would: two separate intervals that overlap do not show that there is no difference, and Georges and his coauthors treat overlap as inconclusive, not as evidence of equality.[^georges]

??? check "A teammate writes: 'Before my change the kernel took 210 ticks, after it 185, a 12% speedup.' What would you ask for before believing the 12%?"

    Several launches of each version, interleaved, and the ratio of the medians with its confidence interval. One reading on each side cannot separate a change in the code from a change in the machine, as the first line of the example shows. If the interval for the ratio excludes 1, there is a difference; its endpoints say how large it plausibly is. And, if the claimed effect is small, the same comparison under a changed setup, to rule out bias.

## From time to rate

Chapters from [P3](p3-roofline.md) on report speed as a rate rather than a time. For matrix multiplication the rate is counted in **FLOP/s**, floating-point operations per second. The loop body of the stage 10 kernel, `c[i][j] += a[i][k] * b[k][j]` in C terms, does one multiplication and one addition, and it runs $M \cdot N \cdot K$ times for an $M \times K$ matrix times a $K \times N$ one. The work is therefore

$$
W = 2MNK \ \text{FLOP}, \qquad \text{GFLOP/s} = \frac{2MNK}{t \ \text{in nanoseconds}}
$$

since one FLOP per nanosecond is $10^9$ FLOP per second. For the stage 10 shape, 64 by 64 by 64, $W = 2 \cdot 64^3 = 524{,}288$ FLOP. The count belongs to the algorithm, not to the machine code: a vectorized or unrolled version does the same arithmetic in fewer instructions, so every version of the kernel uses the same $W$, and their rates can be compared directly.

Convert last. Compute the median time and its interval, then divide $W$ by each. Because the rate falls as the time rises, the endpoints swap: a time interval $[t_\text{lo}, t_\text{hi}]$ becomes the rate interval $[W / t_\text{hi},\ W / t_\text{lo}]$. Averaging rates directly would also go wrong, which is one of Hoefler and Belli's rules below.

??? check "A kernel with W = 524,288 FLOP has a median time of t nanoseconds with a 95% interval [a, b]. Write down its median rate and the rate interval. Which endpoint of the time interval gives the upper end of the rate interval?"

    The median rate is 524,288 / t GFLOP/s, and the interval is [524,288 / b, 524,288 / a]. The upper end of the rate interval comes from the lower end of the time interval, `a`: the fastest plausible time is the highest plausible rate.

## Keeping the compiler from helping too much

A benchmark can also be emptied by the thing this book spends its middle chapters building. The [as-if rule](o1-optimizer-contract.md#the-as-if-rule) lets a compiler do anything that leaves observable behavior unchanged. A timing loop that computes a value and never uses it can be deleted, because doing the work and throwing the answer away looks the same from outside as not doing it. A loop that calls a pure function five times with an input that never changes can be replaced by one call, or by the answer itself, computed during compilation. [O5](o5-constants-and-dead-code.md#dead-code-in-benchmarks) names the two passes responsible: dead-code elimination removes work nobody uses, and folding removes work whose answer is already known.

Both are what you want an optimizer to do to real code and exactly what you do not want it to do to a timing loop, whose whole purpose is to make work happen so the clock has something to measure. If five iterations of work become one, the clock reports the cost of one, and you report it as the cost of five.

The standard defence is an empty inline-assembly statement. The compiler cannot see inside inline assembly, so it must take the statement's declared inputs and outputs at their word. An output operand that is also an input ("the assembly may rewrite this value") means that nothing known about the value before the statement holds after it. An input operand ("the assembly reads this value") means the value must be computed. A `"memory"` clobber ("the assembly may read or write any memory") means pending stores must be done first.

Google Benchmark packages the idea as `DoNotOptimize` and `ClobberMemory`.[^gbench-ug] The IR example below shows what `opt -O2` does with and without it; `"=r,0"` ties an output to the input, and `"r,~{memory}"` reads a value and clobbers memory.[^langref]

--8<-- "includes/examples/optimize/p1-measure-first/folded_benchmark.ll.md"

`@plain` became `ret i64 70225`: five calls of `42 * 42 xor 12345`, which is 14,045, added up during compilation. A clock around it would time a single instruction. `@guarded` still does five multiplications and five `xor`s, each between the two assembly statements, because the input of each call is a value the compiler could not predict.

<figure class="vx-figure">
<svg viewBox="0 0 760 250" role="img" aria-label="Three timelines of a timed region: what the source asks for, what the plain loop became at -O2, and what the guarded loop became" aria-describedby="p1-f4-desc">
<desc id="p1-f4-desc">Three rows, each a timeline from a start mark to a stop mark. Top row, the source: five boxes labelled work, one per repetition. Middle row, the plain loop after opt -O2: the start and stop marks with nothing between them except one small box labelled ret 70225, computed during compilation. Bottom row, the guarded loop after opt -O2: five groups, each an assembly barrier, a multiply and xor box, and a second barrier. A note on the middle row says the clock measures almost nothing, and on the bottom row that the work is kept.</desc>
<text class="vx-text" x="20" y="30">What the source asks for</text>
<line class="vx-line" x1="200" y1="20" x2="200" y2="52"/><line class="vx-line" x1="700" y1="20" x2="700" y2="52"/>
<text class="vx-text-muted" x="200" y="66" text-anchor="middle">start</text><text class="vx-text-muted" x="700" y="66" text-anchor="middle">stop</text>
<rect class="vx-box" x="215" y="24" width="84" height="24" rx="3"/><text class="vx-mono" x="257" y="41" text-anchor="middle">work</text>
<rect class="vx-box" x="309" y="24" width="84" height="24" rx="3"/><text class="vx-mono" x="351" y="41" text-anchor="middle">work</text>
<rect class="vx-box" x="403" y="24" width="84" height="24" rx="3"/><text class="vx-mono" x="445" y="41" text-anchor="middle">work</text>
<rect class="vx-box" x="497" y="24" width="84" height="24" rx="3"/><text class="vx-mono" x="539" y="41" text-anchor="middle">work</text>
<rect class="vx-box" x="591" y="24" width="84" height="24" rx="3"/><text class="vx-mono" x="633" y="41" text-anchor="middle">work</text>
<text class="vx-text" x="20" y="110">@plain after -O2</text>
<line class="vx-line" x1="200" y1="100" x2="200" y2="132"/><line class="vx-line" x1="700" y1="100" x2="700" y2="132"/>
<rect class="vx-box-bad" x="215" y="104" width="110" height="24" rx="3"/><text class="vx-mono" x="270" y="121" text-anchor="middle">ret 70225</text>
<text class="vx-text-muted" x="340" y="121">the clock measures almost nothing</text>
<text class="vx-text" x="20" y="190">@guarded after -O2</text>
<line class="vx-line" x1="200" y1="180" x2="200" y2="212"/><line class="vx-line" x1="700" y1="180" x2="700" y2="212"/>
<g>
<rect class="vx-box-strong" x="212" y="184" width="8" height="24"/><rect class="vx-box-accent" x="222" y="184" width="70" height="24" rx="3"/><rect class="vx-box-strong" x="294" y="184" width="8" height="24"/>
<rect class="vx-box-strong" x="310" y="184" width="8" height="24"/><rect class="vx-box-accent" x="320" y="184" width="70" height="24" rx="3"/><rect class="vx-box-strong" x="392" y="184" width="8" height="24"/>
<rect class="vx-box-strong" x="408" y="184" width="8" height="24"/><rect class="vx-box-accent" x="418" y="184" width="70" height="24" rx="3"/><rect class="vx-box-strong" x="490" y="184" width="8" height="24"/>
<rect class="vx-box-strong" x="506" y="184" width="8" height="24"/><rect class="vx-box-accent" x="516" y="184" width="70" height="24" rx="3"/><rect class="vx-box-strong" x="588" y="184" width="8" height="24"/>
<rect class="vx-box-strong" x="604" y="184" width="8" height="24"/><rect class="vx-box-accent" x="614" y="184" width="70" height="24" rx="3"/><rect class="vx-box-strong" x="686" y="184" width="8" height="24"/>
</g>
<text class="vx-mono" x="257" y="201" text-anchor="middle">mul xor</text>
<text class="vx-text-muted" x="200" y="236" text-anchor="middle">start</text><text class="vx-text-muted" x="700" y="236" text-anchor="middle">stop</text>
<text class="vx-text-muted" x="450" y="236" text-anchor="middle">narrow bars: the empty assembly statements</text>
</svg>
<figcaption>Figure 4. The timed region of the IR example, before and after <code>opt -O2</code>. The plain loop is folded to its answer during compilation, so a clock around it times nothing the source asked for. The guarded loop keeps one multiply and one <code>xor</code> per repetition between its barriers.</figcaption>
</figure>

The barriers keep the calls; they do not protect the work inside them. Google Benchmark's guide says so directly: `DoNotOptimize` does not prevent optimizations on the expression itself, which may even be removed when its result is already known.[^gbench-ug] The C++ example shows the consequence. Its `work` runs eight steps of `x = x * 1103515245 + 12345`, and the loop around it uses the same two barriers.

--8<-- "includes/examples/optimize/p1-measure-first/escape_and_clobber.cpp.md"

Compiled by Apple clang 21 at `-O2` for the M4 Pro (checked on 2026-09-24), `main` keeps five calls, one per repetition, and each is a single `madd` (multiply-add) instruction. The compiler noticed that eight steps of `x * a + c` compose into one step with different constants, worked those constants out during compilation, and replaced the eight steps with one. The printed total is the same either way, so a benchmark of this loop would report one multiply-add per call as eight steps of work. Read the assembly in Compiler Explorer: the printed output never tells you how much work the machine did.

??? check "Delete the two barrier calls from the C++ example, keeping everything else. What can the compiler now do to the loop, and why does the printed total not warn you?"

    `work` is pure and its input is the constant 42, so the compiler can compute `work(42)` and the whole total during compilation and print a constant, the same way `@plain` became `ret i64 70225`. The as-if rule allows it, because the output is identical. That is the problem: the only evidence of how much work ran is in the generated code, not in what the program prints.

### A Vortex program has no input

[O5](o5-constants-and-dead-code.md#dead-code-in-benchmarks) left a question for this chapter. A v0.1 Vortex program reads no input: `main` takes no parameters, and `print` is the only built-in function ([Conformance 1.2](../specification/conformance.md#12-programs), [Programs and declarations 3.9](../specification/declarations.md#39-built-in-functions)). Every value a v0.1 benchmark computes is fixed before it runs, and there is no inline assembly to hide a value behind. An optimizer allowed by the as-if rule could, in principle, compute the whole multiplication during compilation and print the stored answer.

Today this is harmless, because a v0.1 compiler does not optimize. It matters from the first optimizing pass on, and it has consequences for how Vortex code is timed:

- Time the whole program from outside, as a process. The harness starts the compiled program, reads a monotonic clock around it, and checks what it printed. The launch becomes the natural unit of repetition, and nothing inside the program needs to read a clock.
- Make the printed output depend on all of the work, such as a checksum over every element of the result, so that dead-code elimination cannot remove part of it. This does not stop folding.
- Check that the work happened. Time grows with the work: for the naive kernel, doubling the size multiplies the arithmetic by eight, and a time that barely moves says the arithmetic did not run. Reading the generated code is the direct check.
- Measure what a launch costs with no kernel at all (a program that prints one line), and size the kernel so that this cost is a small fraction of the kernel's time. Hoefler and Belli's 5% margin for timer overhead is a reasonable target for it too.

How a future Vortex program might receive data the compiler cannot see, from a file or the command line, is a language design question, not a measurement one, and it is not settled here.

## The reporting rules

Hoefler and Belli collected twelve rules for reporting performance results, written from a survey of papers in high-performance computing.[^hb] Several of them apply to every chapter of this book:

- **Say what the base case is, and how fast it is** (Rule 1, stated for parallel speedup). A speedup means little until the reader knows what it was measured against, so report the base case's own performance, not only the ratio.
- **Summarize costs with the arithmetic mean, rates with the harmonic mean** (Rule 3), and **avoid summarizing ratios**: summarize the costs or rates they come from instead, and fall back to the geometric mean of ratios only when those are not available (Rule 4). "1.3 times faster" alone throws away both numbers it came from.
- **Say whether the values are deterministic; for data that varies, report confidence intervals** (Rule 5), and **do not assume normality without checking** (Rule 6).
- **Document every factor that varied and the complete setup**: software, hardware and techniques (Rule 9).
- **Show upper bounds where you can** (Rule 11): how far a result is from what the machine allows. That is the roofline of [P3](p3-roofline.md).

The other half of reporting is recording the machine. Hardware facts belong in a query, not in a comment, because the next machine will have different ones. On macOS, `sysctl -n machdep.cpu.brand_string` names the processor (an Apple M4 Pro on the machine these chapters were written on, queried 2026-09-23), `sysctl hw.perflevel0.physicalcpu hw.perflevel1.physicalcpu` gives the counts of performance and efficiency cores, and `sw_vers` gives the system version; on Linux, `lscpu` and `/proc/cpuinfo` give the same facts.

The LLVM notes and Google Benchmark's variance guide also list controls for a Linux benchmark machine: disable frequency scaling and Turbo Boost, disable address-space layout randomization, reserve cores for the benchmark (and disable the other hardware thread on each), link statically, and keep inputs and outputs off real storage.[^llvm-bench][^gbench-var]

**Address-space layout randomization (ASLR)** is a security feature that places the stack, heap and libraries at different addresses on each launch. Turning it off lowers noise by fixing one layout, and one fixed layout is exactly the bias this chapter began with, so a small effect still needs the setup check.

On macOS none of these switches is documented for this purpose. Apple's advice is to set a thread's QoS class instead, for example with `pthread_set_qos_class_self_np`, rather than managing thread priorities by hand.[^apple-tuning] A harness on a Mac therefore sets the QoS class of its threads, never times work at a background class, and records the class with the result.

??? check "Why is 'we ran it 100 times and B was always faster' weaker than a ratio of medians with a 95% interval from the same runs?"

    "Always faster" says only that B won each pairing; it says nothing about by how much, and it does not say how the 100 runs were grouped: 100 iterations in one launch share one layout and one warm cache, and so may be one sample repeated. The ratio with its interval gives a size and an uncertainty, computed across launches, that can be put in a table and checked against a threshold. Neither protects against bias; both need the setup recorded.

## For Vortex

!!! vortex "Exercise"

    **Build `vortex-bench`**, a small harness, outside the compiler, that times programs your compiler produced and reports numbers you can defend. Write it in any language you like; it is a tool, not part of Vortex.

    1. **A correctness gate that runs first.** Run the program, compare its standard output and exit status with a golden file (for the stage 10 program, the known answer it already prints), and refuse to report any timing when they differ. A fast wrong answer is not a result.
    2. **Launches as the unit.** Start the program as a separate process `n` times (at least 6, as the check question above showed), with a monotonic clock around each launch. Record every raw time, and whether you discarded any launches as warm-up and why.
    3. **A summary per program:** the minimum, the median, and a 95% interval for the median from ranks, with `n`.
    4. **A comparison mode** for two programs: interleaved launches, the ratio of medians, and a bootstrap interval with a recorded seed and resample count.
    5. **A baseline launch:** the same measurement for a program that only prints one line, reported beside the kernel's, so that a reader can see how much of the time is process start-up.
    6. **JSON output** with the raw times, the statistics and how they were computed, the machine facts queried at run time (processor, core counts, system version), the QoS class used, your compiler's commit and flags, and the date.

    **Not yet:** layout randomization in the style of Stabilizer, hardware counters ([P4](p4-counters-and-tools.md)), threads ([P13](p13-multithreading.md)), GPU timing ([G14](../gpu/g14-measuring-gpu-code.md)), automatic outlier removal, and any change to the Vortex language to let programs read data.

    **Proof that it works:**

    - Break the kernel on purpose (change one loop bound) and confirm that `vortex-bench` reports a failure and no time.
    - Run the unmodified program twice as two separate sessions and compare them in comparison mode: the interval for the ratio should contain 1. If it often does not, your launches are not independent, or the machine is drifting.
    - Plant a slowdown: a copy of the program that runs its multiplication twice (with the second result folded into the checksum). The ratio interval should exclude 1 and sit near 2. A harness that cannot find a planted slowdown will not find a real one.
    - Recompute every statistic from the raw times in the JSON, by hand or with a separate script, and get the same numbers.
    - Fill in this table for the stage 10 program, with the machine and the date:

    | Program | Launches | Min (ns) | Median (ns) | 95% interval (ranks) | GFLOP/s at the median |
    | --- | --- | --- | --- | --- | --- |
    | prints one line (baseline) | | | | | (none) |
    | stage 10, 64 by 64 | | | | | |
    | stage 10, planted slowdown | | | | | |

## Key ideas

!!! recap "Questions you can now answer"

    - **Why is one timed run not a measurement?** It is one sample from a noisy process; scheduling, clock speed and core assignment all move it, and one sample shows none of the spread.
    - **How does measurement bias differ from noise?** Bias is a shift caused by the setup, such as environment size or link order, that is the same on every repetition; repeating cannot reveal it, only varying the setup can.
    - **At which level should you repeat?** At the level where the variation lives: separate launches, not only iterations inside one, with warm-up left out and variants interleaved.
    - **Why report a median with an interval rather than the best run?** The best run is the least typical one and can fall outside the plausible range of the median; the interval says how sure the data let you be.
    - **How do you compare two versions?** With an effect size, such as the ratio of medians, and its confidence interval; one pair of runs can get even the direction wrong.
    - **What does `DoNotOptimize` protect, and what not?** It keeps a value computed and treated as unknown; it does not stop the compiler from simplifying the work that produces the value.
    - **How do you turn a time into GFLOP/s?** Divide the algorithm's work, $2MNK$ for matrix multiplication, by the time in nanoseconds, and convert the interval's endpoints, which swap.

## Where this comes back

!!! next "You will use this again in"

    - [P3. The roofline model](p3-roofline.md): *measured GFLOP/s*, *upper bounds*
    - [P4. Seeing inside the CPU: counters and tools](p4-counters-and-tools.md): *repeated measurements*, *recording the machine*
    - [P8. Cache blocking](p8-cache-blocking.md): *measuring a sweep of tile sizes*
    - [P10. Vectorization](p10-vectorization.md): *keeping a benchmark's work alive*
    - [P13. Multithreading](p13-multithreading.md): *QoS class*, *which cores run the work*
    - [P16. Capstone: the ladder, measured](p16-capstone.md): *vortex-bench*, *median and interval*, *GFLOP/s*
    - [O12. Testing an optimizer](o12-testing-optimizers.md): *the correctness gate*
    - [G14. Measuring GPU code](../gpu/g14-measuring-gpu-code.md): *a stopwatch is not a measurement*, *median over launches*

## Sources and further reading

Start with Mytkowicz and his coauthors: sections 1, 2 and 4 are short and will change how you read every speedup. Then Hoefler and Belli's twelve rules, which fit on one page, and section 3.1.3 of their paper for the interval of the median. Georges and his coauthors are the clearest account of why a best-of-N report misleads, and Curtsinger and Berger show what it takes to sample layouts on purpose.

[^mytk]: Todd Mytkowicz, Amer Diwan, Matthias Hauswirth and Peter F. Sweeney, "Producing Wrong Data Without Doing Anything Obviously Wrong!", *Proceedings of the 14th International Conference on Architectural Support for Programming Languages and Operating Systems (ASPLOS)*, 2009: the abstract, sections 1 to 3 and section 4.1. <https://doi.org/10.1145/1508244.1508275>
[^stab]: Charlie Curtsinger and Emery D. Berger, "Stabilizer: Statistically Sound Performance Evaluation", *ASPLOS*, 2013: the abstract and section 1. <https://doi.org/10.1145/2451116.2451141>
[^georges]: Andy Georges, Dries Buytaert and Lieven Eeckhout, "Statistically Rigorous Java Performance Evaluation", *OOPSLA*, 2007: section 1 and figure 1. <https://doi.org/10.1145/1297027.1297033>
[^kj]: Tomas Kalibera and Richard Jones, "Rigorous Benchmarking in Reasonable Time", *International Symposium on Memory Management (ISMM)*, 2013. <https://doi.org/10.1145/2464157.2464160>
[^hb]: Torsten Hoefler and Roberto Belli, "Scientific Benchmarking of Parallel Computing Systems: Twelve Ways to Tell the Masses when Reporting Performance Results", *SC*, 2015: rules 1 to 12, section 3.1.3 and section 4.2. <https://doi.org/10.1145/2807591.2807644>
[^llvm-bench]: LLVM Project, "Benchmarking tips", sections "Introduction", "General" and "Linux". <https://llvm.org/docs/Benchmarking.html>
[^gbench-ug]: google/benchmark, "User Guide", sections "Preventing Optimization" and the command-line flags for random interleaving and minimum warm-up time. <https://github.com/google/benchmark/blob/main/docs/user_guide.md>
[^gbench-var]: google/benchmark, "Reducing Variance", sections "Disabling CPU Frequency Scaling" and "Disabling ASLR". <https://github.com/google/benchmark/blob/main/docs/reducing_variance.md>
[^apple-tuning]: Apple, "Tuning your code's performance for Apple silicon", Apple Developer Documentation, section "Assign Quality-of-Service (QoS) Classes to Work". <https://developer.apple.com/documentation/apple-silicon/tuning-your-code-s-performance-for-apple-silicon>
[^cppref-steady]: cppreference.com, "std::chrono::steady_clock". <https://en.cppreference.com/w/cpp/chrono/steady_clock>
[^langref]: LLVM Project, "LLVM Language Reference Manual", section "Inline Assembler Expressions". <https://llvm.org/docs/LangRef.html>

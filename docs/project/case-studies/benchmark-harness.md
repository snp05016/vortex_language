# A2. Measurement methodology and performance CI

<p class="page-intro">This case study asks whether a Vortex performance number can be trusted: whether the benchmark harness separates real changes from noise, and whether the performance check in CI catches a planted slowdown without raising false alarms. Every other performance claim on this site depends on the answer.</p>

<p class="vx-meta">Status: Not started · Planning size: S to M</p>

## The question

Does the Vortex benchmark harness produce numbers that repeat within a known,
published spread, and does its regression check flag every planted slowdown
larger than that spread while staying quiet on unchanged code?

Two terms carry this page. The **noise floor** of a benchmark is how much its
result moves between runs when nothing has changed. A **performance
regression** is a change to the code that makes a benchmark slower. A
regression check is only useful if it can tell the two apart.

When the work is done, this section will hold one sentence of this form:

> On ___, the harness measures each benchmark with a run-to-run spread of at
> most ___%; the regression check flagged ___ of ___ planted slowdowns of
> ___% or more, and raised ___ false alarms in ___ runs on unchanged code.

## Why employers care

Profiling and benchmarking is among the skills compiler and kernel teams ask
for most often, alongside compiler fundamentals, C++ and LLVM. Teams that ship
compilers keep infrastructure that watches for performance regressions, and
they need engineers who can tell a real regression from noise before anyone
spends a week chasing it. A published method with measured noise is what
turns a timing into evidence.

The research literature shows how careful people get this wrong. Mytkowicz
and colleagues surveyed 133 papers from four major systems and compiler
conferences and found that none of those with experimental results
adequately considered **measurement bias**, an error that comes from the
experimental setup rather than from the thing being measured.[^mytk] A
harness that controls for it is more convincing than any single fast number.

## What to build

A benchmark harness and a CI job around it. The parts, and the chapters that
teach them:

- **A correctness gate before any timing.** The harness refuses to time a
  kernel whose output is wrong. See [How Vortex is tested](../testing.md).
- **Repetition at several levels.** Separate process invocations, and
  iterations inside each invocation. Kalibera and Jones show how to choose
  the number of repetitions at each level from where the variation arises:
  between builds, between runs or between iterations.[^kj]
- **Sound statistics.** A median with a confidence interval, never the best of
  N runs. Georges and colleagues show that performance comparisons without
  statistically rigorous analysis can be misleading and can even reach the
  wrong conclusion.[^georges]
- **An environment record** for every run: chip, core types, operating
  system, compiler versions, flags, Vortex commit, quality-of-service class,
  power source and thermal state.
- **Raw output.** Every sample written to a file in the repository, not only
  the summary.
- **Protection against deleted work.** Once Vortex removes dead code, a kernel
  whose result is never used can disappear entirely. The harness consumes
  every result, for the same reason google/benchmark provides
  `DoNotOptimize`: to stop the compiler optimizing a value away.[^gbench]
- **A regression check** that compares a commit with its baseline using the
  confidence intervals, with a threshold derived from the measured noise
  floor.
- **A layout perturbation mode** that reruns a benchmark under different
  environment sizes and link orders, the two perturbations Mytkowicz and
  colleagues used,[^mytk] to show whether a conclusion survives them.
  Curtsinger and Berger's STABILIZER takes the idea further by randomizing
  code, stack and heap layout while the program runs.[^stab]
- **A CI job** that always runs the correctness gate, and runs performance
  checks only where the hardware is controlled (see the method below).

Chapters: [P1](../../optimize/p1-measure-first.md) teaches the measurement
protocol, P4 (in the [optimization book](../../optimize/index.md)) teaches
hardware counters and profilers, and [P16](../../optimize/p16-capstone.md)
applies all of it to the matrix multiplication ladder.
[G14](../../gpu/g14-measuring-gpu-code.md) extends the method to GPUs.

This study leaves out:

- GPU timing, which G14 and [A8](gpu-matmul-ladder.md) cover;
- profiling and counter analysis beyond what the environment record needs,
  which belongs to each study's analysis section;
- a dashboard. A table generated from the raw data is enough.

## Method

The protocol itself is on [How Vortex performance is measured](../measuring.md).
This page tests whether that protocol works.

### What is compared with what

1. **A/A runs.** The same binary against itself, many times, in separate
   invocations. This measures the noise floor of every benchmark. The name
   comes from A/B testing: here both sides are A.
2. **Planted regressions.** A commit that slows a kernel by a known
   mechanism, such as disabling one pass from [A1](cpu-matmul-ladder.md),
   against its parent. Some planted changes are smaller than the noise floor
   and some larger, and a planted speedup is included too.
3. **Clean commits.** Commits that do not touch any code a benchmark runs,
   such as documentation changes, against their parents. Any flag raised here
   is a **false alarm**.
4. **Layout perturbations.** The same binary under different environment
   sizes and link orders.
5. **Dedicated machine against hosted runner.** The same benchmarks on the
   owner's machine and on a GitHub-hosted runner, compared by their spread.

### Machines

The owner's Apple M4 Pro, with no other work running, on mains power, at a
fixed quality-of-service class, takes every number that appears elsewhere on
the site. A GitHub-hosted runner takes part only in comparison 5. GitHub
states that each of its hosted runners, apart from single-CPU runners, is a
new virtual machine,[^gh-runners] so the hardware and its neighbours can
change from one job to the next. That is the reason to gate correctness in
hosted CI and to take performance numbers on a machine you control.

LLVM's benchmarking tips list ways to reduce noise, such as disabling
frequency scaling, turbo boost and address-space randomization and reserving
cores for the benchmark, and give commands for Linux only.[^llvm-bench] The
same page warns that low noise does not rule out measurement bias. The
measuring page records which of those steps have a macOS counterpart.

### What counts as success

1. The noise floor of every benchmark is measured and published.
2. The regression threshold is derived from the noise floor by a rule written
   down in advance, not tuned by hand after seeing the results.
3. Every planted regression larger than the threshold is flagged.
4. No false alarm is raised in a number of clean runs fixed in advance.
5. The harness's reports meet each of Hoefler and Belli's twelve rules, or say
   why a rule does not apply.[^hb]
6. Every performance number on the site can be traced to raw data, an
   environment record and a commit.

## Setup

| Field | Value |
| --- | --- |
| Dedicated machine and chip |  |
| Hosted runner label and image |  |
| Operating system on each |  |
| Vortex commit |  |
| Harness commit |  |
| C++ compiler and flags for the harness |  |
| Timer used, and its resolution |  |
| Quality-of-service class |  |
| Power source and thermal state |  |
| Date of the runs |  |

## Results

### Noise floor

| Benchmark | Machine | Invocations | Iterations per invocation | Median | 95% CI | Relative spread | Raw data |
| --- | --- | --- | --- | --- | --- | --- | --- |
|  |  |  |  |  |  |  |  |

Invocations
: Separate process runs of the benchmark.

Iterations per invocation
: Timed repetitions inside one process run, after the warm-up iterations,
  which are not counted.

Median
: The median time of one iteration across all invocations, with its unit.

Relative spread
: The width of the 95% confidence interval divided by the median, as a
  percentage.

### Planted changes

| Planted change | How it was made | Intended effect | Runs | Measured change | 95% CI of the change | Flagged? | Commit |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Slowdown smaller than the noise floor |  | Should not be flagged |  |  |  |  |  |
| Slowdown near the threshold |  | Either outcome, reported |  |  |  |  |  |
| Slowdown well above the threshold |  | Must be flagged |  |  |  |  |  |
| Speedup well above the threshold |  | Must be reported as a change |  |  |  |  |  |

Measured change
: The difference between the new median and the baseline median, as a
  percentage of the baseline median, with the baseline's absolute value in
  the notes.

### False alarms

| Period | Clean commits checked | Runs | False alarms | Threshold rule in force |
| --- | --- | --- | --- | --- |
|  |  |  |  |  |

### Layout sensitivity

| Benchmark | Perturbation | Levels tried | Range of medians | Did a conclusion change? |
| --- | --- | --- | --- | --- |
|  | Environment size |  |  |  |
|  | Link order |  |  |  |

Range of medians
: The smallest and largest median across the perturbation levels, and their
  difference as a percentage of the smallest.

### Dedicated machine and hosted runner

| Benchmark | Machine | Relative spread | Usable as a gate? |
| --- | --- | --- | --- |
|  | Owner's M4 Pro |  |  |
|  | GitHub-hosted runner |  |  |

### Reporting checklist

Hoefler and Belli's twelve rules for reporting performance, in short, with
how the harness meets each one.[^hb]

| Rule | In short | How the harness meets it | Evidence |
| --- | --- | --- | --- |
| 1 | A speedup names its base case and gives the base's absolute performance. |  |  |
| 2 | If only some benchmarks are reported, say why. |  |  |
| 3 | Use the arithmetic mean only for costs such as time; use the harmonic mean for rates. |  |  |
| 4 | Summarize the costs or rates, not their ratios. |  |  |
| 5 | Say whether results are deterministic; give confidence intervals when they are not. |  |  |
| 6 | Do not assume the data is normally distributed without checking. |  |  |
| 7 | Compare noisy results with a sound test, such as non-overlapping confidence intervals. |  |  |
| 8 | Check that the mean or the median is the right summary; some questions need other percentiles. |  |  |
| 9 | Document every factor that varied and the complete setup. |  |  |
| 10 | For parallel runs, say how times were measured, synchronized and summarized. |  |  |
| 11 | Show upper performance bounds where possible. |  |  |
| 12 | Plot enough to interpret the results, and join points with lines only where interpolation is valid. |  |  |

## Analysis

Empty until the first measured run. This section will explain where the
noise comes from on each machine, why any planted change was missed, and what
changed in the harness as a result.

## What did not work

Empty until work begins.

## Threats to validity

**The timer.** Reading a timer costs time. Hoefler and Belli suggest keeping
that overhead under 5% of the interval being measured;[^hb] short
kernels need many iterations per timing.

**Heat and frequency.** Long runs warm the chip, and the operating system
changes frequencies and moves threads between cores. The environment record
and the spread over time show whether a result drifted during a run.

**Background work.** The operating system schedules its own work, such as
indexing and backups, at times the harness does not choose. A/A runs spread
over a day show how much this matters.

**One threshold for everything.** Benchmarks differ in their noise. A single
threshold is too strict for noisy ones and too loose for quiet ones, so the
threshold rule works per benchmark.

**Many comparisons at once.** With enough benchmarks, some will cross any
threshold by chance on a clean commit. The check confirms a flag with a
second, independent run before reporting it.

**Updates.** Operating system and compiler updates change results. The
environment record makes such a change visible instead of mysterious.

**A hosted runner's hardware.** Consecutive jobs may land on different
machines, so a hosted runner's spread describes a pool of machines, not one.

## Reproduce

Empty until the first measured run. This section will give one command that
runs the A/A, planted-change and layout experiments from a clean checkout and
regenerates every table on this page from the raw data.

## What a reviewer should look at

| Evidence | Where it will be | Available |
| --- | --- | --- |
| The commits that add the harness and the regression check |  | Not yet |
| The CI workflow file and its recent runs |  | Not yet |
| Raw samples and environment records behind every table |  | Not yet |
| The planted-regression commits and the CI runs that checked them |  | Not yet |
| The written threshold rule, dated before the first planted change |  | Not yet |
| The [measuring page](../measuring.md), checked against this page's results |  | Not yet |

## Sources

[^mytk]: Todd Mytkowicz, Amer Diwan, Matthias Hauswirth and Peter F. Sweeney, "Producing Wrong Data Without Doing Anything Obviously Wrong!", *ASPLOS 2009*. <https://doi.org/10.1145/1508244.1508275>
[^kj]: Tomas Kalibera and Richard Jones, "Rigorous Benchmarking in Reasonable Time", *ISMM 2013*. <https://doi.org/10.1145/2464157.2464160>
[^georges]: Andy Georges, Dries Buytaert and Lieven Eeckhout, "Statistically Rigorous Java Performance Evaluation", *OOPSLA 2007*. <https://doi.org/10.1145/1297027.1297033>
[^gbench]: google/benchmark, "User Guide", section on preventing optimization. <https://github.com/google/benchmark/blob/main/docs/user_guide.md>
[^stab]: Charlie Curtsinger and Emery D. Berger, "STABILIZER: Statistically Sound Performance Evaluation", *ASPLOS 2013*. <https://doi.org/10.1145/2451116.2451141>
[^gh-runners]: GitHub, "GitHub-hosted runners reference", GitHub Docs. <https://docs.github.com/en/actions/reference/runners/github-hosted-runners>
[^llvm-bench]: LLVM Project, "Benchmarking tips". <https://llvm.org/docs/Benchmarking.html>
[^hb]: Torsten Hoefler and Roberto Belli, "Scientific Benchmarking of Parallel Computing Systems: Twelve Ways to Tell the Masses when Reporting Performance Results", *SC '15*, 2015. <https://htor.inf.ethz.ch/publications/img/hoefler-scientific-benchmarking.pdf>

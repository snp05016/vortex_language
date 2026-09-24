# How Vortex performance is measured

<p class="page-intro">Every performance number in these docs must come from the protocol on this page. It fixes what to record about the machine, what to check before timing anything, how often to repeat, which statistics to report, and how raw data and charts are kept, so that anyone can check a number or reproduce it.</p>

!!! note "Status"

    No Vortex performance number has been measured yet, and the benchmark
    harness this page refers to has not been built. Building it is the
    [measurement methodology case study](case-studies/benchmark-harness.md),
    which has not started. Until it is done, this page is the specification
    that the harness, and every case study that reports a time, has to meet.

The Vortex philosophy lists, among the compiler's duties, "never presenting
an unverified performance estimate as a measured result"
([programmer and compiler responsibilities](../philosophy.md#programmer-and-compiler-responsibilities)).
The documentation holds itself to the same rule. A timing is the outcome of an
experiment, and experiments on modern computers fail in ways that are well
documented and hard to see from inside a single run.

This page is the one method the [case studies](case-studies/index.md) share.
A case study says that it was measured as described here, then lists only
what it did differently, and why. The ideas behind the method belong to the
books: [P1](../optimize/p1-measure-first.md) is the chapter on the statistics
and the traps, [P3](../optimize/p3-roofline.md) on the roofline, and
[G14](../gpu/g14-measuring-gpu-code.md) on what changes on a GPU. This page is
the reference those chapters point back to, written for someone who has to
decide whether to believe a result.

## The protocol at a glance

1. [Record the environment](#record-the-environment) before the first timed
   run and again after the last.
2. Build every variant from a recorded commit with recorded flags.
3. [Pass the correctness gate](#correctness-before-speed): bit for bit where
   the matmul ladder promises identical results, and within a stated tolerance
   where a variant has permission to round differently.
4. [Warm up](#warm-up). Keep the warm-up samples in the raw data and leave
   them out of the statistics.
5. [Repeat at the level where the variation lives](#repetitions-how-many-and-at-which-level):
   many separate process launches, several iterations in each, with the
   variants interleaved.
6. [Report the median, the spread and a confidence interval](#which-statistic-to-report).
   [Compare two variants](#comparing-two-variants) through an interval for
   their ratio.
7. [Commit the raw samples and the scripts](#storing-results), and draw every
   table and chart from them.
8. [Show the result against an upper bound](#the-roofline-as-context): the
   roofline.

The [reporting checklist](#reporting-checklist) at the end is the same
protocol in the form a reviewer can tick through.

## Why one run is not a result

Run the same program twice and the two times differ. That run-to-run
variation is **noise**: the scheduler, interrupts, other programs and the
state of the caches change a little between runs. Noise is visible, because
repeating the run shows it, and statistics can measure it.

The harder problem is **measurement bias**, a push in one direction that comes
from the experimental setup and is the same on every repetition. Repeating a
biased experiment produces a tight cluster of wrong answers. Mytkowicz, Diwan,
Hauswirth and Sweeney showed how ordinary it is.[^mytk] Two details that look
irrelevant changed their results: the total size of the UNIX environment
variables, which moves the start of the call stack and with it the alignment
of local variables, and the order in which object files are given to the
linker, which moves code and data. For a tiny loop compiled without
optimization on a Core 2 machine, changing only the size of an unused
environment variable changed the run time "frequently by about 33% and once
by almost 300%". They found bias on every processor and with both compilers
they tried. In their survey of 133 papers from ASPLOS, PACT, PLDI and CGO,
none of the papers with experimental results considered it adequately.

The same paper describes two remedies. **Setup randomization** runs the
experiment in many different setups and summarizes the whole distribution, so
that no single lucky layout decides the answer. **Causal analysis** tests a
conclusion by changing its suspected cause on purpose, with as little else
changed as possible, and checking that the effect moves as the conclusion
predicts. The paper also lists other sources of bias, among them room
temperature, which affects the processor's clock speed.

Curtsinger and Berger state the layout problem in one sentence: a single
binary is one sample from the space of possible memory layouts, however many
times it runs.[^stab] Their tool, Stabilizer, randomizes the placement of
functions, stack frames and heap objects while the program runs, and keeps
re-randomizing the code and stack placement, so that one experiment samples
many layouts. With it they found that, on SPEC CPU2006, the effect of LLVM's
`-O3` over `-O2` could not be told apart from random noise.

Bias is not always large. Kalibera and Jones repeated the link-order
experiments. They confirmed the effect with SPEC CPU's small training inputs,
but with the full reference inputs the variation was small.[^kj] The
lesson is not that layout always dominates. Its size depends on the program,
the input and the machine, so a result that claims a small effect has to show
that the effect survives a change of setup.

Summaries go wrong as well. Georges, Buytaert and Eeckhout compared common
ways of reporting Java benchmark results, such as the best of 30 runs, with a
mean and a 95% confidence interval.[^georges] In their example the best-of
method called two garbage collectors equal when the intervals showed a
significant difference, and it declared a clear winner between two others
whose intervals overlapped: that collector had one unusually good run among
many ordinary ones. Kalibera and Jones surveyed papers from 2011 and found
that, of the 90 that measured execution time, 71 gave no measure of variation
at all.[^kj]

The LLVM project's benchmarking notes state the practical conclusion: low
noise is required but not sufficient, because it does not exclude measurement
bias.[^llvm-bench] For Vortex this becomes one rule. A single number never
appears on a page as a result. A result is a set of repeated measurements, a
summary of them with its uncertainty, and a record of the setup that produced
them.

## Record the environment

A reader who cannot tell which machine and which build produced a number
cannot check it. Hoefler and Belli make this one of their twelve rules for
reporting performance: document every factor that varied, and the complete
setup of software, hardware and techniques.[^hb] The harness writes the
**environment record**, a machine-readable description of the setup, once
before the first timed run and once after the last. A change during the
session, such as an unplugged power adapter or a thermal warning, then shows
up as a difference between the two records.

| Item | Why it matters | How to read it on macOS |
| --- | --- | --- |
| Processor model; cores of each type | Performance and efficiency cores run at different speeds | `sysctl -n machdep.cpu.brand_string`; `sysctl hw.perflevel0.physicalcpu hw.perflevel1.physicalcpu` |
| Cache sizes, cache line, page size | The roofline, the tile sizes and the size sweep depend on them | `sysctl hw.perflevel0.l1dcachesize hw.perflevel0.l2cachesize hw.cachelinesize hw.pagesize` |
| Memory size | Inputs must fit without paging | `sysctl hw.memsize` |
| Operating system version | Scheduling and power management change between releases | `sw_vers`; `uname -r` |
| Vortex commit, and whether the tree was clean | Names the compiler that generated the code | `git rev-parse HEAD`; `git status --porcelain` |
| Harness and analysis commits | Name the method | the same commands, for the scripts |
| Versions of every other compiler and tool | Comparators and the reference build depend on them | `clang --version`; `opt --version` |
| Complete build command lines | Flags change the code, and some change the results | the build log |
| Shapes, element type, input seed | Define the work being timed | the harness arguments |
| Thread count and QoS class | Decide which cores run the work | set by the harness and recorded |
| Power source, energy mode, adapter | Change the sustained clock speed | `pmset -g batt` (power source); `pmset -g adapter` (adapter); System Settings, Battery, Energy Mode |
| Thermal state, before and after | Heat lowers the clock speed | `pmset -g therm` |
| Background load, before and after | Other work competes for cores and memory | `uptime`; `top -l 1` |
| Date and time of each session | Places the session in a sequence | the harness clock |

On Linux, `lscpu`, `uname -a` and `/proc/cpuinfo` give the same facts. The
Google Benchmark notes use `cpupower frequency-info` to check the
**frequency governor**, the Linux policy that sets the clock
speed.[^gbench-var]

### Apple silicon laptops

Vortex is developed on an Apple silicon laptop with an M4 Pro, and a laptop is
a difficult place to time code: its sustained clock speed depends on power and
heat, and both can change during a session.

**Power source and energy mode.** macOS offers Low Power Mode, Automatic and,
on some models, High Power Mode, chosen separately for battery and for the
power adapter.[^apple-power] Low Power Mode reduces energy use. High Power Mode
lets the fans run faster, which Apple says may allow higher performance in
the most demanding workloads. Apple also ties that mode to the adapter on
some machines: for the 14-inch MacBook Pro with M4 Pro, it recommends the
96 W adapter for High Power Mode while charging. Time on the adapter, and
record the adapter and the energy mode for both settings, which System
Settings shows under Battery, Energy Mode. `pmset -g batt` shows whether the
machine is drawing from AC power or from its battery, and `pmset -g adapter`
describes the adapter.

**Thermal state.** The `pmset` manual page (`man pmset`) describes
`pmset -g therm` as showing thermal conditions that affect CPU speed and
`pmset -g thermlog` as a log of thermal notifications; it adds that neither
is available on all platforms. Apple's `ProcessInfo.ThermalState` names four
levels, from nominal to critical, and describes the critical level as one
that significantly affects the performance of the system.[^apple-thermal] Record the thermal output before
and after each session. A session with a thermal warning stays in the raw
data, marked, and is left out of results. Keep the physical setup the same
across sessions that will be compared, down to the room, since temperature is
one of the sources of bias Mytkowicz and colleagues list.[^mytk]

**Which cores run the work.** macOS takes a thread's **quality-of-service
(QoS) class**, a label a program attaches to its work to say how important
that work is, into account when it decides where the thread runs. Apple's tuning guide says the system is more
likely to run background work on lower-performance cores, and names
`pthread_set_qos_class_self_np` as the call that sets the class of a POSIX
thread.[^apple-tuning] The harness sets the QoS class of its threads
explicitly, never runs timed work at a background class, and records the
class with the thread count.

### Noise controls on Linux, and their macOS counterparts

The LLVM benchmarking notes and the Google Benchmark documentation list the
usual controls for a Linux machine, with commands for Linux.[^llvm-bench][^gbench-var]
Record which controls were on; a result measured with them is not comparable
to one measured without them. The table gives each control and what this
protocol does on macOS instead.

| Linux control | What it removes | On macOS in this protocol |
| --- | --- | --- |
| Frequency governor set to `performance`; turbo boost (short bursts above the normal clock speed) off | Clock-speed changes during a run | No such switch is used. The documented control is the energy mode, which is fixed and recorded, and the thermal state is recorded before and after |
| **Address-space layout randomization (ASLR)** off: ASLR is a security feature that places the stack, heap and libraries at different addresses on every launch | Layout changes between launches | Left on. Launches are repeated, so the layout varies across the sample instead of being fixed at one value |
| Benchmark pinned to reserved cores (`taskset`, `cset shield`) | Migration between cores, competition on the same core | The QoS class is set explicitly and recorded, since it influences which kind of core runs the work[^apple-tuning] |
| **Simultaneous multithreading (SMT)**, two hardware threads sharing one core: the benchmark core's sibling thread disabled | Sharing one core with another thread | Nothing to do on the M4 Pro used here: `sysctl hw.physicalcpu hw.logicalcpu` reports the same count for both |
| Static linking | Variation from loading dynamic libraries | The link command is recorded, and every variant in a comparison is linked the same way |
| Inputs and outputs on an in-memory file system | Variation from storage | The harness generates inputs in memory and reads no files while timing |
| Other processes and services stopped | Competition for cores, caches and memory | Covered under background load below |

Disabling ASLR lowers noise by fixing one layout, and a fixed layout is the
bias described above, so on either system a claim of a small effect still
needs the setup-randomization check.

### Background load

Close programs that wake up on timers, such as web browsers.[^gbench-var] On
macOS, indexing, backups and synchronization are Apple's own examples of
background work.[^apple-tuning] Such work tends to run on the efficiency
cores, but it still shares the memory system with the benchmark, and Google
Benchmark lists cache effects from code running on other cores among the
sources of variance.[^gbench-var] Record the load averages from `uptime`
before and after each session, and repeat a session whose load changed.

## Correctness before speed

A fast wrong answer is not a result. Before a variant is timed, its output is
checked against a reference, and if the check fails, the harness records the
failure and keeps no timing for that variant. This check is the **correctness
gate**.

The **reference output** comes from something other than the variant under
test. For the [CPU matmul ladder](case-studies/cpu-matmul-ladder.md), it is
the output of the naive loop, rung 0, whose own correctness rests on the
known-answer tests of
[stage 10](../compiler/guide/stage-10-matrix-multiplication.md#comparing-with-a-known-answer).
The environment record names the reference and how it was built. A C++
reference is built with `-ffp-contract=off`, and its record says so, because
Clang's default lets it fuse a multiply and an add in the same statement,
which changes rounding.[^clang]

### Bitwise identity where the ladder promises it

Floating-point addition is not associative: $(a + b) + c$ and $a + (b + c)$
can round differently. The bits of each element of $C$ therefore depend on the
order in which its products are added. A transformation that keeps, for every
element, the same additions in the same order, starting from the same value,
keeps every bit. Loop interchange, vectorizing across the $j$ loop, tiling
with $C$ accumulated in place, packing, and splitting the $i$ or $j$ loops
across threads are all of this kind. A transformation that changes the order
or the number of roundings changes bits: fusing multiplies and adds, splitting
the $k$ loop across threads, or starting accumulators at zero instead of at
$C$.

The Vortex specification forbids an implementation to contract floating-point
operations (for example, fuse a multiply and an add so that they round once),
to reassociate or reorder them, to evaluate them in a wider format, or to
flush subnormal values (tiny values stored with reduced precision) to zero.
Relaxed modes may come later, but only as an explicit opt-in
([§4.4](../specification/types-and-values.md#44-floating-point-values)). The
philosophy asks for documented behavior for reassociation and
non-deterministic parallel reductions
([safety philosophy](../philosophy.md#safety-philosophy)). The gate follows
the same line.

| Kind of variant | Gate | Reported with the result |
| --- | --- | --- |
| Promises identical results | Every output element has the same bit pattern as the reference | Pass or fail |
| Has recorded permission to round differently | The largest absolute and relative differences stay within a tolerance chosen before the run | The permission, the tolerance and the observed differences |

Compare bit patterns, not values. The `==` operator treats `-0.0` and `+0.0`
as equal and reports that a NaN is not equal to itself, so it can hide a real
difference and report a false one.

The inputs must be able to show a difference. Stage 10 chose small whole
numbers so that every product and sum is exact, which is right for a
known-answer test and not enough for this one: exact arithmetic gives the same
bits in any order. Gate inputs therefore also include values generated from a
recorded seed whose products and sums round, such as fractions drawn from
$[-1, 1]$, so that a reordering shows up as changed bits.

### Making sure the work happened

The gate also guards against timing a computation that never ran. An
optimizer can delete a result nobody reads, or move an unchanging computation
out of the timing loop. Google Benchmark offers `DoNotOptimize` and
`ClobberMemory` for C++ comparators, and its guide is explicit that they force
results to be kept; they do not stop the compiler from simplifying the
expression itself.[^gbench-ug] The harness adds two checks of its own. It
checks the output of the last timed repetition as well as the first, and it
compares every rate with the roofline. A rate above the roof means the
measurement is wrong, not that the kernel is fast.

The gate is one check on a few inputs. Testing an optimizer more widely, with
differential testing and fuzzing, is the subject of
[O12](../optimize/o12-testing-optimizers.md) and of the
[differential testing case study](case-studies/differential-testing.md).

## Warm-up

The first executions of a kernel often run slower than later ones, because
caches and other hardware state start cold. Google Benchmark's guide gives
caching effects as an example of why a benchmark may need a warm-up, and its
default warm-up time is zero seconds, which turns warm-up off.[^gbench-ug]
Vortex compiles ahead of time
([stage 6](../compiler/guide/stage-6-first-machine-code.md)), so there is no
just-in-time compiler to wait for, but the hardware still has to settle.

Georges and colleagues separate **start-up performance**, the cost of the
first run, from **steady-state performance**, the cost once the program has
settled.[^georges] Kalibera and Jones refine the second idea. A benchmark
reaches an **initialised state** when its start-up costs are gone, and an
**independent state** when successive iteration times behave like independent
samples.[^kj] They found that many benchmarks never reach an independent state
in reasonable time, and that which iterations a result uses can move it by
tens of percent. Two automatic warm-up rules they tested, including one based
on the **coefficient of variation** (the standard deviation divided by the
mean), sometimes chose warm-ups that were too long, which wastes time, and
sometimes too short, which leaves start-up effects in the result. Their
recommendation is to look: plot iteration time against iteration number, a
**run-sequence plot**, once for each benchmark and platform, and choose the
warm-up from the plot.

For Vortex:

- a pilot session for each kernel and machine draws the run-sequence plot for
  several process launches;
- the warm-up count is chosen from that plot, written into the result, and
  chosen again when the kernel, the machine or the operating system changes;
- warm-up samples stay in the raw data, marked as warm-up, and are excluded
  from the statistics;
- a result says whether it measures the warm state, the default for
  throughput kernels such as matrix multiplication, or the cold first call,
  since the two answer different questions.

## Repetitions: how many, and at which level

Variation enters at three levels. A rebuild can change the layout, through the
link order for instance. Each process launch can run at different addresses:
under ASLR, the stack, heap and libraries move on every launch. The
iterations inside one process differ as well. One of Kalibera and Jones's two
key observations is that repetition is most needed at the level where most of
the uncertainty arises.[^kj] They recommend a **dimensioning experiment**: a
pilot run with repetitions at every level, used to measure how much each level
contributes and so how many repetitions the lower levels need. The top level
then repeats until the interval is narrow enough. The pilot is repeated only
when the benchmark or the platform changes.

Iterations inside one process are often not independent of each other:
Kalibera and Jones found strong auto-dependence in the iterations of many
benchmarks, and Georges and colleagues compute their intervals across
invocations for the same reason.[^kj][^georges] Vortex therefore treats the
**process launch** as the unit of repetition. Each launch contributes one
summary, and intervals are computed across launches. Kalibera and Jones show
that their multi-level interval is the same as a single-level interval
computed over the means of the top-level units, here the launches.[^kj]
Vortex summarizes each launch by the median of its steady-state iterations
instead of the mean, so that one interrupted iteration cannot move the
summary far, and the result says which summary it used.

How many launches depends on the kernel and the machine, not on a constant.
Hoefler and Belli give the count for normally distributed data, from a pilot's
mean $\bar{x}$ and standard deviation $s$ and an allowed relative error
$e$:[^hb]

$$
n = \left( \frac{s \, t_{n-1,\,\alpha/2}}{e \, \bar{x}} \right)^{2}
$$

where $t_{n-1,\,\alpha/2}$ is the quantile of Student's $t$ distribution for
confidence $1 - \alpha$. Timing data is rarely normal, so they also give the
general procedure: add measurements in batches, recompute the confidence
interval after each batch, and stop once it is narrow enough. A nonparametric
interval needs more than five measurements. Georges and colleagues cover the
other ending: if the interval is still too wide after a preset number of runs,
report the interval obtained.[^georges] Kalibera and Jones tabulate the counts
needed for half-widths between 0.5% and 5% of the mean, and the counts differ
widely from benchmark to benchmark, which is the point.[^kj]

For Vortex: at least six launches, the fewest that allow a 95% interval for a
median; then batches of launches until half the width of that interval,
divided by the median, is below the target the case study chose before the
first run, up to a cap. The target and the cap are recorded, and a result
that reached the cap says so.

**Interleave the variants.** Run A, B, C, A, B, C (or a shuffled order) rather
than every run of A followed by every run of B, so that slow drift from heat
or background work spreads over all variants instead of landing on one.
Google Benchmark offers random interleaving of repetitions for this
reason.[^gbench-ug]

**Keep the timer out of the measurement.** Hoefler and Belli suggest that
reading the timer should cost less than 5% of the interval being measured,
and that the timer's resolution should be ten times finer than that
interval.[^hb] Use a monotonic clock, such as `std::chrono::steady_clock` in
C++. When one call of a kernel is too short to time accurately, time a batch
of calls, divide by the batch size, and record the batch size. The
statistics then describe batches, not single calls.[^hb]

**Check small effects against the setup.** When a claimed difference is a few
percent, repeat the comparison under setup randomization (several environment
sizes and link orders) or with layout randomization, and report whether the
conclusion survived.[^mytk][^stab]

## Which statistic to report

Timing distributions are rarely the symmetric bell curve that a mean and a
standard deviation describe. Hoefler and Belli observe that most system
effects make runs slower, so measured times are typically skewed to the right
and often have several peaks.[^hb] Their rules for such data: do not assume
normality without checking, report confidence intervals for data that varies
from run to run, and think about whether a mean or a median answers the
question being asked.

Vortex reports, for each variant and size:

- the **median** of the per-launch summaries: the middle value after sorting,
  which one slow launch cannot move far;
- the spread: the **quartiles** (the values a quarter and three quarters of
  the way through the sorted data), the minimum and maximum, and the number of
  launches $n$;
- a 95% **confidence interval** for the median: an interval computed so that,
  if the whole experiment were repeated many times, 95% of the intervals would
  contain the true median.

There are two standard ways to compute an interval for a median. Vortex uses
ranks for the median of one variant, since that interval needs no random
numbers, and the bootstrap for the ratio of two medians, which ranks do not
cover. The result names the method it used.

**From ranks.** Hoefler and Belli, following Le Boudec, give an interval that
needs no assumption about the shape of the distribution.[^hb] Sort the $n$
values. The interval runs from the value at the lower rank to the value at the
upper rank:

$$
\text{lower} = \left\lfloor \frac{n - z\sqrt{n}}{2} \right\rfloor,
\qquad
\text{upper} = \left\lceil 1 + \frac{n + z\sqrt{n}}{2} \right\rceil
$$

with $z = 1.96$ for 95%. Because it can only use measured values, the interval
can come out slightly wider than necessary. The formula approximates the
binomial distribution, and for the smallest samples it gives a rank below 1
or above $n$; take the ranks from the binomial distribution itself in that
case. Six values is the smallest sample that has a 95% interval,[^hb] and for
six values that interval runs from the smallest value to the largest.

**By bootstrap.** The **bootstrap** estimates how a statistic would vary
across repeated experiments by resampling the data already collected. Draw $n$
values with replacement from the $n$ per-launch summaries, compute the
statistic, repeat $B$ times, and read off the 2.5th and 97.5th percentiles of
the $B$ results. Efron introduced the method in 1979, and the variance of the
sample median was one of the first examples in his paper.[^efron] Resample
launches, not iterations, since iterations inside a launch are not
independent. Record $B$ and the random seed, so that the analysis reproduces
exactly from the raw data.

### Comparing two variants

A case study usually asks how much faster B is than A. Kalibera and Jones
recommend answering with an **effect size** and its confidence interval: a
statement of the form "B is faster than A by x% ± y%, with 95% confidence",
which says how large the change is, how uncertain it is, and how sure the
statement is.[^kj] Vortex uses the ratio of medians,

$$
S = \frac{\tilde{t}_{A}}{\tilde{t}_{B}}
$$

where $\tilde{t}$ is a median time, with an interval from a bootstrap that
resamples the launches of A and the launches of B independently and computes
$S$ from each pair of resamples. Kalibera and Jones build the interval for a
ratio of means with Fieller's method;[^kj] Vortex compares medians, so it
uses the bootstrap.

Checking whether two separate intervals overlap is a weaker test. When they do
not overlap, Georges and colleagues treat the difference as significant; when
they overlap, the difference may be random.[^georges] Kalibera and Jones add
that the overlap test is conservative: with 95% intervals, the chance of
reporting a difference that is not there is below 1% under normality, not 5%,
so real differences get missed.[^kj] The interval for the ratio answers the
question directly.

### Summaries, outliers and rates

**Do not average ratios.** Hoefler and Belli's rules: use the arithmetic mean
only for costs such as times, use the harmonic mean for rates, avoid
summarizing ratios, and if a summary of ratios is unavoidable, use the
geometric mean.[^hb] Vortex reports each size and kernel separately and draws
any summary from the underlying times.

**Do not delete outliers.** Hoefler and Belli advise keeping them and using
measures that resist them, such as the median and the quartiles. When
outliers must go, for instance because a mean is required, they recommend
Tukey's rule, which flags values more than 1.5 **interquartile ranges** (the
distance between the two quartiles) beyond the quartiles, and a report of how
many values were removed.[^hb] In this protocol a sample leaves the
statistics only when a recorded event, such as a thermal warning, explains
it; it stays in the raw data, marked, and the result gives the count.

**Convert to rates last.** A rate such as FLOP/s is $W/t$ for a fixed amount
of work $W$, and it falls as $t$ grows. Vortex computes the rate from the
median time and converts the endpoints of the time interval into the rate
interval, where they swap places. Tables show the time and the rate together.

## Units

### FLOP/s for matrix multiplication

A **FLOP** is one floating-point addition, subtraction, multiplication or
division, and **FLOP/s** is FLOPs per second. A **fused multiply-add (FMA)**
computes $a \times b + c$ with one rounding; it counts as two FLOPs, whatever
instruction performs it.

For $C = AB$ with $A$ of size $M \times K$ and $B$ of size $K \times N$, the
loop body `C[i][j] += A[i][k] * B[k][j]` does one multiplication and one
addition, and it runs $MNK$ times:

$$
W = 2MNK, \qquad \text{rate} = \frac{2MNK}{t}
$$

For square $n \times n$ matrices, $W = 2n^{3}$. The count belongs to the
algorithm, not to the machine code. A rung that vectorizes, unrolls or fuses
does the same arithmetic in fewer instructions, so every rung of the ladder
uses the same $W$ and their rates compare directly. A multiplication that
starts each element from zero needs one addition fewer per element; Vortex
uses $2MNK$ regardless, so that $W$ never changes between rungs.

Prefixes are decimal: 1 GFLOP/s is $10^{9}$ FLOP/s.

### GB/s for memory traffic

**Effective bandwidth** is the number of bytes a kernel reads and writes,
divided by its time:[^cuda-bp]

$$
\beta_{\text{eff}} = \frac{B_{r} + B_{w}}{t}
$$

The hard part is counting the bytes. McCalpin's STREAM documentation
describes three conventions in common use: counting the bytes moved from one
place to another, counting the bytes the program asks to read plus the bytes
it asks to write (STREAM's choice), and counting what the hardware
moves.[^stream] The hardware can move more than the program asked for: on most
cached systems, a store that misses the cache first loads the whole cache
line, which is called a **write allocate**. Vortex counts the bytes the
program requests, as STREAM does, unless the number comes from hardware
counters, and every bandwidth figure says which.

A gigabyte here is $10^{9}$ bytes (GB), not $2^{30}$ bytes (GiB). NVIDIA's
guide gives the reason the choice must be stated: theoretical and effective
bandwidth must use the same divisor, or they cannot be compared.[^cuda-bp]
Rates use decimal prefixes (GB/s, GFLOP/s); cache and memory sizes use binary
ones (KiB, MiB).

Times are in seconds with SI prefixes, and they are per call unless a table
says otherwise. A **percentage of peak** appears only next to the peak it
refers to and the source of that peak, since a vendor's peak is only a limit
the machine will not exceed and a measured one can be lower.[^hb]

## The roofline as context

A rate alone does not say whether it is good. The **roofline model** of
Williams, Waterman and Patterson supplies the reference: an upper bound on the
rate of a kernel on a machine, built from two limits of the machine and one
property of the kernel.[^roofline]

The property of the kernel is its **operational intensity** $I$: FLOPs per
byte of DRAM traffic. The bytes counted are those that reach main memory after
the caches have filtered them, not those between the processor and the
caches. The bound is

$$
P_{\text{attainable}} = \min\left(P_{\text{peak}},\ \beta_{\text{peak}} \times I\right),
\qquad I = \frac{W}{Q}
$$

where $Q$ is the DRAM traffic in bytes, $P_{\text{peak}}$ the machine's peak
FLOP/s and $\beta_{\text{peak}}$ its peak memory bandwidth. On log-log axes,
the bandwidth limit is a line at 45 degrees and the compute limit is a
horizontal line. They meet at the **ridge point**, whose intensity is the
minimum a kernel needs to reach peak performance. Williams and colleagues also
draw **ceilings**, lower roofs that stand for missing optimizations such as
**SIMD** (single instruction, multiple data: one instruction working on
several values at once). A kernel has to break through each lower ceiling
before it can reach the ones above it. Roofs and ceilings are drawn once per
machine, not once per kernel.

<figure class="vx-figure">
<svg viewBox="0 0 760 380" role="img" aria-labelledby="meas-roof-title meas-roof-desc">
<title id="meas-roof-title">A roofline drawn without numbers</title>
<desc id="meas-roof-desc">Log-log axes: operational intensity across, attainable FLOP per second up. A slanted bandwidth roof rises at 45 degrees and meets a flat compute roof at the ridge point. A dashed lower ceiling, labelled no SIMD, runs below the compute roof. An early rung sits below the slanted roof on the memory-bound side; a later rung sits between the ceiling and the flat roof on the compute-bound side, with a short vertical gap to the roof marked room left.</desc>
<line class="vx-line" x1="90" y1="320" x2="722" y2="320"/>
<polygon class="vx-arrowhead" points="722,315 730,320 722,325"/>
<line class="vx-line" x1="90" y1="320" x2="90" y2="38"/>
<polygon class="vx-arrowhead" points="85,38 90,30 95,38"/>
<text class="vx-text-muted" x="410" y="358" text-anchor="middle">operational intensity: FLOP per byte of DRAM traffic (log scale)</text>
<text class="vx-text-muted" transform="translate(52 180) rotate(-90)" text-anchor="middle">attainable FLOP/s (log scale)</text>
<line class="vx-line" x1="300" y1="90" x2="300" y2="320" style="stroke-dasharray: 2 5"/>
<text class="vx-text-muted" x="150" y="308">memory-bound</text>
<text class="vx-text-muted" x="560" y="308">compute-bound</text>
<polyline class="vx-box-accent" points="90,300 300,90 710,90" style="fill: none"/>
<text class="vx-text" x="470" y="78">compute roof: peak FLOP/s</text>
<text class="vx-text" transform="translate(150 212) rotate(-45)" text-anchor="middle">bandwidth roof: peak GB/s × I</text>
<line class="vx-line" x1="230" y1="160" x2="710" y2="160" style="stroke-dasharray: 6 5"/>
<text class="vx-text-muted" x="590" y="178">ceiling: no SIMD</text>
<circle class="vx-dot" cx="300" cy="90" r="6"/>
<text class="vx-text-muted" x="300" y="72" text-anchor="middle">ridge point</text>
<circle class="vx-dot" cx="175" cy="262" r="6"/>
<text class="vx-text-muted" x="186" y="266">early rung</text>
<circle class="vx-dot" cx="520" cy="128" r="6"/>
<line class="vx-line" x1="520" y1="120" x2="520" y2="96" style="stroke-dasharray: 3 3"/>
<text class="vx-text-muted" x="530" y="112">room left</text>
<text class="vx-text-muted" x="530" y="142">later rung</text>
</svg>
<figcaption>Figure 1. A roofline drawn without numbers. The slanted roof is peak memory bandwidth multiplied by operational intensity, the flat roof is the peak arithmetic rate, and they meet at the ridge point. The dashed line is a lower ceiling for a missing optimization. Each measured kernel is a point below the roof, and the vertical gap above it is the performance still available. On a real chart every roof, ceiling and point comes from the recorded machine.</figcaption>
</figure>

For matrix multiplication the model gives a useful pair of figures before any
measurement. If each of $A$, $B$ and $C$ crossed the memory bus exactly once,
the traffic for `f32` values would be

$$
Q_{\min} = 4\,(MK + KN + MN) \ \text{bytes},
\qquad
I_{\max} = \frac{2n^{3}}{12\,n^{2}} = \frac{n}{6} \ \text{FLOP/byte for } n \times n
$$

(add $4MN$ bytes if $C$ is also read). Intensity grows with $n$, so a large
multiplication can in principle be limited by arithmetic rather than by
memory. The naive loop does not get there. Lam, Rothberg and Wolf point out
that, in the worst case, their unblocked loop reads $2N^{3} + N^{2}$ words
from memory for $N^{3}$ iterations, about one word per FLOP, which for 4-byte
values is about $1/4$ FLOP per byte.[^lrw] The rungs of the ladder that tile
and pack exist to move the kernel from the second figure toward the first, and
a roofline chart shows each rung as a point moving up and to the right.

Rules for Vortex roofline charts:

- $P_{\text{peak}}$ and $\beta_{\text{peak}}$ are measured on the recorded
  machine with microbenchmarks, the subject of
  [P3](../optimize/p3-roofline.md), or taken from a vendor document, and the
  chart says which.
- The compute roof matches the rounding rules. A strict rung may not fuse
  multiplies and adds, so its roof is the peak rate of separate multiplies and
  adds; the fused roof applies only to a rung with permission to fuse, as the
  [CPU matmul ladder](case-studies/cpu-matmul-ladder.md) explains.
- A point's intensity is either estimated from a traffic model like the one
  above or measured with hardware counters, and the chart labels which. An
  estimate is never drawn as a measurement.
- A point above the roof is a measurement error.
- The roofline puts a result in context. It does not replace the distribution
  behind each point.

## GPU measurements

The protocol is the same on a GPU, with three additions.
[G14](../gpu/g14-measuring-gpu-code.md) is the chapter on the tools.

- **Asynchrony.** A kernel launch returns before the kernel finishes. NVIDIA's
  guide says that timing with a CPU timer requires synchronizing with the GPU
  immediately before starting and immediately before stopping the timer. On
  NVIDIA GPUs the alternative is CUDA events, which the device timestamps
  with a resolution of about half a microsecond.[^cuda-bp]
- **Transfers.** A result says whether copies between host and device memory
  are inside the timed region. When both matter, kernel time and end-to-end
  time are reported separately.
- **Reductions.** A parallel sum whose order depends on scheduling can change
  bits from run to run. The gate applies unchanged: the
  [GPU matmul ladder](case-studies/gpu-matmul-ladder.md) case study reports,
  for every rung, whether its output equals the CPU reference bit for bit.

The environment record gains the GPU model and the driver and toolkit
versions.

## Storing results

Six rules keep every published figure traceable to the samples behind it:

1. **Raw samples are committed.** Every timed sample, warm-up included and
   marked, one record per iteration, with its variant, shape, process launch
   and time in seconds. Plain text (CSV or JSON Lines) keeps the files
   readable and their changes reviewable.
2. **Environment records are committed** with the samples they describe, one
   before and one after each session.
3. **Scripts are versioned.** The harness, the analysis and the chart scripts
   live in the repository, and each result records the commits of the
   compiler, the harness and the analysis.
4. **Derived numbers are generated.** Every table and chart on a page comes
   from committed raw data through a committed script. No number is typed into
   a page by hand.
5. **Results are append-only.** A new session writes new files, old files are
   not edited, and a page names the files it used.
6. **Failures are kept.** A session that failed the gate or showed a thermal
   warning is committed with the reason, not quietly run again.

Each timed sample carries at least these fields:

| Field | Contents |
| --- | --- |
| `session` | Identifier of the session, linking the sample to its environment records |
| `variant` | The rung or comparator, and the build that produced it |
| `shape` | $M$, $K$, $N$ and the element type |
| `launch` | Process launch number within the session |
| `iteration` | Iteration number within the launch |
| `phase` | `warmup` or `measured` |
| `seconds` | Time for one call, or for one batch with the batch size recorded |
| `gate` | Result of the correctness check for this launch |

**Continuous integration** (CI), the checks GitHub runs on every pushed
change, will not produce timings. Each GitHub-hosted runner, other than the
single-CPU kind, is a new virtual machine,[^gh-runners] so the hardware under
a job is neither chosen nor recorded by the project. The plan is for CI to
run the correctness gate and a short smoke run of the harness on every
change, which catches a wrong answer or a broken harness, while timed
comparisons come from the recorded machine. Today's CI checks only the
documentation. The
[measurement methodology case study](case-studies/benchmark-harness.md)
describes how the two parts will fit together, and
[How Vortex is tested](testing.md) covers the rest of the test suite.

## How charts are drawn

- **From the data, by script.** A chart is regenerated from the committed raw
  data by a committed script. Its caption names the machine, the date, the
  data files and the commits.
- **The distribution, not a bar.** Each launch's summary appears as a dot,
  with the median and its confidence interval marked. A single bar hides the
  spread that the rest of this page works to measure. Hoefler and Belli's
  last rule asks for as much information as the reader needs to interpret the
  result, and they show box plots and violin plots doing that job.[^hb]
- **The base next to every speedup.** Hoefler and Belli ask a parallel
  speedup to name its base case and give the base's absolute performance,
  and they extend the rule to every ratio: never report one without the
  absolute values behind it.[^hb]
- **An upper bound.** Charts of rates show the roof or a measured
  peak.[^hb]
- **Lines only where they mean something.** Points are joined only when they
  show a trend and interpolating between them is valid.[^hb] Sizes on a sweep
  may be joined; different kernels may not.
- **Sizes that can show a cliff.** Lam, Rothberg and Wolf showed that cache
  interference in blocked matrix code is highly sensitive to the stride of the
  accesses and can cause wide variations in performance between matrix
  sizes.[^lrw] A sweep includes neighboring sizes as well as round ones, so
  that a cliff between two round sizes shows up.
- **Units on every axis.** Axes name the quantity and its unit. Roofline
  charts use logarithmic axes.
- **Both themes.** Charts are SVG, styled with the same classes as the figures
  in these docs, so that they follow the light and dark themes.

## A results table, ready to fill

Every result table holds at least these columns for each shape; the ladder
case studies add columns of their own, such as a comparison with a vendor
library. The cells are empty on purpose: numbers enter only from a
measurement session, through the analysis script.

| Rung | Gate | Launches | Median time (s) | 95% CI of the median (s) | Rate at the median (GFLOP/s) | Fraction of the roof |
| --- | --- | --- | --- | --- | --- | --- |
| 0 (naive loop) | reference | | | | | |
| 1 | | | | | | |
| 2 | | | | | | |
| … | | | | | | |

A comparison between two variants adds one row per pair:

| Variant | Base | Ratio of medians | 95% CI (bootstrap) | Launches of each |
| --- | --- | --- | --- | --- |
| | | | | |

## Reporting checklist

| # | The result states | Rule from |
| --- | --- | --- |
| 1 | The machine, operating system, compilers, flags and commits, from the environment record | Hoefler and Belli, rule 9 |
| 2 | Power source, energy mode, QoS class and thermal state, before and after the session | Apple; Mytkowicz and colleagues |
| 3 | That the correctness gate passed, and whether it was bitwise or a tolerance with recorded permission | Specification §4.4; philosophy |
| 4 | The warm-up count and how it was chosen | Kalibera and Jones |
| 5 | The number of launches and iterations, the target interval width, and whether the cap was reached | Kalibera and Jones; Hoefler and Belli; Georges and colleagues |
| 6 | The median, quartiles, minimum, maximum and a 95% confidence interval, with the interval method | Hoefler and Belli, rules 5, 6 and 8 |
| 7 | For a comparison, the ratio with its interval, and the base's absolute time | Kalibera and Jones; Hoefler and Belli, rule 1; Efron |
| 8 | The FLOP count and the byte-counting convention behind every rate | STREAM; NVIDIA |
| 9 | An upper bound, and whether each intensity is estimated or measured | Williams and colleagues; Hoefler and Belli, rule 11; philosophy |
| 10 | For an effect of a few percent, whether it survived a change of setup | Mytkowicz and colleagues; Curtsinger and Berger |
| 11 | The raw data files and script commits behind every table and chart | Hoefler and Belli, rule 9 |
| 12 | Every departure from this page, and the reason for it | This page |

## Where the ideas are taught

Each chapter shows its plan until it is written.

- [P1](../optimize/p1-measure-first.md): measurement from first principles,
  the statistics and the traps behind this protocol.
- [P3](../optimize/p3-roofline.md): the roofline model and the
  microbenchmarks that measure its roofs.
- [P16](../optimize/p16-capstone.md): the CPU ladder measured end to end.
- [O12](../optimize/o12-testing-optimizers.md): testing an optimizer beyond
  the correctness gate.
- [G14](../gpu/g14-measuring-gpu-code.md): measuring GPU code.
- [Case studies](case-studies/index.md): every one that reports a time
  follows this page. Measurement itself is the subject of
  [measurement methodology and performance CI](case-studies/benchmark-harness.md).
  The [cost model and autotuner](case-studies/cost-model-autotuner.md) needs
  the same gate and statistics, because an autotuner is a measurement loop
  that makes decisions. The philosophy's rule that auto-tuning must not
  change the observable meaning of a program
  ([performance philosophy](../philosophy.md#performance-philosophy)) is the
  correctness gate applied to every candidate.

## Sources and further reading

[^mytk]: Todd Mytkowicz, Amer Diwan, Matthias Hauswirth and Peter F. Sweeney, "Producing Wrong Data Without Doing Anything Obviously Wrong!", *ASPLOS 2009*. <https://doi.org/10.1145/1508244.1508275>
[^stab]: Charlie Curtsinger and Emery D. Berger, "Stabilizer: Statistically Sound Performance Evaluation", *ASPLOS 2013*. <https://doi.org/10.1145/2451116.2451141>
[^kj]: Tomas Kalibera and Richard Jones, "Rigorous Benchmarking in Reasonable Time", *ISMM 2013*. <https://doi.org/10.1145/2464157.2464160> (author's copy: <https://kar.kent.ac.uk/33611/>)
[^georges]: Andy Georges, Dries Buytaert and Lieven Eeckhout, "Statistically Rigorous Java Performance Evaluation", *OOPSLA 2007*. <https://doi.org/10.1145/1297027.1297033>
[^hb]: Torsten Hoefler and Roberto Belli, "Scientific Benchmarking of Parallel Computing Systems: Twelve Ways to Tell the Masses when Reporting Performance Results", *SC 2015*. <https://doi.org/10.1145/2807591.2807644>
[^llvm-bench]: LLVM Project, "Benchmarking tips". <https://llvm.org/docs/Benchmarking.html>
[^gbench-var]: Google Benchmark, "Reducing Variance". <https://github.com/google/benchmark/blob/main/docs/reducing_variance.md>
[^gbench-ug]: Google Benchmark, "User Guide", sections on warm-up, random interleaving and preventing optimization. <https://github.com/google/benchmark/blob/main/docs/user_guide.md>
[^apple-power]: Apple, "About Power Modes on your Mac", Apple Support. <https://support.apple.com/en-us/101613>
[^apple-thermal]: Apple, "ProcessInfo.ThermalState", Apple Developer Documentation. <https://developer.apple.com/documentation/foundation/processinfo/thermalstate-swift.enum>
[^apple-tuning]: Apple, "Tuning your code's performance for Apple silicon", Apple Developer Documentation. <https://developer.apple.com/documentation/apple-silicon/tuning-your-code-s-performance-for-apple-silicon>
[^clang]: LLVM Project, "Clang Compiler User's Manual", option `-ffp-contract`. <https://clang.llvm.org/docs/UsersManual.html>
[^efron]: Bradley Efron, "Bootstrap Methods: Another Look at the Jackknife", *The Annals of Statistics* 7(1), 1979. <https://doi.org/10.1214/aos/1176344552>
[^stream]: John D. McCalpin, "STREAM Benchmark Reference Information", University of Virginia, section "Counting Bytes and FLOPS". <https://www.cs.virginia.edu/stream/ref.html>
[^cuda-bp]: NVIDIA, "CUDA C++ Best Practices Guide", sections "Timing" and "Bandwidth". <https://docs.nvidia.com/cuda/cuda-c-best-practices-guide/index.html>
[^roofline]: Samuel Williams, Andrew Waterman and David Patterson, "Roofline: An Insightful Visual Performance Model for Multicore Architectures", *Communications of the ACM* 52(4), 2009. <https://doi.org/10.1145/1498765.1498785>
[^lrw]: Monica S. Lam, Edward E. Rothberg and Michael E. Wolf, "The Cache Performance and Optimizations of Blocked Algorithms", *ASPLOS 1991*. <https://doi.org/10.1145/106972.106981>
[^gh-runners]: GitHub, "GitHub-hosted runners", GitHub Docs. <https://docs.github.com/en/actions/concepts/runners/github-hosted-runners>

# Cost model and autotuner

<p class="page-intro">This case study will test whether a performance model can choose good tile and unroll parameters for the matrix multiplication Vortex generates, by comparing the model's predictions and choices with a measured search over the same parameters. Nothing has been built yet, so every result table below is empty.</p>

<p class="vx-meta">Case study A9 · Status: Not started · Evidence so far: none</p>

This page was written before the work, on purpose. It fixes the question, the
comparisons and the success criteria now, so that results cannot quietly
redefine them later. Every table has defined columns and empty cells. A cell
gets a value only when that value was measured or counted, and each value must
trace back to a commit and a raw data file. Until then the page makes no claim.
It is one of the [case studies](index.md) described in
[Vortex for reviewers](../index.md).

## The question

**Question.** For the blocked matrix multiplication that Vortex generates, how
well does a model built from the roofline and the machine's cache sizes
predict run time, and how close do the parameters it chooses come to the best
ones a measured search finds?

**Claim.** None yet. When the tables are full, the claim will have this shape:
"On machine *M*, the model's choice met the threshold set in advance for *s*
of *t* shapes, including shapes it was never tuned on; where it failed, the
cause was *c*."

A **cost model** predicts how fast a variant of a program will run without
running it. An **autotuner** runs variants and keeps the fastest. The
**search space** is the set of variants either one may choose from. Here it
holds the blocking parameters of a matrix multiplication organized the way
Goto's and the BLIS library's are: the sizes of the blocks kept in each cache
level (mc, kc and nc) and of the block of C kept in registers (mr by
nr),[^goto][^blis] plus loop unroll factors.

## Why employers care

In the compiler and performance job postings read while planning these pages
(September 2026), performance models appeared in few qualification lists, but
where they appeared they were central. The most demanding kernel roles asked
for a roofline or analytical model, measurements against it, and an
explanation of the remaining gap. Middle-end and ML compiler teams described
choosing tile sizes, fusion and schedules with performance models and search,
and some roles built autotuning infrastructure.

A study that reports where its model is wrong, and not only where it is
right, is the kind of evidence those roles look for. It also delivers two
items from the [roadmap's list for after v0.1](../../roadmap.md#after-v01):
cost models and auto-tuning.

## What to build

The study needs the [CPU matmul ladder](cpu-matmul-ladder.md) (A1) through
tiling, packing and register blocking, so that there are parameters to
choose, and the [benchmark harness](benchmark-harness.md) (A2), because a
search is only as good as its measurements.

Both approaches have a long history in dense linear algebra. ATLAS generated
many variants of its kernels and chose among them by measuring.[^atlas] A
later study found that a model-driven version of ATLAS produced code
comparable to the searched one,[^yotov] and BLIS's blocking parameters can be
derived analytically from the machine.[^low] Newer systems combine the two:
OpenTuner runs several search techniques together,[^opentuner] and TVM and
Ansor guide the search for fast tensor programs with learned cost
models.[^tvm][^ansor] This study starts with an analytical model and plain
search, and adds a learned model only if the analytical one fails in ways a
learned model could fix.

| Part | What it does | Done when | Taught in |
| --- | --- | --- | --- |
| Machine facts | Reads cache sizes, cache-line size and core counts from the host instead of hard-coding them | The compiler prints the facts it used in its performance remarks | [P2](../../optimize/p2-memory-hierarchy.md) |
| Roofline | Measures peak arithmetic throughput and memory bandwidth, and from them the **ridge point**, the arithmetic intensity at which a kernel stops being limited by memory[^roofline] | Both are measured under the protocol, with confidence intervals | [P3](../../optimize/p3-roofline.md) |
| Analytical model | Predicts run time for each parameter choice from the roofline and a cache model, and picks the choice it predicts to be fastest | It prints a prediction for every candidate, labelled as an estimate | [P8](../../optimize/p8-cache-blocking.md), [P12](../../optimize/p12-fast-gemm.md), [P15](../../optimize/p15-choosing-parameters.md) |
| Safe search space | Keeps only variants whose results are bitwise identical to the strict reference | A test runs every candidate and compares its output | [P11](../../optimize/p11-floating-point.md), [P14](../../optimize/p14-algorithms-and-schedules.md) |
| Autotuner | Runs a grid or random search under a fixed budget, timed by the harness | Every measurement lands in a CSV file | [P15](../../optimize/p15-choosing-parameters.md), [P16](../../optimize/p16-capstone.md) |
| Remarks | Reports the chosen parameters and whether the model or the tuner chose them | A test checks the remark text | [Principle 6](../../philosophy.md#6-explain-performance-decisions) |

The search space is filtered because the philosophy says auto-tuning must not
change the observable meaning of a program
([performance philosophy](../../philosophy.md#performance-philosophy)). A
blocking choice that changes the order in which each element of C is summed
changes floating-point results, so the filter keeps only choices that
preserve that order. Chapter [P11](../../optimize/p11-floating-point.md)
explains which choices do.

Not in this study: learned cost models, unless the analytical model fails in
a way they could fix; GPU parameters, which rung 8 of the
[GPU matmul ladder](gpu-matmul-ladder.md) tunes with the same machinery; and
thread counts, which stay fixed at one until the single-thread model works.

## Method

The measurement protocol is on
[How Vortex performance is measured](../measuring.md), and the test protocol
is on [How Vortex is tested](../testing.md). This section adds only what is
specific to this study.

### What is compared

1. **Prediction against measurement:** for every candidate in the space, the
   model's predicted time against the measured time.
2. **Choice against search:** the model's pick against the best candidate the
   search found, and against the untuned default parameters.
3. **Cost:** the time to evaluate the model against the time spent searching.
4. **Held-out shapes:** the model is built while looking only at a set of
   tuning shapes. It is then judged on held-out shapes, chosen before any
   measurement, that it was never tuned on.
5. **A second machine,** if one is available: the model, re-derived from that
   machine's facts, against a search on that machine.

Every finalist, meaning the model's pick and the search's best few, is
measured again under the full protocol before any comparison, because noise
in a long search can crown a winner by chance. Comparisons use confidence
intervals rather than the best of a few runs.[^georges]

### Machines

The main study runs on the owner's Apple M4 Pro. It has two kinds of cores,
performance and efficiency, and the measuring page says how runs are kept on
one kind. The transfer test needs a second machine with a different cache
hierarchy. Shared CI runners are not used for timing.

### What counts as success

1. **A safe space.** Every candidate gives bitwise-identical results to the
   strict reference. One mismatch fails the study.
2. **A threshold set in advance.** Before any measurement, the setup table
   states what "close enough" means, for example "the model's pick lies inside
   the confidence interval of the search's best". The commit that sets it
   predates the measurements.
3. **The whole error table.** Prediction error is reported for every
   candidate, worst cases included, and each large error has an explanation,
   such as a cache effect the model ignores.
4. **Held-out shapes judged.** The model is judged on shapes it was not built
   on, and those rows are marked.
5. **An honest cost.** The search's cost is reported, so that a reader can see
   what the model saves.

## Setup

| Item | Value |
| --- | --- |
| Machine: chip, cores, memory | |
| Operating system and version | |
| Cores used, and how runs were kept on them | |
| Parameters searched, with their ranges | |
| Search method and budget | |
| Tuning shapes | |
| Held-out shapes | |
| "Close enough" threshold, and the commit that set it | |
| Vortex commit | |
| Date of the run | |

## Results

When the tables are filled, each row gets one or two sentences of
explanation, backed by measurements or counters. A number without an
explanation does not go in.

### Machine facts and roofline

| Fact | Value | How obtained |
| --- | --- | --- |
| Peak FP32 throughput, one core | | |
| Memory bandwidth, one core | | |
| Ridge point (FLOP per byte) | | |
| L1 data cache size | | |
| L2 cache size | | |
| Cache-line size | | |

- **Value:** measured values with their confidence intervals; queried values
  as the system reports them.
- **How obtained:** the microbenchmark and its commit, or the system query
  used.

### Prediction accuracy

| Shape | Tuning or held-out | Candidates | Rank correlation | Median error | Worst error | Worst candidate |
| --- | --- | --- | --- | --- | --- | --- |
| | | | | | | |
| | | | | | | |
| | | | | | | |

- **Candidates:** the number of parameter choices measured for this shape.
- **Rank correlation:** Spearman's rank correlation between predicted and
  measured times over the candidates. A value of 1 means the model ranks them
  exactly as the measurements do.
- **Median error, worst error:** of the absolute difference between predicted
  and measured time, divided by the measured time, over the candidates.
- **Worst candidate:** the parameter choice with the worst error.

### The model's choice against search

| Shape | Tuning or held-out | Untuned default | Model's choice | Search's best | Model inside threshold | Search cost |
| --- | --- | --- | --- | --- | --- | --- |
| | | | | | | |
| | | | | | | |
| | | | | | | |

- **Untuned default, model's choice, search's best:** GFLOP/s with its 95%
  confidence interval. The parameters behind each value are in the raw data.
- **Model inside threshold:** yes or no, by the rule in the setup table.
- **Search cost:** the number of candidates measured and the total
  wall-clock time.

### Where the model fails

| Case | What the model assumed | What happened | Evidence |
| --- | --- | --- | --- |
| | | | |

- **Evidence:** the counters, measurements or generated code that show the
  cause.

### Second machine

| Machine | Model's choice | Search's best | Model inside threshold |
| --- | --- | --- | --- |
| | | | |

- **Model's choice, search's best:** GFLOP/s with its 95% confidence
  interval, for one shape named in the setup table.

## What did not work

Filled in as the work goes: one row per attempt that failed or was dropped,
including model changes that made things worse.

| Attempt | What happened | What it taught | Commit |
| --- | --- | --- | --- |
| | | | |

## Threats to validity

- **A model shaped by its own test.** If the model is adjusted after seeing
  results on the shapes used to judge it, its accuracy is overstated.
  Held-out shapes, fixed in advance, guard against this.
- **A small search.** The search's best is only the best of what it tried,
  so a small budget makes the model look better than it is. The budget is
  reported, and on a space small enough to try every candidate, the
  exhaustive result serves as ground truth.
- **Noise in the search.** A long search on a noisy machine can pick a winner
  by chance. Finalists are measured again under the full protocol.
- **The harness's own bias.** Model and search share the harness, so a bias
  such as caches warmed by repeated runs shifts both. The comparison between
  them survives it; absolute numbers may not.
- **One machine's facts.** Cache sizes and core types differ between chips.
  A model built on one machine may not transfer, which the second-machine
  table tests.
- **Estimates stay labelled.** Predictions appear only in places marked as
  predictions. The philosophy forbids presenting an estimate as a measured
  result
  ([responsibilities](../../philosophy.md#programmer-and-compiler-responsibilities)).

## What a reviewer should look at

| Evidence | What to check | Where |
| --- | --- | --- |
| One command that runs the model and the search and regenerates every table | It runs from a clean checkout | |
| The model's source and its written assumptions | Each assumption is stated, and the failures table refers to them | |
| The threshold commit | It predates the measurement commits | |
| Search log | Every candidate and every run, in CSV | |
| The safety test | Every candidate's output compared with the strict reference | |
| Remarks | The compiler reports the chosen parameters and where they came from | |
| Decision records | Why an analytical model first, and how the threshold was chosen | |
| CI runs | The safety test and the remark tests pass on the commit in the setup table | |

## Sources

[^goto]: Kazushige Goto and Robert A. van de Geijn, "Anatomy of High-Performance Matrix Multiplication", *ACM Transactions on Mathematical Software* 34(3), 2008. <https://doi.org/10.1145/1356052.1356053>
[^blis]: Field G. Van Zee and Robert A. van de Geijn, "BLIS: A Framework for Rapidly Instantiating BLAS Functionality", *ACM Transactions on Mathematical Software* 41(3), 2015. <https://doi.org/10.1145/2764454>
[^atlas]: R. C. Whaley and J. J. Dongarra, "Automatically Tuned Linear Algebra Software", *Proceedings of the IEEE/ACM SC98 Conference*, 1998. <https://doi.org/10.1109/SC.1998.10004> (free version: LAPACK Working Note 131, <https://www.netlib.org/lapack/lawnspdf/lawn131.pdf>)
[^yotov]: K. Yotov, Xiaoming Li, Gang Ren, M. J. S. Garzaran, D. Padua, K. Pingali and P. Stodghill, "Is Search Really Necessary to Generate High-Performance BLAS?", *Proceedings of the IEEE* 93(2), 2005. <https://doi.org/10.1109/JPROC.2004.840444>
[^low]: Tze Meng Low, Francisco D. Igual, Tyler M. Smith and Enrique S. Quintana-Orti, "Analytical Modeling Is Enough for High-Performance BLIS", *ACM Transactions on Mathematical Software* 43(2), 2017. <https://doi.org/10.1145/2925987>
[^opentuner]: Jason Ansel, Shoaib Kamil, Kalyan Veeramachaneni, Jonathan Ragan-Kelley, Jeffrey Bosboom, Una-May O'Reilly and Saman Amarasinghe, "OpenTuner", *PACT 2014*, pages 303 to 316. <https://doi.org/10.1145/2628071.2628092>
[^tvm]: Tianqi Chen et al., "TVM: An Automated End-to-End Optimizing Compiler for Deep Learning", *OSDI 2018*. <https://www.usenix.org/conference/osdi18/presentation/chen>
[^ansor]: Lianmin Zheng et al., "Ansor: Generating High-Performance Tensor Programs for Deep Learning", *OSDI 2020*. <https://www.usenix.org/conference/osdi20/presentation/zheng>
[^roofline]: Samuel Williams, Andrew Waterman and David Patterson, "Roofline: An Insightful Visual Performance Model for Floating-Point Programs and Multicore Architectures", Technical Report UCB/EECS-2008-134, University of California, Berkeley, 2008; published in *Communications of the ACM* 52(4), 2009. <https://www2.eecs.berkeley.edu/Pubs/TechRpts/2008/EECS-2008-134.html>
[^georges]: Andy Georges, Dries Buytaert and Lieven Eeckhout, "Statistically Rigorous Java Performance Evaluation", *OOPSLA 2007*, pages 57 to 76. <https://doi.org/10.1145/1297027.1297033>

# Native AArch64 back end

<p class="page-intro">This case study will test whether Vortex can turn its own IR into good AArch64 machine code without LLVM, and compare the result with LLVM's back end on the same kernels. It covers instruction selection, register allocation checked by an independent checker, and instruction scheduling. Nothing has been built yet, so every result table below is empty.</p>

<p class="vx-meta">Case study A6 · Status: Not started · Evidence so far: none</p>

This page was written before the work, on purpose. It fixes the question, the
comparisons and the success criteria now, so that results cannot quietly
redefine them later. Every table has defined columns and empty cells. A cell
gets a value only when that value was measured or counted, and each value must
trace back to a commit and a raw data file. Until then the page makes no claim.
It is one of the [case studies](index.md) described in
[Vortex for reviewers](../index.md).

## The question

**Question.** Can a back end written from scratch for Vortex produce correct
AArch64 code for every Vortex test program, and how close does its code come to
LLVM's back end on the same kernels?

**Claim.** None yet. When the tables are full, the claim will have this shape:
"The native back end passes every correctness test on two platforms, and on
kernel *K* its code runs at *r* times the speed of LLVM's back end, for the
reasons shown in the assembly."

Three terms carry the study. **Instruction selection** chooses machine
instructions for each IR operation, usually by matching patterns over the IR.
**Register allocation** decides which values live in the processor's registers
and which are **spilled**: stored to the stack and loaded back when needed.
**Instruction scheduling** reorders instructions inside a block so that slow
operations overlap instead of waiting on each other.

## Why employers care

In the compiler job postings read while planning these pages (September 2026),
most asked for LLVM experience. A smaller group, the back-end teams that build
code generators for GPUs, CPUs and ML accelerators, work every day on the three
problems above. Their postings usually list instruction selection, register
allocation and scheduling as preferred skills rather than requirements, and
some name a substantial compiler project as a way for a candidate to stand
out. CPU performance roles add a related skill: reading generated assembly to
find what the compiler missed.

A back end written from scratch shows understanding of the problems, not only
familiarity with one API. It becomes evidence those teams can use when two
more things are true. Its correctness is checked by something other than its
author: tests, an independent checker and a second back end. And its passes
are mapped onto LLVM's vocabulary (SelectionDAG or GlobalISel, machine IR, the
greedy allocator, the machine scheduler), which is how those teams talk about
the same work. This study includes both.

## What to build

The study starts after the v0.1 compiler passes its
[release gate](../../compiler/guide/stage-11-release.md) and after
[the simplest back end](../../backend/b1-simplest-backend.md) works. That
back end keeps every value in a stack slot, and it is the **baseline** for
every comparison below. Two other case studies feed this one: the
[LLVM IR back end](llvm-backend.md) (A4) supplies the path to compare
against, and [differential testing](differential-testing.md) (A3) supplies
random programs.

| Part | What it does | Done when | Taught in |
| --- | --- | --- | --- |
| Instruction selector | Covers the IR with AArch64 instructions, folding array address arithmetic into addressing modes where it can | Every IR operation the test suite uses has a pattern and a FileCheck test | [C1](../../backend/c1-instruction-selection.md) |
| Liveness | Computes, for each point in the code, which values are still needed later | Its output for the matmul function matches an answer worked out by hand | [C2](../../backend/c2-liveness.md) |
| Linear-scan allocator | Assigns registers in one pass over **live intervals**, the stretches of code where each value is live, and spills when registers run out | The whole suite passes with it, including a stress mode that offers only a few registers | [C3](../../backend/c3-linear-scan.md), [C5](../../backend/c5-spilling.md) |
| Allocation checker | Re-checks every allocation without trusting the allocator | It accepts every allocation in the suite and rejects every seeded fault | [C3](../../backend/c3-linear-scan.md) |
| List scheduler | Reorders instructions inside each basic block by **critical path**, the longest chain of dependent instructions, using a latency table | No block becomes slower by llvm-mca's estimate, and the suite still passes | [C6](../../backend/c6-scheduling.md) |
| Calls and frames | Follows AAPCS64 and Apple's documented differences from it | The same tests pass on macOS and Linux | [A4](../../backend/a4-calling-conventions.md), [A5](../../backend/a5-stack-frames.md) |
| Mapping to LLVM | Names the LLVM counterpart of each pass and what LLVM does beyond it | The mapping table below is filled | [E1](../../backend/e1-llvm-codegen-pipeline.md), [E3](../../backend/e3-llvm-allocator-scheduler-mc.md) |

Why linear scan first: its authors designed it for settings where compile time
matters, such as just-in-time compilers, and reported code almost as efficient
as graph-coloring allocators produce, from a simpler and faster
algorithm.[^poletto] LLVM moved on: in LLVM 3.0 its greedy allocator, which
splits live ranges globally, replaced linear scan as the default optimizing
allocator, and the announcement reported code 1 to 2% smaller and up to 10%
faster than linear scan's (2011).[^olesen] Finding out what that change buys
on Vortex's own kernels is part of the mapping table. The choice itself belongs in a
[decision record](../../decisions/index.md).

Not in this study: graph-coloring allocation
([C4](../../backend/c4-graph-coloring.md)), peephole rules
([C7](../../backend/c7-peephole.md)), a second target
([B2](../../backend/b2-x86-64.md)), writing object files directly
([B3](../../backend/b3-object-files.md)) and debug information
([D1](../../backend/d1-debug-info.md)). Each can follow as its own piece of
work.

## Method

The measurement protocol (runs, warm-up, statistics, machine settings) is on
[How Vortex performance is measured](../measuring.md), and the test protocol
is on [How Vortex is tested](../testing.md). This section adds only what is
specific to this study.

### What is compared

Three paths compile the same Vortex IR:

1. **Baseline:** the simplest back end.
2. **Native:** this study's back end, measured after each pass is added
   (selection, then allocation, then scheduling), so that each pass's effect
   shows on its own.
3. **LLVM:** the LLVM IR from case study A4, compiled by `llc` at `-O0` and
   `-O2`, with no `opt` passes before it. Skipping `opt` keeps the comparison
   about back-end work rather than middle-end optimization. `llc` still runs
   some IR-level passes of its own; `llc -debug-pass=Structure` prints the full
   list, and the page records it.

Four things are compared across those paths:

- **Correctness:** each path's output and exit status against the reference
  path named on the testing page, bit for bit. No path may fuse multiplies and
  adds, because the specification forbids silent changes to floating-point
  results ([section 4.4](../../specification/types-and-values.md#44-floating-point-values)).
  Exact equality is therefore the expectation, not a tolerance.
- **Inner-loop code:** counts taken from the disassembled innermost loop of
  each kernel, plus the cycles-per-iteration estimate from **llvm-mca**,
  LLVM's tool that predicts throughput from its model of a CPU without running
  the code.[^mca]
- **Run time:** measured under the protocol.
- **Compile time:** how long each path takes to produce an object file, since
  compile speed is linear scan's main argument.

The kernels are naive matrix multiplication from
[Milestone 10](../../roadmap.md#milestone-10-matrix-multiplication), in one
square and one rectangular shape; a dot product; and one kernel written to
keep many floating-point values live at once, so that the allocator must
spill.

### Machines

Timing runs happen on one machine, the owner's Apple M4 Pro under macOS,
because it is the only machine where the protocol's settings can be
controlled. Correctness runs happen in CI on both macOS arm64 and Linux arm64
runners.[^runners] Two platforms matter because Apple's arm64 ABI differs from
the standard AAPCS64 in several places (for example, Apple platforms reserve
register `x18`), so a back end tested on one platform can hide bugs that the
other exposes.[^aapcs64][^apple-arm64]

On the owner's machine, `llc` 18.1.8 reports the host CPU as `apple-m1`
(observed on 2026-09-23). The scheduling model llvm-mca uses there therefore
describes an older core than the M4 Pro. The setup table records the model
used.

### How the checker is tested

A checker that never fails proves nothing. The study keeps a set of **seeded
faults**: deliberately broken versions of the allocator, such as one that
gives two overlapping intervals the same register and one that forgets a
reload after a spill. The checker must reject every one.

The checker must also be independent. It walks the allocated code and tracks
which value each register and stack slot holds, instead of reusing the
allocator's own liveness results. This is the approach of the checker built
for Cranelift's register allocator,[^fallin] and [E4](../../backend/e4-testing-backends.md)
teaches the same idea for back ends generally.

### What counts as success

1. **Correct on two platforms.** Every program in the v0.1 suite, and in a
   random batch whose size is fixed before the run, passes on macOS and Linux
   arm64. Every mismatch found along the way has a bug-ledger entry and a
   fixing commit.
2. **Checked allocations.** The checker accepts every allocation in the suite
   and the random batch, and rejects every seeded fault.
3. **No spills in the matmul inner loop.** A **FileCheck** test (LLVM's tool
   for matching patterns in text output)[^filecheck] shows that the innermost
   loop of naive matmul contains no spill or reload.
4. **Each pass pays for itself.** Each pass improves the inner-loop counts or
   the measured time of the stage before it, or the page explains why it did
   not.
5. **Faster than the baseline.** Every kernel runs faster than on the
   baseline, with non-overlapping confidence intervals.
6. **An honest gap to LLVM.** The time relative to `llc -O2` is reported for
   every kernel with its confidence interval, and the largest gap is
   explained from the assembly. No target ratio is promised in advance.

## Setup

| Item | Value |
| --- | --- |
| Machine: chip, cores, memory | |
| Operating system and version | |
| Assembler and linker | |
| LLVM version (`llc`, llvm-mca) | |
| CPU model given to llvm-mca | |
| Flags for each path | |
| Size of the random batch | |
| Vortex commit | |
| Date of the run | |

## Results

When the tables are filled, each row gets one or two sentences of
explanation, backed by the assembly or the measurements. A number without an
explanation does not go in.

### Correctness

| Test set | Programs | Passed | Mismatches | Checker rejections | Platforms |
| --- | --- | --- | --- | --- | --- |
| v0.1 test suite | | | | | |
| Random programs from case study A3 | | | | | |
| Stress mode (small register budget) | | | | | |
| Seeded allocator faults | | | | | |

- **Programs:** how many programs the set contains, fixed before the run.
- **Passed:** programs whose output and exit status match the reference
  exactly. In the seeded-fault row, the faults the checker rejected.
- **Mismatches:** programs whose output or exit status differs. Each one
  needs a bug-ledger entry.
- **Checker rejections:** allocations the checker refused.
- **Platforms:** where the set ran.

### Inner loop of naive matmul

| Path | Instructions per iteration | Memory operations per iteration | Spills and reloads per iteration | Estimated cycles per iteration |
| --- | --- | --- | --- | --- |
| Baseline | | | | |
| Native: pattern selection, values still in stack slots | | | | |
| Native: plus linear scan | | | | |
| Native: plus list scheduling | | | | |
| LLVM, `llc -O0` | | | | |
| LLVM, `llc -O2` | | | | |

- **Instructions per iteration:** machine instructions in the innermost loop
  body, counted from the disassembly.
- **Memory operations per iteration:** loads and stores in that body, spill
  code included.
- **Spills and reloads per iteration:** stack stores and loads that the
  allocator inserted.
- **Estimated cycles per iteration:** llvm-mca's estimate for the loop body
  with the CPU model in the setup table. A model, not a measurement.

### Run time

| Kernel | Shape | Baseline | Native | LLVM `-O0` | LLVM `-O2` | Native relative to `-O2` |
| --- | --- | --- | --- | --- | --- | --- |
| Naive matmul, square | | | | | | |
| Naive matmul, rectangular | | | | | | |
| Dot product | | | | | | |
| Register-pressure kernel | | | | | | |

- **Shape:** the array dimensions, as declared in the Vortex source.
- **Baseline, Native, LLVM:** median run time with its 95% confidence
  interval, as defined on the measuring page.
- **Native relative to `-O2`:** the native median divided by the `-O2`
  median, with a confidence interval for the ratio. Below 1 means the native
  code is faster.

### Compile time

| Program set | Baseline | Native | LLVM `-O0` | LLVM `-O2` |
| --- | --- | --- | --- | --- |
| v0.1 test suite, total | | | | |
| Random batch, total | | | | |

- **Each cell:** median wall-clock time from the back end's input to an
  object file, including the assembler for paths that write assembly text.

### Mapping to LLVM

| Native pass | LLVM counterpart | What LLVM does that the native pass does not |
| --- | --- | --- |
| Pattern-based instruction selection | SelectionDAG or GlobalISel[^codegen][^globalisel] | |
| Linear-scan allocation | The greedy allocator[^olesen] | |
| List scheduling | The machine scheduler[^braun] | |
| Allocation checker | | |

- **LLVM counterpart:** the part of LLVM's code generator that does the same
  job, taken from LLVM's documentation.
- **What LLVM does that the native pass does not:** filled in after reading
  LLVM's pass on the same kernels, with `llc` output as evidence.

## What did not work

Filled in as the work goes: one row per attempt that failed or was dropped.
Failed attempts are part of the evidence, because they show how the final
design was reached.

| Attempt | What happened | What it taught | Commit |
| --- | --- | --- | --- |
| | | | |

## Threats to validity

- **The comparison depends on A4's IR.** If the LLVM IR that A4 emits is poor,
  LLVM's back end starts from a worse input and the native back end looks
  better than it is. The LLVM IR for each kernel is published next to the
  results.
- **Back-end work or middle-end work.** Keeping `C[i][j]` in a register
  across the inner loop, for example, is scalar replacement, a middle-end
  transformation. Comparing against `llc` without `opt` keeps such
  differences out of the result, and any IR pass that `llc` runs by itself is
  listed.
- **Estimates are not measurements.** llvm-mca's accuracy depends on LLVM's
  scheduling models, and it models neither branch prediction nor the cache
  hierarchy.[^mca] On this machine it also uses an older core's model.
  Estimates appear only in their own column.
- **One machine for timing.** Results from one M4 Pro say little about other
  AArch64 cores.
- **Tests find bugs; they do not prove their absence.** Random programs
  rarely create high register pressure, which is where allocators break. The
  stress mode and the seeded faults narrow that gap without closing it.
- **Shared blind spots.** A checker that reused the allocator's liveness would
  share its bugs. Independence is a design rule for the checker, and its
  source is where a reviewer can confirm it.
- **Runtime checks.** Bounds and overflow checks from
  [stage 9](../../compiler/guide/stage-9-runtime-safety.md) add instructions
  to the inner loop. All paths must keep the same checks, or the counts
  compare different programs.

## What a reviewer should look at

| Evidence | What to check | Where |
| --- | --- | --- |
| One command that rebuilds the compiler, runs the tests and regenerates every table | It runs from a clean checkout | |
| Commits for each pass | Each pass lands with its own tests | |
| Decision records | Why linear scan first, why an independent checker, where the latency table came from | |
| FileCheck tests on assembly | The no-spill test for the matmul inner loop | |
| CI runs | macOS arm64 and Linux arm64, passing on the commit in the setup table | |
| Raw data | A CSV file with every timed run and every static count | |
| Before-and-after assembly | The matmul inner loop for each row of the inner-loop table | |
| Bug-ledger entries | Bugs found by differential testing, with their fixing commits | |

## Sources

[^poletto]: Massimiliano Poletto and Vivek Sarkar, "Linear Scan Register Allocation", *ACM Transactions on Programming Languages and Systems* 21(5), 1999, pages 895 to 913. <https://doi.org/10.1145/330249.330250>
[^olesen]: Jakob Stoklund Olesen, "Greedy Register Allocation in LLVM 3.0", The LLVM Project Blog, 18 September 2011. <https://blog.llvm.org/2011/09/greedy-register-allocation-in-llvm-30.html>
[^fallin]: Chris Fallin, "Cranelift, Part 3: Correctness in Register Allocation", 15 March 2021. <https://cfallin.org/blog/2021/03/15/cranelift-isel-3/>
[^mca]: LLVM Project, "llvm-mca - LLVM Machine Code Analyzer". <https://llvm.org/docs/CommandGuide/llvm-mca.html>
[^filecheck]: LLVM Project, "FileCheck - Flexible pattern matching file verifier". <https://llvm.org/docs/CommandGuide/FileCheck.html>
[^runners]: GitHub, "GitHub-hosted runners reference". <https://docs.github.com/en/actions/reference/runners/github-hosted-runners>
[^aapcs64]: Arm, "Procedure Call Standard for the Arm 64-bit Architecture (AArch64)", release 2025Q4. <https://github.com/ARM-software/abi-aa/blob/main/aapcs64/aapcs64.rst>
[^apple-arm64]: Apple, "Writing ARM64 code for Apple platforms". <https://developer.apple.com/documentation/xcode/writing-arm64-code-for-apple-platforms>
[^codegen]: LLVM Project, "The LLVM Target-Independent Code Generator". <https://llvm.org/docs/CodeGenerator.html>
[^globalisel]: LLVM Project, "Global Instruction Selection". <https://llvm.org/docs/GlobalISel/index.html>
[^braun]: Matthias Braun, "Welcome to the Back End: The LLVM Machine Representation", 2017 LLVM Developers' Meeting. <https://llvm.org/devmtg/2017-10/slides/Braun-Welcome%20to%20the%20Back%20End.pdf>

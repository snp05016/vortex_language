# A4. LLVM IR back end

<p class="page-intro">This case study asks whether Vortex can emit correct LLVM IR from the same intermediate representation its other back end uses, and what that choice costs and buys in correctness, code quality, speed and compile time. It is also the groundwork for the upstream contributions in A5.</p>

<p class="vx-meta">Status: Not started · Planning size: M</p>

## The question

Given the same Vortex IR, does a back end that emits LLVM IR produce programs
that behave exactly like the other back end on every test and every random
program, and how do the two compare on run time, code quality and compile
time for the matrix multiplication kernel?

The v0.1 back end is still the owner's decision:
[stage 6](../../compiler/guide/stage-6-first-machine-code.md#choosing-a-back-end)
lays out LLVM IR, C and direct assembly as options, and the
[implementation choices](../../decisions/implementation.md#i1) record who
decides. The question works either way. If v0.1 emits assembly or C, this
study adds LLVM IR as the second back end. If v0.1 already emits LLVM IR, this
study is the comparison with the native back end of
[A6](native-aarch64-backend.md) once that exists. Two back ends, one IR,
measured side by side.

When the work is done, this section will hold one sentence of this form:

> Across ___ tests and ___ generated programs, the LLVM IR back end and the
> ___ back end agree on every observable result; on the stage 10 kernel at
> ___ × ___ × ___, the LLVM path at ___ runs in ___ ms against ___ ms, and
> compiles in ___ ms against ___ ms.

## Why employers care

LLVM is the compiler infrastructure named most often in compiler job
postings, and a number of teams require it rather than merely prefer it. Emitting
LLVM IR from your own compiler is the most direct honest route to that
experience. It forces you to learn exactly what LLVM IR promises: when a value
is poison, what `nsw` and `noalias` allow the optimizer to assume, which
floating-point flags permit which rewrites, and what a front end must emit so
that LLVM's optimizer can do its work.

Comparing the result with a second back end on the same programs adds
something a tutorial cannot: evidence of judgment about when a large dependency
is worth its cost. And the bugs and rough edges met along the way
are the natural starting points for upstream patches in
[A5](upstream-contributions.md).

## What to build

**Lowering from Vortex IR to LLVM IR.** How the IR is produced, as text or
through LLVM's C++ interface, is the owner's choice. What it must say is fixed
by Vortex's rules, and each rule meets a specific feature of LLVM IR.

| Vortex rule | What LLVM IR offers | What the lowering must do |
| --- | --- | --- |
| Integer overflow is a [runtime error](../../language-tour/06-runtime-and-numerical-rules.md#checked-operations). | With `nsw` or `nuw`, an overflowing `add` produces **poison**, a value that makes later uses undefined.[^langref] The overflow intrinsics return the result together with a bit that says whether it overflowed.[^langref] | Emit `nsw` or `nuw` only where the compiler has proved that overflow cannot happen; check everything else, for example with the overflow intrinsics, and branch to the runtime error. |
| No transformation may change a specified floating-point result ([4.4](../../specification/types-and-values.md#44-floating-point-values)). | Fast-math flags on floating-point instructions permit rewrites; `contract` permits fusing a multiply and an add.[^langref] | Emit no fast-math flags in strict mode. |
| One mutable access excludes every conflicting access ([aliasing, 9.8](../../specification/references.md#98-aliasing)). | `noalias` on a parameter promises that memory reached through it is not accessed through other pointers during the call; breaking the promise is undefined behaviour.[^langref] | Emit `noalias` only where the aliasing rule guarantees it. The rule leaves some cases to future specification work, and a wrong `noalias` becomes a miscompile. |
| A failed runtime check stops the program with status 101 and one line on standard error ([decision 14](../../decisions/program.md#d14)). | Ordinary branches and calls. | Branch to a runtime routine that writes the line and exits, identically to the other back end. |

**Locals the optimizer can promote.** LLVM's advice to front-end authors is to
place `alloca` instructions at the start of the entry block, where its SROA
and mem2reg passes can turn them into registers, and to add attributes such
as `noalias` to function arguments where they are true.[^perf-tips]
[Stage 6](../../compiler/guide/stage-6-first-machine-code.md#ssa-if-you-use-llvm)
explains why this avoids building SSA form by hand.

**The verifier in CI.** LLVM provides a verifier that checks generated IR for
consistency, and the Kaleidoscope tutorial runs it on every function it
builds because it catches many bugs early.[^kal3] Every module Vortex emits
passes the verifier in CI.

**IR tests.** A FileCheck test for each row of the table above, both the case
where the feature must appear and the case where it must not, such as a
strict floating-point kernel checked for the absence of `contract`.[^filecheck]

**A comparison harness** that runs the same programs through both back ends,
using [A3](differential-testing.md)'s runner for correctness and
[A2](benchmark-harness.md)'s harness for time.

Chapters: [stage 6](../../compiler/guide/stage-6-first-machine-code.md) covers
the back-end choice, SSA and linking;
[B1](../../backend/b1-simplest-backend.md) builds the simplest native back end
to compare against; [E4](../../backend/e4-testing-backends.md) covers testing
back ends with FileCheck and lit;
[E1](../../backend/e1-llvm-codegen-pipeline.md) walks LLVM's own code
generator pipeline; [O1](../../optimize/o1-optimizer-contract.md) sets out
what an optimizer may and may not change, and
[O10](../../optimize/o10-pass-pipelines.md) and
[O11](../../optimize/o11-undefined-behavior.md) cover pass pipelines,
undefined behaviour and poison.

This study leaves out:

- writing an LLVM back end for a new processor, which is the TableGen work of
  the back-end book's part E;
- GPU targets through LLVM, which belong to [A8](gpu-matmul-ladder.md) and
  [M12](../../mlir/m12-vortex-gpu-path.md);
- tuning LLVM's pass pipeline beyond its standard levels.

## Method

Timings follow [How Vortex performance is measured](../measuring.md), and
correctness checks follow [How Vortex is tested](../testing.md).

### What is compared with what

1. **Correctness.** The LLVM path against the other back end on the whole test
   suite, on every example in the documentation, and on A3's generated
   programs: identical standard output, exit status, and runtime-error kind
   and position.
2. **Code quality.** For each kernel, the inner loop's instruction count,
   loads and stores, and register spills, read from the assembly. Next to
   them, the throughput that llvm-mca predicts for the inner loop. llvm-mca
   is a static model: it estimates instructions per cycle and resource
   pressure from LLVM's scheduling model for a named CPU, and it does not
   model branch prediction or the cache hierarchy.[^llvm-mca] Its numbers go
   in a column labelled as a model.
3. **Run time** of each kernel, measured by A2's harness.
4. **Compile time and executable size** for each program.
5. **Matched configurations only.** LLVM with no optimization passes against
   the other back end without optimization; LLVM's standard pipeline against
   the other back end with Vortex's own passes from A1. An optimized path is
   never compared with an unoptimized one without saying so in the table.

### Machines

The owner's Apple M4 Pro for every measured number. For llvm-mca, the CPU
model closest to that machine among those the installed LLVM offers, named in
the setup table. A model of a different chip is still only a model.

### What counts as success

1. Every module the compiler emits passes the LLVM verifier in CI.
2. The two back ends disagree on nothing across the test suite, the
   documentation examples and the campaigns listed in the results, or every
   disagreement has a verdict and a fix.
3. Every row of the lowering table has a passing FileCheck test for its
   positive and negative case.
4. The comparison tables are filled from measurements, and every difference
   larger than the noise floor is explained with an IR or assembly excerpt.
5. The decision the comparison informs, such as which back end is the
   default and why, is written down as a decision record.

## Setup

| Field | Value |
| --- | --- |
| Machine and chip |  |
| Operating system |  |
| Vortex commit |  |
| LLVM version used by the LLVM path |  |
| Other back end, and its toolchain versions |  |
| System assembler and linker |  |
| llvm-mca CPU model |  |
| Flags for each configuration |  |
| Date of the runs |  |

## Results

### Correctness

| Test set | Programs | Agree | Disagree | Verifier failures | Verdicts |
| --- | --- | --- | --- | --- | --- |
| v0.1 test suite |  |  |  |  |  |
| Documentation examples |  |  |  |  |  |
| A3 campaigns (list the campaign dates) |  |  |  |  |  |

### IR obligations

| Obligation | Positive test | Negative test | Passing in CI |
| --- | --- | --- | --- |
| No fast-math flags in strict mode |  |  |  |
| `nsw` and `nuw` only where overflow is proved impossible |  |  |  |
| Overflow checked everywhere else |  |  |  |
| `noalias` only where the aliasing rule guarantees it |  |  |  |
| Bounds checks kept unless proved unnecessary |  |  |  |
| Runtime errors exit with status 101 and one standard-error line |  |  |  |

### Code quality and run time

One table per kernel and shape.

| Back end | Configuration | Inner-loop instructions | Loads and stores in the inner loop | Spills and reloads | llvm-mca block reciprocal throughput (model) | Median time (ms) | 95% CI (ms) |
| --- | --- | --- | --- | --- | --- | --- | --- |
| LLVM | No optimization passes |  |  |  |  |  |  |
| LLVM | Standard pipeline |  |  |  |  |  |  |
| Other back end | No optimization |  |  |  |  |  |  |
| Other back end | Vortex's own passes |  |  |  |  |  |  |

Inner-loop instructions
: Machine instructions in the body of the innermost loop, counted from the
  assembly the back end produced.

Spills and reloads
: Stores of register values to the stack and the loads that bring them back,
  inside the kernel.

llvm-mca block reciprocal throughput (model)
: The theoretical cycles per iteration of the inner loop that llvm-mca
  derives from dispatch width and hardware resources, ignoring dependencies
  that carry from one iteration to the next; lower is better.[^llvm-mca] A
  prediction, not a measurement. For a loop whose iterations wait on each
  other, such as a strict running sum, the real cost per iteration can be
  far higher.

Median time and 95% CI
: As defined on the [measuring page](../measuring.md), for one full run of
  the kernel.

### Compile time and size

| Program | Back end | Configuration | Median compile time (ms) | 95% CI (ms) | Executable size (bytes) |
| --- | --- | --- | --- | --- | --- |
|  | LLVM | No optimization passes |  |  |  |
|  | LLVM | Standard pipeline |  |  |  |
|  | Other back end | No optimization |  |  |  |
|  | Other back end | Vortex's own passes |  |  |  |

### Cost of the dependency

| Configuration | Clean build time of the compiler | Disk space of the toolchain it needs |
| --- | --- | --- |
| With the LLVM path |  |  |
| Without the LLVM path |  |  |

## Analysis

Empty until the first measured run. This section will explain each
difference in code quality with an IR or assembly excerpt, and say which LLVM
passes account for the gap between the two LLVM configurations.

## What did not work

Empty until work begins.

## Threats to validity

**Undefined behaviour leaking in.** An `nsw` or a `noalias` emitted without
proof lets LLVM assume something false about a legal Vortex program, and the
resulting miscompile may appear only for some inputs. A3's campaigns are the
net for this, and the IR obligation tests are the first line.

**Unequal optimization.** LLVM's standard pipeline does far more than a young
back end. The matched configurations keep the comparison fair, and the table
names the configuration on every row.

**Model against measurement.** llvm-mca knows nothing about caches or branch
prediction.[^llvm-mca] Its predictions explain the shape of a loop, not its
speed, and never sit in a measured column.

**Version drift.** Different LLVM versions optimize differently. The setup
table pins the version, and a result is rerun before a version change is
reported as an improvement.

**Different runtimes.** Both back ends must link the same runtime. If they do
not, a difference in printing or error reporting looks like a back-end bug.

**Compile time includes start-up.** Compile time measured for a whole compiler
run includes loading LLVM. The table says so, rather than splitting it out by
guesswork.

## Reproduce

Empty until the first measured run. This section will give one command that,
from a clean checkout, builds both back ends, runs the correctness comparison
and the benchmarks, and regenerates every table on this page.

## What a reviewer should look at

| Evidence | Where it will be | Available |
| --- | --- | --- |
| The commits that add the LLVM IR lowering |  | Not yet |
| The FileCheck tests for each IR obligation |  | Not yet |
| The CI job that runs the verifier on every emitted module |  | Not yet |
| Logs of the correctness comparison, including A3 campaigns |  | Not yet |
| Raw timing and size data with environment records |  | Not yet |
| The decision record that the comparison informed |  | Not yet |

## Sources

[^langref]: LLVM Project, "LLVM Language Reference Manual", sections on the `add` instruction, arithmetic with overflow intrinsics, fast-math flags and parameter attributes. <https://llvm.org/docs/LangRef.html>
[^perf-tips]: LLVM Project, "Performance Tips for Frontend Authors". <https://llvm.org/docs/Frontend/PerformanceTips.html>
[^kal3]: LLVM Project, "My First Language Frontend with LLVM Tutorial", chapter 3, "Code generation to LLVM IR". <https://llvm.org/docs/tutorial/MyFirstLanguageFrontend/LangImpl03.html>
[^filecheck]: LLVM Project, "FileCheck - Flexible pattern matching file verifier". <https://llvm.org/docs/CommandGuide/FileCheck.html>
[^llvm-mca]: LLVM Project, "llvm-mca - LLVM Machine Code Analyzer". <https://llvm.org/docs/CommandGuide/llvm-mca.html>

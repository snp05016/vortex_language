# A3. Differential testing and fuzzing

<p class="page-intro">This case study asks whether every path by which Vortex can compile and run a program gives the same answer, and which bugs random programs find that the hand-written tests missed. It is the safety net under every optimization and every back end on this site.</p>

<p class="vx-meta">Status: Not started · Planning size: M</p>

## The question

When thousands of randomly generated, well-typed Vortex programs are compiled
and run through every execution path the compiler offers, do all paths agree
on the output, the exit status and the runtime error, and what do the
disagreements reveal?

[Differential testing](../testing.md#the-test-pyramid) runs the same program
through two or more implementations that should agree and treats any
disagreement as a bug in one of them. The rule that decides whether a result
is right is called the [oracle](../testing.md#the-test-pyramid); here the
oracle is agreement between paths.
[Coverage-guided fuzzing](../testing.md#coverage-guided-fuzzing) feeds a
program large numbers of generated inputs to find crashes and hangs.
[Test-case reduction](../testing.md#test-case-reduction) shrinks a failing
input to the smallest one that still fails, so that a person can see the bug.

When the work is done, this section will hold one sentence of this form:

> A campaign of ___ generated programs across ___ execution paths, at commit
> ___, found ___ bugs that the existing test suite missed; each one has a
> reduced regression test in the suite and a fix commit.

## Why employers care

A compiler that crashes is annoying. A compiler that silently produces wrong
code is dangerous, because nothing tells the user. Compiler teams therefore
care a great deal about validation: testing shows up in their job
descriptions both as a qualification and as part of the daily work, and
debugging is one of the skills they ask for most often.

A system that finds miscompiles automatically, reduces each one to a few
lines and turns it into a permanent regression test is direct evidence of
those skills. A ledger of real bugs, each with its root cause and the commit
that fixed it, is evidence a reviewer can check line by line.

The approach has a strong record. Csmith, a generator of random C programs,
compiles each program with several compilers, runs them and compares the
outputs; over three years its authors reported more than 325 previously
unknown compiler bugs.[^csmith] **Equivalence modulo inputs** (EMI) takes a
program and an input, deletes code that the input never executes, and checks
that the output does not change; its authors report 147 confirmed, unique
bug reports for GCC and LLVM from eleven months of testing.[^emi]

## What to build

**A generator of random Vortex programs** that the type checker accepts by
construction. It covers arithmetic in every numeric type, casts, fixed-shape
arrays, nested loops, functions, structs and references, and it can be told
to favour one feature at a time.

Vortex makes one part of this easier than it is for C. Csmith has to avoid
every program that executes any of C99's 191 kinds of undefined behaviour,
because a C compiler may do anything with such a program.[^csmith] In Vortex,
out-of-bounds indexing, integer division by zero, integer overflow and
impossible casts are
[runtime errors](../../language-tour/06-runtime-and-numerical-rules.md#checked-operations)
with a defined outcome. A generated program may hit one, and then every path
must stop the same way: exit status 101 and one line on standard error naming
the kind of error and its source position, as
[decision 14](../../decisions/program.md#d14) specifies. The generator still
has to keep most programs away from those errors, because a program that
stops at its first overflow tests little. Loops get bounded trip counts by
construction, and a timeout catches anything that slips through.

Floating point needs no tolerance. The compiler must not change a result the
specification fixes
([types and values, 4.4](../../specification/types-and-values.md#44-floating-point-values)),
so every strict path must produce the same bits, and any difference is a bug
or a gap in the specification.

**A runner** that compiles each program on every path, runs it with a
timeout, and records standard output, the exit status, and the runtime-error
line. Decision 14 leaves the error message text to the implementation, so the
runner compares the error's kind and position, not its wording.

**A reducer** that shrinks a failing program while keeping it well-typed and
keeping the same disagreement. The authors of C-Reduce found that generic
reduction, which deletes pieces of text, tends to leave C test cases that are
too large or no longer valid, and built reducers that understand the
language instead.[^creduce] A Vortex reducer removes statements, simplifies
expressions and shrinks array shapes, and runs the type checker after every
step.

**Front-end fuzzing.** The lexer, parser and type checker must never crash or
hang on any input, and every rejected input must get a diagnostic. A
coverage-guided fuzzer mutates its inputs toward code they have not yet
reached. libFuzzer is one: it runs inside the program under test and needs a
matching Clang version.[^libfuzzer] Its documentation also says that its
original authors have stopped active work on it, while important bugs still
get fixed,[^libfuzzer] so check that your toolchain ships it before planning
around it.

**The bug ledger and regression tests.** Each bug found becomes a small
regression test in the normal suite. LLVM's testing guide describes the same
practice: when a bug is found, a test with only as much code as it takes to
reproduce it goes into the regression suite,[^testing-guide] where tools like lit run it
and FileCheck checks its output.[^lit][^filecheck]

**A planted-bug check.** In a branch, deliberately break one pass, for
example by reversing a loop interchange's legality test, and confirm that a
campaign finds the bug within its normal budget. This shows the system can
find the kind of bug it exists for.

The paths it compares, as they become available:

| Path | What it is | Case study |
| --- | --- | --- |
| Reference | The v0.1 compiler with no optimization | Stages 6 to 10 of the [guide](../../compiler/guide/index.md) |
| Optimized | The same back end with the optimizer's passes enabled | [A1](cpu-matmul-ladder.md) |
| LLVM IR back end | Vortex IR lowered to LLVM IR, then to machine code by LLVM | [A4](llvm-backend.md) |
| Native AArch64 back end | Vortex's own instruction selection and register allocation | [A6](native-aarch64-backend.md) |
| MLIR path | Lowering through MLIR dialects to LLVM | [A7](mlir-lowering-path.md) |

Chapters: [O12](../../optimize/o12-testing-optimizers.md) teaches testing an
optimizer with random programs, [B1](../../backend/b1-simplest-backend.md)
tests a first native back end against the v0.1 path, and
[E4](../../backend/e4-testing-backends.md) covers FileCheck, lit and
differential testing for back ends. The guide's
[stage 9](../../compiler/guide/stage-9-runtime-safety.md) defines the runtime
checks the oracle compares, and
[stage 11](../../compiler/guide/stage-11-release.md) the test suite the
regression tests join.

This study leaves out, for later work:

- **translation validation**, which checks each optimized function against
  the original instead of comparing whole program runs. Alive2 does this for
  LLVM and found 47 new bugs by running over LLVM's unit tests.[^alive2]
- EMI-style variants of generated programs;
- fuzzing the back ends with structured IR inputs, as LLVM's own
  `llvm-isel-fuzzer` and `llvm-opt-fuzzer` do for instruction selection and
  optimization passes.[^fuzzing-llvm]

## Method

The general test protocol is on [How Vortex is tested](../testing.md).

### What is compared with what

- **Every path with the reference path, for every generated program.** The
  observable result is standard output byte for byte, the exit status, and,
  when the status is 101, the runtime error's kind and source position.
- **Compilation itself.** The generator only produces valid programs, so any
  rejection, crash or hang of the compiler on a generated program is a bug.
- **Front-end fuzzing.** The oracle is the absence of crashes and hangs, and a
  diagnostic for every rejected input.

### Machines

The owner's Mac (arm64, macOS) runs development campaigns. Scheduled
campaigns run in CI on hosted Linux and macOS runners, for the paths those
runners support. Every campaign records its seed and generator version, and
every disagreement must reproduce from its seed.

### What counts as success

1. Campaigns run on a schedule in CI with a fixed budget, and their logs are
   public.
2. Every disagreement gets a verdict: compiler bug, generator bug, runner
   bug, or a gap in the specification.
3. Every compiler bug has a ledger row, a reduced regression test in the
   suite and a fix commit.
4. The planted-bug check finds every planted bug within the normal budget.
5. The generator's feature coverage is published, so that a quiet campaign
   cannot hide a generator that never exercises, say, nested loops.

## Setup

| Field | Value |
| --- | --- |
| Machines and runners |  |
| Operating systems |  |
| Vortex commit |  |
| Generator commit and version |  |
| Paths included |  |
| Seed range |  |
| Per-program timeout |  |
| Budget per campaign (programs or hours) |  |
| Fuzzer and its version, for front-end fuzzing |  |
| Date of the campaign |  |

## Results

### Campaigns

| Date | Commit | Generator version | Programs | Paths | Disagreements | Unique bugs | Machine time | Log |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
|  |  |  |  |  |  |  |  |  |

Disagreements
: Programs on which at least one path's observable result differed from the
  reference path.

Unique bugs
: Distinct root causes behind the disagreements, after reduction and triage.
  Many programs can share one bug.

Machine time
: The total processor time the campaign used, with the machine named.

### Bug ledger

| ID | Found by | Paths that disagreed | Component | Root cause, in one line | Reduced test | Fix commit | Found on | Fixed on |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
|  |  |  |  |  |  |  |  |  |

Found by
: Differential campaign, front-end fuzzing, planted-bug check, or another
  source named in full.

Component
: The part of the compiler the fix changed, such as the parser, a pass, or a
  back end.

### Planted bugs

| Planted bug | Where | Found? | Programs until first detection | Size of the reduced test (lines) | Branch |
| --- | --- | --- | --- | --- | --- |
|  |  |  |  |  |  |

### Front-end fuzzing

| Target | Fuzzer | Inputs run | Crashes | Hangs | Unique issues | Corpus |
| --- | --- | --- | --- | --- | --- | --- |
| Lexer |  |  |  |  |  |  |
| Parser |  |  |  |  |  |  |
| Type checker |  |  |  |  |  |  |

### Generator coverage

| Language feature | Generated? | Share of programs using it |
| --- | --- | --- |
| Integer arithmetic in every integer type |  |  |
| Floating-point arithmetic in `f32` and `f64` |  |  |
| Casts, including ones that fail at run time |  |  |
| Fixed-shape arrays, including out-of-bounds indexes |  |  |
| Nested loops with `break` and `continue` |  |  |
| Function calls |  |  |
| Structs |  |  |
| Shared and mutable references |  |  |
| Programs that end with a runtime error |  |  |

## Analysis

Empty until the first campaign. This section will group the bugs by
component and by the kind of program that exposed them, and say what the
groups suggest about where the compiler is weak.

## What did not work

Empty until work begins.

## Threats to validity

**Shared parts hide shared bugs.** Every path shares the front end and, in
most cases, the Vortex IR. A bug there makes every path agree on the same
wrong answer, and differential testing cannot see it. Known-answer tests and
tests written from the specification remain necessary.

**Generator bias.** The campaign finds bugs only in the kinds of programs the
generator writes. The coverage table exists to make that limit visible.

**Counting.** A thousand failing programs may share one bug. The ledger counts
root causes, never failing programs.

**A quiet campaign is not proof.** Finding no bugs may mean the compiler is
sound, or that the generator is too weak to find anything. The planted-bug
check separates the two.

**A reducer that changes the bug.** A reduced program can fail for a
different reason than the original. Every reduced test is checked to show the
same disagreement as the program it came from.

**Relaxed modes later.** If Vortex adds an opt-in mode that allows results to
change, paths in that mode need a tolerance instead of a bitwise comparison,
and the page must say which paths use which rule.

**Timeouts.** A slow path can look like a hang. A timeout is reported as its
own verdict, never as agreement.

## Reproduce

Empty until the first campaign. This section will give one command that
reruns any campaign from its seed and generator version, and one that
replays a single ledger entry.

## What a reviewer should look at

| Evidence | Where it will be | Available |
| --- | --- | --- |
| The bug ledger above, with every row linked |  | Not yet |
| Each reduced test in the regression suite |  | Not yet |
| The CI workflow and logs of scheduled campaigns, with seeds |  | Not yet |
| The planted-bug branch and the campaign that found each bug |  | Not yet |
| The generator's commit and its coverage report |  | Not yet |

## Sources

[^csmith]: Xuejun Yang, Yang Chen, Eric Eide and John Regehr, "Finding and Understanding Bugs in C Compilers", *PLDI 2011*. <https://doi.org/10.1145/1993498.1993532>
[^emi]: Vu Le, Mehrdad Afshari and Zhendong Su, "Compiler Validation via Equivalence Modulo Inputs", *PLDI 2014*. <https://doi.org/10.1145/2594291.2594334>
[^creduce]: John Regehr, Yang Chen, Pascal Cuoq, Eric Eide, Chucky Ellison and Xuejun Yang, "Test-Case Reduction for C Compiler Bugs", *PLDI 2012*. <https://doi.org/10.1145/2254064.2254104>
[^libfuzzer]: LLVM Project, "libFuzzer – a library for coverage-guided fuzz testing". <https://llvm.org/docs/LibFuzzer.html>
[^testing-guide]: LLVM Project, "LLVM Testing Infrastructure Guide", section on regression tests. <https://llvm.org/docs/TestingGuide.html>
[^lit]: LLVM Project, "lit - LLVM Integrated Tester". <https://llvm.org/docs/CommandGuide/lit.html>
[^filecheck]: LLVM Project, "FileCheck - Flexible pattern matching file verifier". <https://llvm.org/docs/CommandGuide/FileCheck.html>
[^alive2]: Nuno P. Lopes, Juneyoung Lee, Chung-Kil Hur, Zhengyang Liu and John Regehr, "Alive2: Bounded Translation Validation for LLVM", *PLDI 2021*. <https://doi.org/10.1145/3453483.3454030>
[^fuzzing-llvm]: LLVM Project, "Fuzzing LLVM libraries and tools". <https://llvm.org/docs/FuzzingLLVM.html>

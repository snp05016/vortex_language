# How Vortex is tested

<p class="page-intro">This page is the testing policy for the Vortex compiler and its documentation: what each kind of test compares, which techniques the project uses and why, and what continuous integration runs. It separates what exists today from what is planned.</p>

A compiler makes two promises. It accepts every valid program and gives it the
meaning the specification defines, and it rejects every invalid program for
the right reason, at the right place. The
[conformance chapter](../specification/conformance.md#13-implementation-conformance)
states both, and adds that crashing on a construct, or quietly dropping it, is
not conforming. These documents make a third promise: every example they show
is correct. A **test** is a small automatic check of one of those promises,
with a definite pass or fail.

Four rules apply to every test on this page.

1. **Expected results come from the documents.** The specification and the
   decision records say what must happen. The compiler's output is never
   copied into an expected file unread; [stage 0](../compiler/guide/stage-0-workbench.md#traps)
   explains how that would protect the compiler's mistakes.
2. **A test compares only what the documents specify:** exit status, standard
   output, error category and source position. It never compares the wording
   of a message, which the [diagnostics chapter](../specification/diagnostics.md)
   leaves to the implementation.
3. **A test is deterministic and can fail.** It gives the same bytes on every
   run and every machine, and the runner that executes it has been seen to
   report a failure.
4. **Every bug found becomes a permanent test.** Whether a person or a random
   tester finds it, the smallest program that shows the bug joins the suite.

## What is in place today

**Continuous integration** (CI) is a set of jobs that GitHub runs on its own
machines each time a change is pushed or proposed in a pull request. Today
those jobs check the documentation; the compiler has one test and no CI job
yet.

| Area | What is checked | Where | Status |
| --- | --- | --- | --- |
| Code examples on these pages | C++ examples are built and run, and their output compared byte for byte with `.expected` files; LLVM IR, MLIR and assembly go through their tools; CUDA only where `nvcc` is installed; Metal not yet | `tools/docs/check_examples.py`, workflow `examples.yml`, three platforms | In place |
| The site | Strict build, generated content up to date, internal links and anchors | workflows `docs.yml` and `docs-quality.yml` | In place |
| Prose | Vale style rules and codespell on every change to the docs; external links weekly | workflow `docs-quality.yml` | In place |
| Accessibility | pa11y against WCAG 2 AA | `.pa11yci.json` | Configured; no workflow runs it yet |
| Vortex examples on these pages | Every `vortex` block names its kind and expected result | [record 28](../decisions/documentation.md#d28) | Labels decided; checker planned |
| The compiler | One lexer test program, registered with CTest | `CMakeLists.txt`, `tests/` | Started; no workflow runs it |

Vale checks prose against the project's style rules (banned filler and hype
words, no em or en dashes, no exclamation marks, sentence-case headings),
codespell looks for common misspellings, and pa11y checks built pages against
the Web Content Accessibility Guidelines (WCAG 2, level AA). CTest is CMake's
test runner.

**The examples harness.** Every code example on these pages is a file under
`examples/`, and the page includes that file rather than a copy, so the code a
reader sees is the code that was checked. The checker builds each C++ example
with `-std=c++26 -Wall -Wextra -Werror -O2`, runs it, and, when the example
has an `.expected` file, compares its standard output with that file byte for
byte. LLVM IR goes through `opt` and `llc`, MLIR through `mlir-opt`, and
assembly through `cc -c`. An example whose tool is missing on a machine, such
as a CUDA example where `nvcc` is not installed, is skipped with the reason
printed; Metal examples are always skipped, because the checker has no Metal
toolchain yet. A missing C++ compiler or assembler is a failure instead,
because every machine that checks examples must have both.
`examples/README.md` holds the full contract.

The `examples.yml` workflow runs the checker on three GitHub-hosted
runners:[^gh-runners] `macos-latest` (arm64, Apple clang), `ubuntu-24.04-arm`
(arm64) and `ubuntu-24.04` (x86-64), the two Linux runners with `g++-14` and
the LLVM 18 tools. The macOS job uses LLVM tools only when the runner image has them and
otherwise leaves the LLVM and MLIR examples to Linux. All three runners
compare against the same `.expected` file, so an example that prints
different bytes on different machines fails. The x86-64 job also runs the
checker's self-test and fails if the manifest of Compiler Explorer links is
out of date.

**The compiler.** CMake registers one test today, `lexer_tests`, which runs
the lexer test program in `tests/`. The
[architecture page](../compiler/architecture.md#current-implementation-boundary)
describes the same boundary.

Everything else on this page is plan. The plan follows the roadmap, so each
kind of test arrives with the compiler stage it checks and keeps running from
then on: the staircase in
[stage 11](../compiler/guide/stage-11-release.md#the-test-suite-as-a-whole).

## The test pyramid

The **test pyramid** is a picture of how many tests of each kind a project
should have. Martin Fowler, who notes that most people know it from Mike
Cohn's 2009 book *Succeeding with Agile*, sums it up as many more low-level
tests than broad tests that run through the whole system, because broad tests
are slower, costlier to write and more brittle.[^fowler] A broad test that
fails also does not say where the fault is, while a small one points at one
part.

Compiler projects use the same shape. LLVM's testing guide divides its tests
into three kinds: unit tests of single components, written in C++;
regression tests, small input files run through one tool and checked against
expected output; and whole programs, compiled, linked and run.[^llvm-testing]
Vortex uses the same three layers and adds a fourth on top, because its
documents make promises too.

<figure class="vx-figure">
<svg viewBox="0 0 760 400" role="img" aria-labelledby="tp-title tp-desc">
<title id="tp-title">The Vortex test pyramid, with random testing feeding it</title>
<desc id="tp-desc">A pyramid of four layers. From the wide base to the narrow top they are unit tests, golden-file tests, end-to-end run tests and doc tests. The base holds the most tests, each small and fast; the top holds the fewest, each running the whole compiler. On the right, a box for random testing, meaning differential testing, equivalence modulo inputs and fuzzing, is seeded by the existing tests. Each failure it finds goes to a box labelled reduce, and the reduced program enters the pyramid as a new permanent test.</desc>
<text class="vx-text-muted" x="20" y="28">fewer tests, each running the whole compiler</text>
<g class="vx-seq" style="--vx-i: 0; --vx-n: 4">
<polygon class="vx-box" points="72.5,275 427.5,275 470,350 30,350"/>
<text class="vx-text" x="250" y="308" text-anchor="middle">Unit tests</text>
<text class="vx-text-muted" x="250" y="328" text-anchor="middle">one part of the compiler, in C++</text>
</g>
<g class="vx-seq" style="--vx-i: 1; --vx-n: 4">
<polygon class="vx-box" points="115,200 385,200 427.5,275 72.5,275"/>
<text class="vx-text" x="250" y="233" text-anchor="middle">Golden-file tests</text>
<text class="vx-text-muted" x="250" y="253" text-anchor="middle">tokens, trees, diagnostics, IR</text>
</g>
<g class="vx-seq" style="--vx-i: 2; --vx-n: 4">
<polygon class="vx-box" points="157.5,125 342.5,125 385,200 115,200"/>
<text class="vx-text" x="250" y="158" text-anchor="middle">End-to-end runs</text>
<text class="vx-text-muted" x="250" y="178" text-anchor="middle">output and exit status</text>
</g>
<g class="vx-seq" style="--vx-i: 3; --vx-n: 4">
<polygon class="vx-box" points="200,50 300,50 342.5,125 157.5,125"/>
<text class="vx-text" x="250" y="84" text-anchor="middle">Doc tests</text>
<text class="vx-text-muted" x="250" y="104" text-anchor="middle">labelled examples</text>
</g>
<text class="vx-text-muted" x="250" y="380" text-anchor="middle">more tests, each small, fast and pointed at one part</text>
<rect class="vx-box-accent" x="560" y="40" width="180" height="78" rx="4"/>
<text class="vx-text" x="650" y="66" text-anchor="middle">Random testing</text>
<text class="vx-text-muted" x="650" y="86" text-anchor="middle">differential, EMI, fuzzing</text>
<text class="vx-text-muted" x="650" y="104" text-anchor="middle">no stored answer</text>
<rect class="vx-box-strong" x="560" y="175" width="180" height="60" rx="4"/>
<text class="vx-text" x="650" y="201" text-anchor="middle">Reduce</text>
<text class="vx-text-muted" x="650" y="221" text-anchor="middle">to a few lines</text>
<path class="vx-line" d="M357 150 L520 150 L520 79 L552 79"/>
<polygon class="vx-arrowhead" points="552,74 560,79 552,84"/>
<text class="vx-text-muted" x="440" y="142" text-anchor="middle">seed the generators</text>
<path class="vx-flow" d="M650 118 L650 167"/>
<polygon class="vx-arrowhead" points="645,167 650,175 655,167"/>
<text class="vx-text-muted" x="660" y="150">a failure</text>
<path class="vx-flow" d="M560 205 L403 205"/>
<polygon class="vx-arrowhead" points="403,200 395,205 403,210"/>
<text class="vx-text-muted" x="478" y="196" text-anchor="middle">a new permanent test</text>
</svg>
<figcaption>Figure 1. The test pyramid for Vortex. Most tests sit in the wide lower layers, where each test is small, fast and points at one part of the compiler; the narrow upper layers run the whole compiler and hold fewer tests. Random testing (differential testing, equivalence modulo inputs and fuzzing) sits outside the pyramid. The existing tests seed it, and every failure it finds is reduced and added to the lowest layer that can show it, so a bug found once stays found.</figcaption>
</figure>

- **Unit tests** check one part of the compiler through its C++ interface:
  the source manager turning a byte offset into a line and column by
  [record 16](../decisions/diagnostics.md#d16), the lexer meeting one
  malformed literal, the constant evaluator meeting an overflow. LLVM keeps
  its unit tests for support code and generic data structures and prefers
  regression tests for transformations and analyses,[^llvm-testing] and
  Vortex follows the same split.
- **Golden-file tests** run the `vortex` command on a small source file and
  compare its text output (tokens, a tree, a diagnostic, generated code) with
  a stored expectation. Most compiler tests live here.
- **End-to-end run tests** compile, link and run a program, and compare how
  the run ends.
- **Doc tests** compile every labelled `vortex` block on these pages that is
  not a fragment.

**Random testing** sits outside the pyramid. It generates programs and inputs
that nobody wrote by hand, so no stored answer exists for them. Instead it
checks each result against an **oracle**: anything that can tell a right
result from a wrong one, such as a second implementation or the rule that the
compiler never crashes. It finds failures no one thought to test for, and
each one, once reduced, becomes a new test in the lowest layer that can show
it.

## Golden-file tests

A **golden file** is a stored copy of the output a test expects; the test
runs the tool again and compares. There are two ways to compare, and Vortex
uses both.

**Exact comparison** checks every byte. It suits output whose format is
written down, where any change is a real change: the token list from
`vortex --tokens` ([I4](../decisions/implementation.md#i4)), the tree from
`vortex --ast` ([I5](../decisions/implementation.md#i5)), and a program's
standard output ([record 4](../decisions/program.md#d4)). The Rust
compiler's UI tests, which check what the compiler prints, work this way,
keeping each test's whole error output in a file next to the test.[^rustc-ui]

**Pattern comparison** checks only the lines a test is about. In LLVM's
regression tests, each test file carries `RUN:` lines, which tell the lit test
runner how to execute it, and `CHECK:` lines, which the FileCheck tool must
find in the output, in order.[^llvm-testing][^filecheck] A `CHECK-NOT:` line
asserts that a pattern is absent, between the neighbouring matches if there
are any. Patterns suit output that changes for good reasons, such as
generated IR or assembly. A test that strict floating-point mode never emits
a fused multiply-add needs one negative pattern, not a copy of the whole
function, and it keeps passing when an unrelated improvement changes the code
around it. [Stage 6](../compiler/guide/stage-6-first-machine-code.md#traps)
warns about the alternative: printed IR, kept whole as a test, changes
whenever lowering improves.

Two more ideas from lit are worth keeping, whatever runner Vortex ends up
with. lit finds tests by searching directories, so adding a test means adding
a file.[^lit] And each test file is self-contained: the command, the input
and the expectation sit together.

**Regenerating expectations.** LLVM ships scripts that write `CHECK:` lines
from current output,[^llvm-testing] and rustc rewrites its stored error
output with a `--bless` option. The rustc guide still asks that the result be
inspected by hand, and it requires every expected error to be annotated in
the test source as well: a second record that catches a stored file generated
without a second look.[^rustc-ui] The Vortex rule is the same. An expectation
may be regenerated by a command, every changed line is read before it is
committed, and expectations for invalid programs are checked against the
specification, never taken from the compiler.

## What an invalid-program test compares

A **negative test** passes only if the compiler rejects a bad program for the
right reason at the right place. Implementation record
[I3](../decisions/implementation.md#i3) turns "right reason" and "right
place" into a comparison of three things:

- the exit status of `vortex`, which [record 20](../decisions/program.md#d20)
  sets to 1 for any error in the source;
- the category of the error: lexical, syntax, name, type, semantic,
  constant-evaluation or implementation-limit;
- the **primary position**, the line and column where the error's primary
  span (the stretch of source the error is about) starts, counted as
  [record 16](../decisions/diagnostics.md#d16) specifies.

Record 20 adds a fourth check: after an error, no output file is created or
changed. The message text, any notes and any warnings are never compared;
[record 48](../decisions/diagnostics.md#d48) keeps warnings out of every
conformance test.

Here is the example from record 28, written as a complete test file:

```vortex
// program: type error
fn main() {
    let ready: bool = 10;   // an integer literal cannot initialize a bool
}
```

The test expects:

| Check | Expected |
| --- | --- |
| Exit status of `vortex` | 1 |
| Errors, in order | one type error whose primary span starts at line 3, column 23: the literal `10` |
| Output file | none written |

In the expected-file layout that I3 suggests, the error is the single line
`type 3:23`. The runner reads the category and the position from the header
line of each diagnostic, whose layout is
[I2](../decisions/implementation.md#i2), and ignores the rest.

There are three reasons to leave the words out. The diagnostics chapter makes
exact wording an implementation detail, so a test of wording tests something
the specification does not require. Wording is also the part of a diagnostic
most worth improving, and a suite that breaks on every improvement
discourages it. And McKeeman, comparing C compilers with each other, found
even required diagnostics hard to compare, because their form is not
specified.[^mckeeman]

Other compilers choose differently, for their own reasons. Clang's `-verify`
mode reads directives such as `expected-error` from comments in the test file
and checks that each expected diagnostic appears at its line with the expected
text.[^clang-verify] rustc's UI tests compare the whole error output with a
stored file.[^rustc-ui] In both projects the message text is part of what the
tests check. The Vortex specification promises categories and positions
instead, so Vortex tests compare exactly those.

A negative test is only half of a test. The
[diagnostics chapter](../specification/diagnostics.md#107-verification-requirements)
asks for a nearby accepted program beside every rejected one, so that the pair
proves the compiler rejects the broken form and nothing wider:

```vortex
// program: valid
fn main() {
    let ready: bool = true;
}
```

A negative test breaks exactly one rule, so it expects exactly one error. A
recovery test, which breaks several rules on purpose to check that one mistake
does not hide the next, lists one category and position per mistake, in
order. How a compiler recovers after an error is implementation-defined
([record 55](../decisions/documentation.md#d55)), so recovery tests belong to
this compiler's own suite, not to the conformance tests every implementation
must pass.

## End-to-end run tests

An **end-to-end test** compiles a program, links it, runs the executable and
checks how the run ends. The decision records fix the comparison:

| How the run ends | Exit status | Standard output | Standard error |
| --- | --- | --- | --- |
| Normal finish | 0 | compared byte for byte ([record 4](../decisions/program.md#d4)) | empty |
| Runtime error | 101 | compared byte for byte, up to the error | one line: its kind and position are compared, its message is not ([record 14](../decisions/program.md#d14)) |
| Rejected by the compiler | 1, from `vortex`; nothing runs | nothing | compared as for a negative test |

The first such test is the
[Milestone 6](../roadmap.md#milestone-6-basic-cpu-code-generation) program,
which prints `14` and exits with 0.
[Milestone 9](../roadmap.md#milestone-9-runtime-safety) adds a test for each
runtime check and for each boundary value that must not fail, and
[Milestone 10](../roadmap.md#milestone-10-matrix-multiplication) compares
matrix multiplication with a
[known answer](../compiler/guide/stage-10-matrix-multiplication.md#comparing-with-a-known-answer).

Floating point needs no tolerance here.
[Record 56](../decisions/numbers.md#d56) makes each floating-point operation
one IEEE 754 operation, rounded to nearest with ties to even, and forbids
contraction, reordering, wider evaluation and flushing subnormal values to
zero.
[Record 4](../decisions/program.md#d4) prints a float as the shortest decimal
that reads back as the same value, and prints `-0.0` apart from `0.0`. So one
expected-output file holds for every conforming compiler on every target, and
comparing output byte for byte compares every printed value bit for bit. The
one exception is NaN, whose sign and payload `print` does not show.

## Doc tests from labelled examples

A **doc test** compiles an example from the documentation and checks it
against what the page says about it. rustdoc does this for Rust: it runs the
code blocks in documentation as tests, wraps a block that has no `main` in
one, and understands markers such as `compile_fail` (the block must not
compile) and `should_panic` (it must compile, then fail when it runs).[^rustdoc]
A `compile_fail` block passes on any compile error, so the marker does not say
which error is expected.

Vortex examples say more. Under [record 28](../decisions/documentation.md#d28),
the first line of every `vortex` block is a label naming its kind and its
expected result. A checker completes each block the same way every time: it
appends `fn main() {}` to an `items` block that has no `main`, wraps a
`statements` block in `fn main() { ... }`, and never compiles a `fragment`.
Then it checks the result:

| Label result | The test passes when |
| --- | --- |
| `valid` | the compiler accepts the completed block (exit status 0); the program is not run |
| an error category, such as `type error` | the compiler rejects the block (exit status 1), and its first error has that category |
| `runtime error` | the block compiles, and the run ends with exit status 101 and a `runtime error[` line |
| `planned` | the compiler rejects the block, with any category ([record 50](../decisions/documentation.md#d50)) |

The rules come from [I10](../decisions/implementation.md#i10). The checker
compares no positions: a label records none, and wrapping a `statements`
block moves every line down, so line-and-column checks belong in the
compiler's own golden-file tests. It compares only the first error, because
recovery after an error is implementation-defined.

The labels are decided; the checker is planned, and I10 runs it at the
release gate. One gap remains. A page that shows a program's output makes a
promise about that output too, as the comments in
[record 4](../decisions/program.md#d4)'s examples do, but the checker does
not run `valid` blocks, so that output goes unchecked. The C++ examples
already keep their output in `.expected` files that the harness compares.
Closing the gap for Vortex examples, by moving them into `examples/` or by
teaching the checker to read output written in comments, is still open.

## Tests for unfinished work

Two kinds of "not yet" need opposite tests, and
[record 50](../decisions/documentation.md#d50) gives them different names.

- **Planned** behavior is not part of v0.1. Its test expects rejection, and it
  passes today.
- **Specified, not yet implemented** behavior is part of v0.1 but not built
  yet. Its test is an **expected failure**: marked to fail, and reported apart
  from real failures ([I9](../decisions/implementation.md#i9)). The mark sits
  in the test's own expected file and names the stage that will make it pass,
  such as `xfail: stage 8`.

LLVM's lit shows how to keep the second kind honest. It reports a marked test
that fails as XFAIL and a marked test that passes as XPASS, and it counts
XPASS as a failure of the run.[^lit] The mark cannot outlive the missing
feature: the day the feature works, the suite fails until someone removes the
mark. At the release gate, each mark still present is listed as a known
limitation.

Vortex adds one condition. An expected failure must fail in the documented
way, with an implementation-limit diagnostic at the construct the compiler
cannot handle yet. The
[diagnostics chapter](../specification/diagnostics.md#implementation-limit-error)
forbids using that category to disguise a crash, and a crash is not an
acceptable expected failure either. McKeeman's rule holds for every test on
this page: "A crash is never the right answer."[^mckeeman]

## Differential testing

A stored expectation exists only for a program somebody has thought about.
For a new program there is no expected answer, and working one out is the
expensive part of testing. McKeeman calls evaluating the result of a test
"the ugliest problem in testing" and concludes that an oracle is
needed.[^mckeeman]

**Differential testing** replaces the oracle with a comparison. McKeeman's
1998 paper, drawn from testing compilers at Digital, defines it: give the same
generated test to two or more comparable systems, and treat any difference in
their results, or a crash or endless loop in one of them, as a candidate
bug.[^mckeeman] Nobody needs to know the right answer, because the systems
check each other. With three or more implementations, a majority vote
suggests which one is wrong.[^csmith]

The paper also names the costs. Two correct systems may still differ wherever
the language leaves a choice open, and C leaves many. The generated tests must
be good enough to reach deep into the compiler. And a failing random test is
long and noisy, so it must be shrunk before a person can use it.[^mckeeman]
The next sections take these problems one at a time.

Vortex is well placed for differential testing. The conformance chapter rules
out [undefined behavior](../specification/conformance.md#15-undefined-behavior)
in a well-formed program, and
[host and target choices](../specification/conformance.md#16-implementation-defined-behavior)
must not change a program's meaning.
[Record 39](../decisions/diagnostics.md#d39) makes every implementation accept
the same programs, even when a runtime check is certain to fail, and record 56
fixes floating-point results to the bit. The behaviors an implementation may
choose are listed in one place
([record 55](../decisions/documentation.md#d55)), and a test that depends on
one of them must say so. Outside that list, any difference in what a test
compares is a bug.

The comparable pairs, in the order they become available:

| Compare | Available from | A difference means |
| --- | --- | --- |
| The same test on each CI platform the compiler supports | Milestone 1 for diagnostics, Milestone 6 for runs | the compiler, runtime or back end depends on the platform |
| The compiler built by two C++ compilers | Milestone 0 | the compiler's own C++ relies on behavior its language leaves open |
| A kernel and an independent C++ reference | Milestone 10 | a bug in the compiled kernel, or in the reference |
| Unoptimized and optimized builds | the first optimization, after v0.1 | an optimization changed a program's meaning |
| The v0.1 back end and a second back end | after v0.1 ([B1](../backend/b1-simplest-backend.md)) | a bug in one of the two |

A C++ reference must follow the same floating-point rules as Vortex. It is
built with `-ffp-contract=off` and without fast-math options, because both
compilers in CI fuse multiplies and adds by default: Clang within one
statement,[^clang-fp] and GCC, which the Linux runners use, across statements
in C++.[^gcc-fp] The rest follows from IEEE 754 itself. As Goldberg's survey
explains, the standard requires the basic operations to be exactly rounded,
so the same operations on the same values, in the same order, give the same
bits on every machine that implements it.[^goldberg]

A difference is sorted before anyone reads it, the way McKeeman's harness
sorted results into crashes, endless loops, abnormal ends and different
output, and discarded a test when a comparison compiler crashed or
looped.[^mckeeman] Each candidate is then reduced (see below). The
[differential testing case study](case-studies/differential-testing.md) lists
every path it compares and keeps the ledger of what was found.

## Random program generation

Differential testing needs a steady supply of new programs, and a **random
program generator** writes them. What the programs can find depends on how
valid they are. McKeeman built C test inputs at seven levels, each obeying
more of the language's rules than the last, and observed how deep into the
compiler each level reached.[^mckeeman] For Vortex, the levels line up with
the phases:

| Level | The input is | It exercises |
| --- | --- | --- |
| 1 | any sequence of characters | the lexer and its errors |
| 2 | a sequence of valid tokens | the parser and syntax errors |
| 3 | a syntactically valid program | name resolution |
| 4 | a program whose names resolve and whose types check | the semantic and constant-evaluation checks |
| 5 | a program that passes every static rule | code generation: it compiles and runs |
| 6 | a program that also passes every runtime check | a whole run, with output to compare |
| 7 | a program shaped like real code, such as loop nests over fixed-shape arrays | the optimizer, where it works hardest |

Levels 1 to 4 test that the compiler rejects bad input cleanly, which is the
job of fuzzing. Levels 5 to 7 feed differential testing.

**Csmith**, a generator of random C programs, works at levels 6 and 7. Its
authors, Yang, Chen, Eide and Regehr, used it for three years and reported
more than 325 previously unknown bugs in C compilers, GCC and LLVM among them;
every compiler they tested both crashed and silently generated wrong code on
some valid input.[^csmith] Two design choices matter here. Each generated
program prints a checksum of its global variables, so a wrong value in any of
them reaches the output. And each program has exactly one meaning, which in C
means avoiding all 191 kinds of undefined behavior and 52 kinds of
unspecified behavior in the C99 standard. Csmith does this by construction where it can,
and otherwise with static analysis and run-time guards, such as wrapper
functions around arithmetic that could overflow.[^csmith]

The paper is candid about the limits. Csmith aims at the optimizer, every
program it writes passes the front end, it is poor at finding gaps in
standards conformance, and the version the paper describes generates no
floating-point code.[^csmith] YARPGen, a later generator, goes deeper into
loops. Its 2023 version avoids undefined behavior in the loops it generates
by reasoning about them statically, adds ways to make loop code more varied,
and found 122 bugs in C++ compilers and in compilers for data-parallel
languages.[^yarpgen]

A Vortex generator starts from firmer ground. There is no undefined behavior
to avoid. A program that divides an integer by zero at run time keeps its
meaning: it stops with a `divide-by-zero` runtime error at a position every
implementation must report the same way
([record 14](../decisions/program.md#d14)), and record 39 decides which such
failures are caught during compilation instead. The generator still steers
away from runtime errors, for a reason McKeeman gives: a large share of his
level-5 C programs stopped abnormally, usually on a division by zero, and
each of those tests was wasted.[^mckeeman] Floating
point is in scope from the start, because record 56 fixes every result. And
the programs most worth generating are the ones the roadmap cares about,
fixed-shape arrays and loop nests like the Milestone 10 matrix multiplication,
where the [optimization book](../optimize/index.md) applies its
transformations.

Every generated program must be reproducible. A generator is driven by a
**seed**, the number that starts its random choices, and the test log records
the seed and the generator's version for every program, so any failure can be
generated again exactly. As the C-Reduce paper observes of Csmith, a generated
program is completely determined by the sequence of choices the generator
makes.[^creduce]

A random tester is itself a program that can be wrong, so it needs the check
the [stage 0 runner](../compiler/guide/stage-0-workbench.md#a-runner-that-has-failed-at-least-once)
needs: plant a known bug in one pass, and confirm that the tester finds it
within its usual budget. The
[case study](case-studies/differential-testing.md) describes this check.

## Equivalence modulo inputs

Differential testing needs two implementations. **Equivalence modulo inputs**
(EMI) needs one compiler and one program. Le, Afshari and Su observed that a
program run on some inputs never executes part of its code, so deleting that
code gives a different program that must behave identically on those
inputs.[^emi] The compiler cannot know which code went unused: it still has
to analyze and optimize each variant for every possible input, and the
variants' control flow and data flow differ, so they send the optimizer down
different paths while demanding the same output. Their tool, Orion, profiles
a program's runs and randomly deletes statements that never ran. In eleven
months it produced 147 confirmed, unique bug reports for GCC and LLVM, most of
them wrong-code bugs.[^emi]

EMI fits Vortex well. A v0.1 program has no way to read input, so it has
exactly one run, and every end-to-end test becomes a source of variants. Two
Vortex-specific rules apply. A variant must still pass every static rule:
deleting a `let` whose name is used later, or a `return` that a function needs
on some path, gives a program the front end rejects, and such variants are
discarded before any comparison. And EMI needs to know which statements ran,
which requires a way to count statement executions in a test build. EMI is
planned with the first optimizations, since the optimizer is what it tests.

## Test-case reduction

A random failure arrives as a program hundreds of lines long, most of it
irrelevant. **Test-case reduction** shrinks it to the smallest program that
still fails. McKeeman's harness applied 23 shortening transformations, such
as removing a statement or changing a constant to 1, and kept each change
after which the failure remained. It typically turned generated C programs of
500 to 600 lines into programs of a few lines, and runs of 10,000 or more
compilations were not unusual:[^mckeeman] machine time, spent to save a
person's time.

Regehr and colleagues showed that for C, generic reduction is not
enough.[^creduce] **Delta debugging**, the existing method at the time,
deletes chunks of text and keeps whatever still fails. On C programs it
stopped at results that were too large, or invalid: the smaller program
relied on undefined behavior, so the difference it showed was no longer
evidence of a compiler bug. Their
tool, C-Reduce, runs a set of pluggable transformations, many of them
compiler-like, until none of them makes progress, and keeps a variant only if
it still shows the behavior of interest and is still a valid test. Its results
were on average more than 25 times smaller than those of the other reducers
they compared, including the one compiler developers used most.[^creduce]

For a Vortex wrong-code bug, the check that decides whether to keep a smaller
variant has three parts:

1. the front end still accepts the variant;
2. the reference path still runs it to a normal finish, so the variant has
   not traded the bug for a runtime error;
3. the two paths still disagree.

For a compiler crash, part 3 becomes "the compiler still crashes in the same
place". Because Vortex has no undefined behavior, parts 1 and 2 are decided
by running the compiler and the program, which is exactly what a C reducer
cannot do.

A person then reads the reduced program, gives it its expected result from
the specification and the reference, and adds it at the lowest layer that
shows the bug: a golden-file test if the compiler's output shows it, an
end-to-end test if it takes a run. If Vortex emits LLVM IR, a bug below that
line can be shrunk with `llvm-reduce`, which LLVM's testing guide names as a
tool that partly automates minimizing tests.[^llvm-testing]

## Coverage-guided fuzzing

A **fuzzer** feeds a program a stream of generated inputs and watches for
crashes. A **coverage-guided** fuzzer also records which code each input
reaches and keeps the inputs that reach something new, so the stream drifts
toward unexplored code. libFuzzer, part of LLVM, works this way. It is linked
into the program under test, calls one entry function with each input, and
mutates a **corpus** of saved inputs, keeping a mutation when it covers new
code.[^libfuzzer] Its documentation suggests seeding the corpus with varied
valid and invalid inputs, and notes that the corpus then doubles as a
regression check.

For Vortex, the first fuzz targets are the stages whose input is arbitrary
text: the lexer and the parser, then the name and type checks. The oracle is
short and strict. For any input, the compiler either accepts it or rejects it
with a diagnostic in one of the compile-time categories, at a position inside
the file; it never crashes, hangs or trips a sanitizer (a run-time check
compiled into a test build of the compiler, described below). The seed corpus
is every test program and every labelled doc example, which gives valid and
invalid inputs for every rule.

Two practical notes. libFuzzer's original authors have stopped active work on
it; important bugs still get fixed, but major new features are not
expected.[^libfuzzer] And Apple clang 21 on the owner's Mac has no libFuzzer
runtime: linking a target built with `-fsanitize=fuzzer` fails because the
runtime library is missing (checked on 2026-09-23). Fuzzing is therefore
planned as a scheduled, time-limited job on Linux, with a toolchain whose
runtime is present.

**Sanitizers** are compiler options that add run-time checks to a program,
turning silent memory and arithmetic errors into loud ones, and a sanitized
build of the compiler makes every existing test stricter. AddressSanitizer
reports out-of-bounds accesses and use after free, and its documentation
gives a typical slowdown of 2x; UndefinedBehaviorSanitizer catches undefined
behavior such as signed integer overflow at a small run-time
cost.[^asan][^ubsan] The whole suite, not only the fuzzers, will run against a
build with both. Apple clang 21 builds and runs programs with both sanitizers
on the owner's Mac (checked on 2026-09-23), so the same build also works
locally.

## Bitwise identity for optimizations

v0.1 has no optimizer, but the rule for when one arrives is already fixed, and
this section records how it will be tested.

An optimization may change how fast a program runs, never what it does. The
conformance chapter says
[optimization strategy](../specification/conformance.md#16-implementation-defined-behavior)
must not change a program's meaning, and
[types and values 4.4](../specification/types-and-values.md#44-floating-point-values)
forbids contracting, reordering or widening floating-point operations,
allowing relaxed modes only as an explicit opt-in in a future version. For a
test, "what the program does" means:

- the same bytes on standard output, which by record 4 means the same bits
  for every printed value;
- the same exit status;
- for a run that fails a runtime check, the same kind at the same position.
  An optimization must not remove a check that can fail, or move the failure
  somewhere else.

The comparison is exact, and it covers everything at once. Every end-to-end
test and every generated program runs unoptimized and optimized, and any
difference fails. A new pass is gated the same way: the suite and a fixed set
of generated programs run with the pass on and off, and one differing byte
blocks it. Two things make the gate meaningful. Test
programs print everything they compute, for the reason Csmith prints a
checksum: a wrong value that is never printed is one no test can
see.[^csmith] And the inputs must be able to show a difference. Small whole
numbers make every sum exact, so they give the same bits in any order; gate
inputs also include values whose sums round, as
[How Vortex performance is measured](measuring.md#bitwise-identity-where-the-ladder-promises-it)
explains for its timing gate.

Some transformations change results by design. A fused multiply-add rounds
once where a multiply followed by an add rounds twice, and splitting a sum
across threads adds in a different order. Strict mode allows neither. The
[CPU matmul ladder](case-studies/cpu-matmul-ladder.md) is built around the
difference: it marks each rung as keeping the bits or changing them, and
every rung that promises to keep them passes this gate before it is timed, up
to the optimization book's [capstone](../optimize/p16-capstone.md). If a
later version adds an explicit relaxed mode, its tests compare with the
strict result within a tolerance chosen before the run, name the permission
that allows the difference, and never replace the exact test of strict mode.

Two habits from LLVM's testing guide apply to optimization work. Commit a new
test with its current, baseline expectations first and the change second, so
the effect of the change shows up as a diff in the test. And run the fewest
passes that show the effect, rather than the whole pipeline.[^llvm-testing]

## The continuous-integration matrix

Today's workflows all check the documentation:

| Workflow | Runners | Runs when | What it checks |
| --- | --- | --- | --- |
| `examples.yml` | `macos-latest`, `ubuntu-24.04-arm`, `ubuntu-24.04` | a push or pull request touches `examples/`, the checker or the workflow | every example builds, runs and prints its expected output; the checker's self-test and manifest, on x86-64 Linux |
| `docs-quality.yml` | `ubuntu-latest` | a pull request, or a push to `main`, touches the docs; weekly | Vale, codespell, and a strict build with every internal link and anchor checked; external links weekly |
| `docs.yml` | `ubuntu-latest` | a push to `main` touches the docs | generated content is current; a strict build; publishing |

The compiler's matrix is planned. It reuses the three runners and the
toolchain setup that `examples.yml` already proves, including `g++-14` on
Linux, which the workflow installs because Ubuntu 24.04's default `g++` is
version 13, with neither `-std=c++26` nor `<print>`.

| Job | Runs when | Runners | What it runs |
| --- | --- | --- | --- |
| Build and test | every push and pull request | all three | the [Milestone 0](../roadmap.md#milestone-0-project-foundation) command: build, then every unit, golden-file, end-to-end and doc test |
| Sanitizers | every push and pull request | one Linux runner | the same suite, against a compiler built with AddressSanitizer and UndefinedBehaviorSanitizer |
| Second compiler | every push and pull request | one Linux runner | the compiler built with a second C++ compiler, and the two builds' results compared |
| Benchmark gate | every push and pull request | one runner | the benchmark harness's correctness gate and a short smoke run; no timings kept |
| Fuzzing | on a schedule, time-limited | Linux | the lexer, parser and checker targets, seeded from the suite |
| Random differential | on a schedule | Linux and macOS, for the paths each supports | generated programs through every available pair; failures reduced and reported |
| Coverage | on a schedule | one runner | which lines of the compiler the suite reaches, from Clang's source-based coverage[^coverage] |

Four policies go with the matrix.

- **Per-change jobs are deterministic.** They use fixed programs and fixed
  seeds, so a new failure points at the change under test. Exploration with
  new seeds runs on a schedule and reports what it finds; it never blocks an
  unrelated change.
- **A flaky test is a failing test.** Fowler notes that end-to-end tests are
  more prone to nondeterminism, which undermines trust in them.[^fowler] A
  test that sometimes fails is fixed or removed, not retried until it passes.
- **Every test has a time limit.** A hang is a failure (McKeeman's harness
  filed endless loops as their own category), never a stuck job. The examples
  checker already stops any command that runs longer than 120 seconds.
- **CI checks correctness, not speed.** Apart from the single-CPU kind, each
  GitHub-hosted runner is a new virtual machine, and the standard runners are
  free and unlimited for public repositories.[^gh-runners] That suits
  correctness checks, but not timing, because the hardware under a job is
  neither chosen nor recorded. So the benchmark gate keeps no timings. It
  runs the harness's correctness gate, which checks each variant's output
  against the reference, and a short smoke run that shows the harness still
  works. Timings come from a recorded machine, by the method in
  [How Vortex performance is measured](measuring.md) and the
  [benchmark harness case study](case-studies/benchmark-harness.md).

GPU code is tested in two parts. Tests of generated GPU code in text form,
such as Metal Shading Language source, PTX (NVIDIA's virtual assembly
language) or disassembled SPIR-V, are golden-file tests and run in CI like any
other. Running GPU code needs a GPU, and GitHub lists GPU-powered
runners only among its larger runners, which it offers to organizations and
enterprises on its Team and Enterprise Cloud plans.[^gh-runners] GPU execution
tests therefore run on the owner's Mac or on a rented GPU, and compare with
the CPU reference as the
[GPU matmul ladder case study](case-studies/gpu-matmul-ladder.md) does: bit
for bit where the floating-point settings and the order of additions match
the reference; otherwise the result reports its largest error and the opt-in
that allows it.

## Adding a test

- Write the expectation from the documents first, then run the compiler
  ([stage 0](../compiler/guide/stage-0-workbench.md#why-the-tests-come-first)).
- Give every negative test its nearby accepted program.
- Fix a bug together with a test that fails before the fix and passes after
  it, reduced, in the lowest layer that shows the bug.
- Keep tests independent, so that each runs alone and in any order.
- Use the narrowest comparison that proves the point: exact output where the
  format is written down, a pattern where the output may change for good
  reasons.
- If a test depends on the platform, or on a behavior an implementation may
  choose, such as the stack size, say so in the test
  ([record 55](../decisions/documentation.md#d55)), as the examples'
  `platforms` key does, instead of letting it fail elsewhere.

## Where each technique is taught

| Technique | Where it is taught |
| --- | --- |
| A test runner, valid and invalid programs, expected failures | [Stage 0](../compiler/guide/stage-0-workbench.md) |
| Valid and invalid pairs, compiling every example, the release gate | [Stage 11](../compiler/guide/stage-11-release.md) |
| Differential testing of a new back end against the v0.1 path | [B1](../backend/b1-simplest-backend.md) |
| FileCheck on assembly and machine IR, encoding tests, differential testing | [E4](../backend/e4-testing-backends.md) |
| Golden tests, differential testing, random programs, EMI, reduction | [O12](../optimize/o12-testing-optimizers.md) |
| Which transformations keep the floating-point bits | [P11](../optimize/p11-floating-point.md), [CPU matmul ladder case study](case-studies/cpu-matmul-ladder.md) |
| Measuring speed, which tests do not do | [P1](../optimize/p1-measure-first.md), [How Vortex performance is measured](measuring.md) |
| Checking GPU kernels against the CPU reference | [GPU matmul ladder case study](case-studies/gpu-matmul-ladder.md), [G10](../gpu/g10-matmul-ladder.md) |
| Measuring GPU code | [G14](../gpu/g14-measuring-gpu-code.md) |
| The GPU path that GPU tests will attach to | [M12](../mlir/m12-vortex-gpu-path.md) |
| The differential-testing and fuzzing campaign and its results | [Case study A3](case-studies/differential-testing.md) |

## Sources and further reading

[^fowler]: Martin Fowler, "Test Pyramid", martinfowler.com, 1 May 2012. <https://martinfowler.com/bliki/TestPyramid.html>
[^llvm-testing]: LLVM Project, "LLVM Testing Infrastructure Guide", sections "Regression tests", "Generating assertions in regression tests", "Precommit workflow for tests" and "Best practices for regression tests". <https://llvm.org/docs/TestingGuide.html>
[^lit]: LLVM Project, "lit - LLVM Integrated Tester", sections "Test discovery" and "Test status results". <https://llvm.org/docs/CommandGuide/lit.html>
[^filecheck]: LLVM Project, "FileCheck - Flexible pattern matching file verifier". <https://llvm.org/docs/CommandGuide/FileCheck.html>
[^clang-verify]: Clang Project, "Clang CFE Internals Manual", section "Verifying Diagnostics". <https://clang.llvm.org/docs/InternalsManual.html#verifying-diagnostics>
[^rustc-ui]: Rust Project, *Rust Compiler Development Guide*, "UI tests", sections "Output comparison" and "Error annotations". <https://rustc-dev-guide.rust-lang.org/tests/ui.html>
[^rustdoc]: Rust Project, *The rustdoc book*, "Documentation tests". <https://doc.rust-lang.org/rustdoc/write-documentation/documentation-tests.html>
[^mckeeman]: William M. McKeeman, "Differential Testing for Software", *Digital Technical Journal* 10(1), 1998, pp. 100-107. Course-archive copy: <https://www.cs.tufts.edu/~nr/cs257/archive/bill-mckeeman/DifferentailTesting.pdf>
[^csmith]: Xuejun Yang, Yang Chen, Eric Eide and John Regehr, "Finding and Understanding Bugs in C Compilers", *PLDI 2011*. <https://doi.org/10.1145/1993498.1993532>; author's copy: <https://users.cs.utah.edu/~regehr/papers/pldi11-preprint.pdf>
[^emi]: Vu Le, Mehrdad Afshari and Zhendong Su, "Compiler Validation via Equivalence Modulo Inputs", *PLDI 2014*. <https://doi.org/10.1145/2594291.2594334>; author's copy: <https://web.cs.ucdavis.edu/~su/publications/emi.pdf>
[^creduce]: John Regehr, Yang Chen, Pascal Cuoq, Eric Eide, Chucky Ellison and Xuejun Yang, "Test-Case Reduction for C Compiler Bugs", *PLDI 2012*. <https://doi.org/10.1145/2254064.2254104>; author's copy: <https://users.cs.utah.edu/~regehr/papers/pldi12-preprint.pdf>
[^yarpgen]: Vsevolod Livinskii, Dmitry Babokin and John Regehr, "Fuzzing Loop Optimizations in Compilers for C++ and Data-Parallel Languages", *Proceedings of the ACM on Programming Languages* 7(PLDI), 2023. <https://doi.org/10.1145/3591295>; copy in the YARPGen repository: <https://github.com/intel/yarpgen/blob/main/papers/yarpgen-pldi-2023.pdf>
[^libfuzzer]: LLVM Project, "libFuzzer - a library for coverage-guided fuzz testing". <https://llvm.org/docs/LibFuzzer.html>
[^asan]: Clang Project, "AddressSanitizer". <https://clang.llvm.org/docs/AddressSanitizer.html>
[^ubsan]: Clang Project, "UndefinedBehaviorSanitizer". <https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html>
[^coverage]: Clang Project, "Source-based Code Coverage". <https://clang.llvm.org/docs/SourceBasedCodeCoverage.html>
[^clang-fp]: Clang Project, "Clang Compiler User's Manual", option `-ffp-contract`. <https://clang.llvm.org/docs/UsersManual.html#cmdoption-ffp-contract>
[^gcc-fp]: GCC Project, *Using the GNU Compiler Collection*, "Options That Control Optimization", option `-ffp-contract`. <https://gcc.gnu.org/onlinedocs/gcc/Optimize-Options.html#index-ffp-contract>
[^goldberg]: David Goldberg, "What Every Computer Scientist Should Know About Floating-Point Arithmetic", *ACM Computing Surveys* 23(1), March 1991; edited reprint in Oracle's *Numerical Computation Guide*. <https://docs.oracle.com/cd/E19957-01/806-3568/ncg_goldberg.html>
[^gh-runners]: GitHub, "GitHub-hosted runners reference". <https://docs.github.com/en/actions/reference/runners/github-hosted-runners>

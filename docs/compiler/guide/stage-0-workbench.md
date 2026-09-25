# 0. The workbench

<p class="page-intro">Before the compiler understands a single character of Vortex, it needs a command you can run, a build you can repeat, and a test runner that can tell you when something is wrong.</p>

This stage produces a compiler that does nothing. That sounds like a strange
place to start, but it is the right one. Everything later in this guide is a
claim of the form "the compiler now accepts this program and rejects that one".
A claim like that is only worth something if a machine checks it every time
you change the code.

So stage 0 builds the bench you will work at: the command that later becomes
the compiler, the build that turns your source code into that command, and the
test runner that feeds it Vortex programs and compares what happens with what
should happen. None of this appears in the mountain drawing on the
[overview page](index.md#the-shape-of-the-whole-thing). It is the floor the
mountain stands on.

The [roadmap](../../roadmap.md#milestone-0-project-foundation) calls this
Milestone 0, "Project foundation". It is short, and it is easy to rush. Most of
the traps on this page come from rushing it.

--8<-- "includes/remember/compiler__guide__stage-0-workbench.md"

!!! goals "In this stage"

    - Build a `vortex` driver that accepts a source path and reports a usage
      error for anything else.
    - Return the exit statuses from the command-line decision, which the test
      runner and scripts depend on.
    - Set up a build that works the same way from a clean checkout every time.
    - Write a test runner that finds its own test cases and reports a verdict
      for each.
    - Prove the runner can fail, on purpose, before trusting it to say pass.

## What this stage is for

The job of this stage is to make every later stage checkable. When you finish,
you should be able to type one command and learn, within a minute or so,
whether the compiler still does everything it did yesterday.

There are three pieces. The first is a **driver**: the program the user
actually runs, called `vortex`, which takes the path of a Vortex source file.
Its full command line, `vortex <source> [-o <output> | --tokens | --ast]`, is
fixed in the [command-line decision](../../decisions/program.md#d20); this
stage needs only the path. For now it reads nothing and translates nothing.
The second is a repeatable **build**: a fixed recipe that turns the
compiler's own source code into the `vortex` program, the same way on every
machine and from a clean start. The third is a **test runner** with folders of
example programs and the results they are expected to produce.

The driver's exit status is how every script and test runner learns what
happened, so different outcomes get different numbers. That is an old
convention, not a Vortex invention. The POSIX `grep` command ends with 0 when
it found a match, 1 when it found none, and a larger status when an error
stopped it.[^grep] The example below follows the same rule in a tiny text
search, which is not a compiler: each pretend command line earns a status that
says which of the three things happened.

--8<-- "includes/examples/build-v0.1/stage-0-workbench/usage_and_exit.cpp.md"

??? check "Why does the driver need a distinct exit status for a usage mistake, rather than reusing the status for a source-code error?"

    A script or a test runner that only sees "nonzero" cannot tell "the
    command line was wrong" from "the program was rejected". Separate
    statuses let a caller react differently: retry with corrected arguments,
    or go read the diagnostic.

## Words for this stage

driver
: The program a person runs to use the compiler. It reads the command line,
  runs the stages in order, and reports the outcome. For Vortex it is the
  `vortex` command.

build
: The process that turns the compiler's own source code into the `vortex`
  program. A **build system** is the tool that knows the recipe.

clean configuration
: A build that starts from nothing: no files left over from an earlier build,
  no settings that exist only on one person's machine.

exit status
: A small number every program hands back to whoever started it when it
  finishes. By long convention zero means success and anything else means
  failure. Test runners and build scripts read it.

test
: One small, automatic check with a definite answer: pass or fail.

test case
: The material for one test. For a compiler this is usually one Vortex source
  file together with a description of what should happen to it.

valid program
: A Vortex program the specification says must be accepted.

invalid program
: A program the specification says must be rejected. It is written on purpose
  to break exactly one rule.

expected output
: A written-down record of what should happen when the compiler meets a test
  case: the program's printed output, or the kind and location of the error.

test runner
: A program that finds all the test cases, runs the compiler on each one,
  compares what happened with the expected output, and reports a pass or a
  fail for each.

regression
: Something that used to work and has stopped working. Running every old test
  after every change is how you catch one.

## The bench as a loop

The three pieces fit together into a loop that you will go round hundreds of
times. You change the compiler. One command rebuilds it and runs every test.
The runner reads the test folders, runs `vortex` on each program, and prints a
verdict. You read the verdict and change the compiler again.

<figure class="vx-figure">
<svg viewBox="0 0 760 330" role="img" aria-labelledby="bench-title bench-desc">
<title id="bench-title">The workbench loop</title>
<desc id="bench-desc">One command builds the compiler and then runs the test runner. The runner reads three folders, valid programs, invalid programs and expected output, runs the vortex command on each program, and prints pass or fail for each test plus a summary. The loop returns to editing the compiler.</desc>
<rect class="vx-box-strong" x="30" y="40" width="150" height="56"/>
<text class="vx-text" x="105" y="66" text-anchor="middle">You change</text>
<text class="vx-text" x="105" y="84" text-anchor="middle">the compiler</text>
<rect class="vx-box" x="220" y="40" width="150" height="56"/>
<text class="vx-text" x="295" y="66" text-anchor="middle">One command</text>
<text class="vx-text-muted" x="295" y="84" text-anchor="middle">build, then test</text>
<rect class="vx-box" x="410" y="40" width="150" height="56"/>
<text class="vx-text" x="485" y="66" text-anchor="middle">Build</text>
<text class="vx-text-muted" x="485" y="84" text-anchor="middle">produces vortex</text>
<rect class="vx-box-accent" x="410" y="160" width="150" height="56"/>
<text class="vx-text" x="485" y="186" text-anchor="middle">Test runner</text>
<text class="vx-text-muted" x="485" y="204" text-anchor="middle">runs vortex on each</text>
<rect class="vx-box" x="600" y="130" width="130" height="34"/>
<text class="vx-mono" x="665" y="152" text-anchor="middle">valid/</text>
<rect class="vx-box" x="600" y="172" width="130" height="34"/>
<text class="vx-mono" x="665" y="194" text-anchor="middle">invalid/</text>
<rect class="vx-box" x="600" y="214" width="130" height="34"/>
<text class="vx-mono" x="665" y="236" text-anchor="middle">expected/</text>
<rect class="vx-box-strong" x="220" y="160" width="150" height="56"/>
<text class="vx-text" x="295" y="186" text-anchor="middle">Report</text>
<text class="vx-text-muted" x="295" y="204" text-anchor="middle">pass or fail, each test</text>
<path class="vx-flow" d="M180 68 L220 68"/>
<path class="vx-flow" d="M370 68 L410 68"/>
<path class="vx-flow" d="M485 96 L485 160"/>
<path class="vx-flow" d="M410 188 L370 188"/>
<path class="vx-flow" d="M220 188 L105 188 L105 96"/>
<path class="vx-line" d="M600 147 L580 147 L580 188 L560 188"/>
<path class="vx-line" d="M600 189 L580 189"/>
<path class="vx-line" d="M600 231 L580 231 L580 188"/>
<text class="vx-text-muted" x="665" y="120" text-anchor="middle">test folders</text>
<text class="vx-text-muted" x="30" y="290">The summary line at the end is the part you read first:</text>
<text class="vx-mono" x="30" y="312">41 passed, 1 failed</text>
</svg>
<figcaption>Figure 1. The workbench loop. The dashes show the order things happen in. The test folders are read on every run, so a test added once keeps checking the compiler for the rest of the project.</figcaption>
</figure>

The loop is only useful if it is short and trusted. Short means one command,
not a list of steps you have to remember. Trusted means that "all tests pass"
actually tells you something, which is harder than it sounds. A runner that
cannot fail will always report success.

??? check "Your runner prints a verdict for every test but always ends with exit status 0, even when some verdicts are FAIL. A script runs it after every change. What can the script not tell?"

    Whether anything failed. The script reads only the exit status, so a run
    with ten failures looks the same to it as a clean run; the verdicts exist
    only on a screen nobody may be watching. The runner cannot be trusted until
    its exit status is failing whenever any test fails.

## What goes in a test case

A compiler test has two halves: a program, and a statement of what the compiler
should do with it. The roadmap asks for three folders to hold these: one for
valid programs, one for invalid programs, and one for expected output. How you
lay out the files inside them is your decision. What each kind of test must say
is not.

A valid program's test says "this must be accepted", and later, once the
compiler produces executables, "and when run it must print this". The roadmap's
first end-to-end program, from
[Milestone 6](../../roadmap.md#milestone-6-basic-cpu-code-generation), is a good
example of what such a test will eventually check:

```vortex
// program: valid
fn main() {
    let result = 2 + 3 * 4;
    print(result);
}
```

Its expected output is the single line `14` on standard output, and its
expected exit status is 0 ([decision](../../decisions/program.md#d14)).
Earlier stages have their own kinds of expected output. The lexer's tests
expect a list of tokens. The parser's tests expect a printed tree.

An invalid program's test says "this must be rejected, for this reason, at this
place". The reason and the place matter as much as the rejection. The
[diagnostics chapter](../../specification/diagnostics.md#107-verification-requirements)
of the specification asks, for every rule, for one minimal rejected program,
the expected error category, and a check that the error points at the right
part of the source. It also asks for "a nearby accepted program", so the test
proves the compiler is rejecting the broken form and not something wider.

```vortex
// statements: type error
let value: bool = 10;
```

The specification's required result for this line is a type error that relates
the initializer `10` to the written type `bool`. A test that only checks "the
compiler failed" would also pass if the compiler failed for a completely
different reason, such as crashing. That is why the expected output for an
invalid program records the category and the location as well as the failure.

The label on the block's first line says it is a statement, so its test file
places it inside `fn main() { ... }`. On its own at the top of a file it would
be a syntax error, because statements are not allowed outside functions.

The specification also says that the exact wording of error messages is left
to the implementation. That gives you a decision to make early: will your
tests compare whole messages word for word, or only the category and the
location? Comparing whole messages catches accidental changes to wording but
forces you to update many files whenever you improve a message. Comparing only
the category and position is sturdier. Either is fine. Pick one and write it
down. The suggested default,
[implementation choice I3](../../decisions/implementation.md#i3), compares the
exit status and, for every error in order, its category and the position where
it starts, never the wording.

??? check "You improve a diagnostic's wording without changing what it means or where it points. Under a whole-message comparison, and under I3's comparison, what happens to the existing tests?"

    Under a whole-message comparison, every test whose message changed now
    fails and has to be updated by hand, even though nothing about the
    compiler's behavior is wrong. Under I3's comparison, the category and
    position are unchanged, so the tests keep passing: the wording was never
    part of what they checked.

<figure class="vx-figure">
<svg viewBox="0 0 760 290" role="img" aria-labelledby="grid-title grid-desc">
<title id="grid-title">A test run as a grid of expected and actual results</title>
<desc id="grid-desc">Four rows, one per test program. Each row shows the expected result, the actual result and the verdict. Three rows pass. The fourth expects a lexical error at line 2, column 17, but the compiler accepted the file, so the row is marked FAIL.</desc>
<text class="vx-text-muted" x="30" y="30">test program</text>
<text class="vx-text-muted" x="280" y="30">expected</text>
<text class="vx-text-muted" x="470" y="30">actual</text>
<text class="vx-text-muted" x="660" y="30">verdict</text>
<line class="vx-line" x1="20" y1="42" x2="740" y2="42"/>
<g class="vx-seq" style="--vx-i: 0; --vx-n: 4">
<rect class="vx-box" x="20" y="54" width="720" height="44"/>
<text class="vx-mono" x="30" y="81">valid/hello</text>
<text class="vx-mono" x="280" y="81">accepted</text>
<text class="vx-mono" x="470" y="81">accepted</text>
<text class="vx-text" x="660" y="81">PASS</text>
</g>
<g class="vx-seq" style="--vx-i: 1; --vx-n: 4">
<rect class="vx-box" x="20" y="108" width="720" height="44"/>
<text class="vx-mono" x="30" y="135">invalid/missing_colon</text>
<text class="vx-mono" x="280" y="135">syntax, 2:15</text>
<text class="vx-mono" x="470" y="135">syntax, 2:15</text>
<text class="vx-text" x="660" y="135">PASS</text>
</g>
<g class="vx-seq" style="--vx-i: 2; --vx-n: 4">
<rect class="vx-box" x="20" y="162" width="720" height="44"/>
<text class="vx-mono" x="30" y="189">invalid/bool_from_int</text>
<text class="vx-mono" x="280" y="189">type, 2:23</text>
<text class="vx-mono" x="470" y="189">type, 2:23</text>
<text class="vx-text" x="660" y="189">PASS</text>
</g>
<g class="vx-seq" style="--vx-i: 3; --vx-n: 4">
<rect class="vx-box-bad" x="20" y="216" width="720" height="44"/>
<text class="vx-mono" x="30" y="243">invalid/open_string</text>
<text class="vx-mono" x="280" y="243">lexical, 2:17</text>
<text class="vx-mono" x="470" y="243">accepted</text>
<text class="vx-text-accent" x="660" y="243">FAIL</text>
</g>
<text class="vx-text-muted" x="30" y="282">Positions are written line:column. Only the category and position are compared here, not the wording.</text>
</svg>
<figcaption>Figure 2. A test run seen as a grid, one row per test. The runner checks the rows one after another. The last row fails because a program that should have been rejected was accepted, which is exactly the kind of mistake the bench exists to catch. Each invalid test holds its statement inside <code>fn main() {</code> and <code>}</code>, on line 2, indented four spaces. Lines and columns both count from 1, as <a href="../../specification/conformance.md#17-source-locations">Conformance 1.7</a> requires.</figcaption>
</figure>

## Why the tests come first

The roadmap's own instructions for every milestone begin with reading the
grammar, and then: "Write at least one valid example and one invalid example
before implementation." Tests come before code for three plain reasons.

The first is that a test written beforehand is a statement of the rule, made
while you are thinking about the rule and not about your code. Written
afterwards, tests tend to record whatever the code happens to do, including
its mistakes.

The second is that Vortex already has most of the tests written for you, in
prose. The specification is full of `vortex` examples, and the first line of
each is a label, such as `// statements: type error`, that says what kind of
code the block holds and what the compiler must do with it
([decision 28](../../decisions/documentation.md#d28)). The
[conformance chapter](../../specification/conformance.md#18-specification-examples)
explains the labels. Because the label also says how to complete a block
(statements go inside `fn main() { ... }`, a block of declarations gets an
empty `main`, and a fragment is skipped), turning examples into test files is
steady, mechanical work, and it ties the compiler to the documents from the
first day. A small checker that reads the examples straight from the pages,
instead of copying them, is the suggested default in
[implementation choice I10](../../decisions/implementation.md#i10).

The third is regressions. A compiler is a long chain of stages, and a change in
an early one quietly affects every later one. The roadmap asks you to "run all
earlier milestone tests to detect regressions" at every milestone. That is only
practical if running them is one command.

## A runner that has failed at least once

The roadmap's completion condition for this stage has a detail worth reading
twice: the runner must be able to report "a passing and a failing test". A
runner that has only ever said "pass" has not been tested itself. Perhaps it
never ran anything. Perhaps it compares the wrong files. Perhaps a failure
inside it is swallowed and reported as success.

The simple way to know is to make a test fail on purpose, see the runner say
so clearly (which test, what was expected, what happened), and see the whole
command end with a failing exit status. Then remove the deliberate failure.

The runner below checks a small classifier, not a compiler, but the shape is
the one to copy: one line per case naming what was expected and what
happened, then a summary and the exit status the run ends with. Its last case is wrong on purpose, the way yours
should be at least once before you trust it.

--8<-- "includes/examples/build-v0.1/stage-0-workbench/mini_test_runner.cpp.md"

??? check "Suppose a slip in the runner above compared each case's expected answer with itself. What would the run print, and which line would warn you?"

    Every case would pass: "4 passed, 0 failed" and exit status 0. The warning
    is the `deliberate_failure` line. A case known to be wrong reported as PASS
    means the runner is not comparing what it should, which is exactly what the
    planted failure is there to reveal.

At this stage almost every interesting test will fail, because the compiler
does nothing yet. That is expected, and you have a choice about how to treat
it. One option is to add tests only when their stage begins. Another is to add
them all now and mark the ones that belong to later stages as expected to
fail. LLVM's test tool has a result for exactly this, XFAIL, alongside a
separate result, XPASS, for a test that was expected to fail but
passed.[^lit] Either approach works as long as the report stays honest. The
suggested default,
[implementation choice I9](../../decisions/implementation.md#i9), is the
second: each marked test names the stage that will make it pass, and a marked
test that starts passing fails the run until its mark is removed.

## What you need to have

<div class="vx-split" markdown="1">
<div markdown="1">

#### Need to have

- A `vortex` command that accepts a source-file path: every later stage hangs
  off it.
- A sensible response to a missing path, an unknown option or a wrong number
  of arguments: a short usage message on standard error and exit status 2, not
  a crash.
- The exit statuses from the
  [command-line decision](../../decisions/program.md#d20): 0 for success, 1
  when the source has errors, 2 for usage and file problems. The test runner
  and scripts depend on them.
- A runner that ignores warnings: a compiler may print them, but they never
  change the exit status or whether a program is accepted
  ([Diagnostics 10.1](../../specification/diagnostics.md#101-required-diagnostic-data)).
- A build that works from a clean configuration: the roadmap's first piece of
  evidence for every milestone.
- A test runner that finds test cases by itself: adding a test should mean
  adding a file, not editing the runner.
- Folders for valid programs, invalid programs and expected output: the
  roadmap names all three.
- A written decision about what an invalid-program test compares (category and
  position, or the whole message).
- A per-test verdict and a summary, and a failing exit status when any test
  fails: so one glance, or one script, can tell the result.
- One command that builds and then runs every test.
- Proof that the runner can fail: one deliberate failure, seen and removed.

</div>
<div class="vx-not-yet" markdown="1">

#### Not yet

- Reading or checking Vortex source: that is
  [stage 1](stage-1-source-and-diagnostics.md) and later.
- Command-line options for optimization, output names, or targets: v0.1 has no
  optimization, and the output file only matters from
  [stage 6](stage-6-first-machine-code.md).
- Installers, packages, or release versions: the release gate is
  [stage 11](stage-11-release.md).
- Editor or IDE support: useful later, but nothing exists yet to support.
- Parallel or incremental test runs: the suite is small; speed can wait until
  it hurts.
- Performance benchmarks: performance work starts after v0.1.
- A choice of back end or IR: that decision belongs to stage 6.

</div>
</div>

## What you do not need yet

The right-hand column above is the list. The common thread is that every item
on it either needs a working compiler to be meaningful or belongs to a version
after v0.1. The [roadmap](../../roadmap.md#after-v01) keeps an explicit list of
features that are "deliberately outside the first release". When you are
tempted to add a flag or a feature at this stage, check it against that list
first.

## How you know it is finished

The roadmap says Milestone 0 is complete "when the empty compiler builds
reliably and the test runner can report a passing and a failing test". An
**empty compiler** is one whose driver runs and exits properly but does no
translation yet. In practice, you are finished when all of these are true:

- On a fresh checkout, with no leftover build files, the one command builds
  `vortex` and runs the tests without any manual step in between.
- `vortex` with no arguments prints a usage message on standard error and ends
  with exit status 2.
- The runner reports at least one pass and, when you plant a deliberate
  failure, at least one fail, naming the test and saying what differed.
- The whole command's exit status is failing when any test fails.
- Someone else could add a test by adding a file, after reading a short note
  on where files go.

## Traps

**Checking only that the compiler failed.** An invalid-program test that passes
whenever the compiler reports any error, or crashes, will pass for the wrong
reason. Record the category and the position from the start, as the
[specification](../../specification/diagnostics.md#107-verification-requirements)
asks.

**Copying the compiler's output into the expected file without reading it.**
It is tempting to run the compiler, save whatever it printed, and call that the
expected output. If the compiler was wrong, the test now protects the mistake.
Expected output should come from the documents, not from the code under test.

**A build that only works on your machine.** A build that depends on files left
over from last week, or on a path that only exists on one computer, will break
the day someone else tries it. The roadmap asks for a clean configuration for
a reason.

**Tests that depend on each other.** If one test only passes because another
ran first, the suite will fail in confusing ways when someone runs a single
test. Each test case should stand alone.

**Building features instead of the bench.** Command-line options, color
output and configuration files are pleasant to write and easy to justify. None
of them helps you tell whether the lexer works. Leave them.

## Key ideas

!!! recap "Questions you can now answer"

    - **Why does stage 0 produce a compiler that translates nothing?** Because
      every later claim, "the compiler accepts this and rejects that", is only
      worth something once a machine can check it automatically, and that
      checking machinery is what this stage builds.
    - **What are the three pieces the bench needs?** A driver (`vortex`), a
      repeatable build, and a test runner with folders of test cases.
    - **Why does an invalid-program test need a category and a position, not
      only "the compiler failed"?** Because a compiler that fails for the
      wrong reason, or crashes, would otherwise also pass the test.
    - **Why should tests be written before the compiler does anything?** A
      test written first records the rule as understood before any code
      exists to bias it; a test written after tends to record what the code
      already does, mistakes included.
    - **What must you prove about a test runner before trusting its "all
      tests pass"?** That it can also report a failure: plant one on purpose,
      see it named and described, and see the whole command's exit status go
      nonzero.
    - **What does exit status 2 mean under the command-line decision, and who
      relies on it?** A usage error or a failure outside the source, such as
      an unreadable file, distinct from 0 (success) and 1 (the source has
      errors); the test runner and any scripts around it depend on the
      difference.

## Where this comes back

--8<-- "includes/next/compiler__guide__stage-0-workbench.md"

## How others teach this stage

**Ghuloum.** Abdulaziz Ghuloum's paper on incremental compiler construction
treats testing as part of the method, not an afterthought. He recommends
writing test cases for a small piece of the language first, then extending the
compiler until it passes them. His test cases are sample programs, each paired
with the output it should print, and his test driver compiles each one, links it with a small
runtime, runs it, and compares the output.[^ghuloum] He also provides an
automated testing facility and a test suite with his tutorial. The difference
for Vortex is that many Vortex tests are invalid programs, so the expected
output is often an error with a position, not a printed value.

**Sandler.** Nora Sandler's blog series comes with a public test suite, run by
pointing a script at your compiler. It holds valid and invalid programs for
each stage, and for her first stage the invalid programs are meant to "fail in
the parser, not the lexer".[^sandler-blog] That idea, that an invalid test also
says which kind of error it expects, is the same one Vortex asks for with
error categories. Her tests also check that no executable is produced when
parsing fails. Her later book opens with a chapter
titled "A Minimal Compiler".[^sandler-book]

**LLVM's lit and FileCheck.** Large compilers organize thousands of tests with
dedicated tools, and LLVM's are well documented. lit is a portable tool that runs
LLVM-style test suites, summarizes the results, and points out the
failures.[^lit] It finds tests by searching
folders, and each test file carries its own command lines saying how to run
it. FileCheck is a separate tool that reads a file of patterns and checks that
another program's output contains them, in order.[^filecheck] You do not need
either tool for Vortex. They are worth reading as an example of the questions
a test system has to answer: how tests are found, how expected output is
written, and what counts as a failure.

[^ghuloum]: Abdulaziz Ghuloum, "An Incremental Approach to Compiler Construction", *Proceedings of the 2006 Scheme and Functional Programming Workshop*, University of Chicago Technical Report TR-2006-06, section 2.7 "Testing Infrastructure". <http://scheme2006.cs.uchicago.edu/11-ghuloum.pdf>
[^sandler-blog]: Nora Sandler, "Writing a C Compiler, Part 1", 29 November 2017. <https://norasandler.com/2017/11/29/Write-a-Compiler.html>
[^sandler-book]: Nora Sandler, *Writing a C Compiler: Build a Real Programming Language from Scratch*, No Starch Press, 2024. <https://nostarch.com/writing-c-compiler>
[^grep]: The Open Group, "grep", *The Open Group Base Specifications Issue 8* (IEEE Std 1003.1-2024), section "Exit status". <https://pubs.opengroup.org/onlinepubs/9799919799/utilities/grep.html>
[^lit]: LLVM Project, "lit - LLVM Integrated Tester". <https://llvm.org/docs/CommandGuide/lit.html>
[^filecheck]: LLVM Project, "FileCheck - Flexible pattern matching file verifier". <https://llvm.org/docs/CommandGuide/FileCheck.html>

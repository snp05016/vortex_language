# 11. Release

<p class="page-intro">No new features. This stage proves that everything built in stages 0 to 10 works together, writes down what does not work yet, and only then puts the name v0.1.0 on it.</p>

A compiler is never finished in the sense of having nothing left to add. It
can be finished in a narrower sense: it does what it claims to do, the claims
are written down, and anyone can check them. That narrower sense is what this
stage is about.

The roadmap calls
[Milestone 11](../../roadmap.md#milestone-11-v01-release-gate) a "release
gate". A **gate** is a set of checks that must all pass before something
moves on. Nothing new is built here. Instead, every earlier stage is tested
again, together, from a clean start, and the results decide whether the
compiler may call itself Vortex v0.1.0.

This stage covers the whole [compiler mountain](index.md#the-shape-of-the-whole-thing)
at once. Figure 1 shows where it sits among the other stages.

<figure class="vx-figure">
<svg viewBox="0 0 760 240" role="img" aria-labelledby="s11-time-title s11-time-desc">
<title id="s11-time-title">The twelve stages of the guide on one line, with stage 11 highlighted</title>
<desc id="s11-time-desc">Twelve numbered circles, 0 to 11, sit on a horizontal line with short names below: workbench, diagnostics, lexer, parser, names, types, first code, control flow, memory, safety, matrix, release. Brackets above group stages 2 to 5 as the front end and stages 6 to 9 as code generation and the runtime. The circle for stage 11 is highlighted and pulses. A dot travels along the line from stage 0 to stage 11.</desc>
<text class="vx-text" x="30" y="30">Twelve stages, one compiler</text>
<line class="vx-line" x1="170" y1="100" x2="350" y2="100"/>
<text class="vx-text-muted" x="260" y="90" text-anchor="middle">front end</text>
<line class="vx-line" x1="410" y1="100" x2="590" y2="100"/>
<text class="vx-text-muted" x="500" y="90" text-anchor="middle">code generation and runtime</text>
<line class="vx-line" x1="650" y1="100" x2="710" y2="100"/>
<text class="vx-text-muted" x="680" y="90" text-anchor="middle">proof</text>
<line class="vx-line" x1="50" y1="140" x2="710" y2="140"/>
<circle class="vx-dot" r="5">
<animateMotion dur="8s" repeatCount="indefinite" path="M50 140 L710 140" keyPoints="0;0;1;1" keyTimes="0;0.08;0.9;1" calcMode="linear"/>
</circle>
<circle class="vx-box" cx="50" cy="140" r="16"/>
<circle class="vx-box" cx="110" cy="140" r="16"/>
<circle class="vx-box" cx="170" cy="140" r="16"/>
<circle class="vx-box" cx="230" cy="140" r="16"/>
<circle class="vx-box" cx="290" cy="140" r="16"/>
<circle class="vx-box" cx="350" cy="140" r="16"/>
<circle class="vx-box" cx="410" cy="140" r="16"/>
<circle class="vx-box" cx="470" cy="140" r="16"/>
<circle class="vx-box" cx="530" cy="140" r="16"/>
<circle class="vx-box" cx="590" cy="140" r="16"/>
<circle class="vx-box" cx="650" cy="140" r="16"/>
<circle class="vx-box-accent vx-pulse" cx="710" cy="140" r="18"/>
<text class="vx-text" x="50" y="145" text-anchor="middle">0</text>
<text class="vx-text" x="110" y="145" text-anchor="middle">1</text>
<text class="vx-text" x="170" y="145" text-anchor="middle">2</text>
<text class="vx-text" x="230" y="145" text-anchor="middle">3</text>
<text class="vx-text" x="290" y="145" text-anchor="middle">4</text>
<text class="vx-text" x="350" y="145" text-anchor="middle">5</text>
<text class="vx-text" x="410" y="145" text-anchor="middle">6</text>
<text class="vx-text" x="470" y="145" text-anchor="middle">7</text>
<text class="vx-text" x="530" y="145" text-anchor="middle">8</text>
<text class="vx-text" x="590" y="145" text-anchor="middle">9</text>
<text class="vx-text" x="650" y="145" text-anchor="middle">10</text>
<text class="vx-text-accent" x="710" y="145" text-anchor="middle">11</text>
<text class="vx-text-muted" x="50" y="182" text-anchor="middle">workbench</text>
<text class="vx-text-muted" x="110" y="204" text-anchor="middle">diagnostics</text>
<text class="vx-text-muted" x="170" y="182" text-anchor="middle">lexer</text>
<text class="vx-text-muted" x="230" y="204" text-anchor="middle">parser</text>
<text class="vx-text-muted" x="290" y="182" text-anchor="middle">names</text>
<text class="vx-text-muted" x="350" y="204" text-anchor="middle">types</text>
<text class="vx-text-muted" x="410" y="182" text-anchor="middle">first code</text>
<text class="vx-text-muted" x="470" y="204" text-anchor="middle">control flow</text>
<text class="vx-text-muted" x="530" y="182" text-anchor="middle">memory</text>
<text class="vx-text-muted" x="590" y="204" text-anchor="middle">safety</text>
<text class="vx-text-muted" x="650" y="182" text-anchor="middle">matrix</text>
<text class="vx-text-accent" x="710" y="204" text-anchor="middle">release</text>
</svg>
<figcaption>Figure 1. All twelve stages of this guide, matching roadmap milestones 0 to 11. The dot is the compiler growing from an empty workbench to a release. Stage 11, highlighted, adds nothing to the line; it checks the whole of it.</figcaption>
</figure>

--8<-- "includes/remember/compiler__guide__stage-11-release.md"

!!! goals "In this stage"

    - Rerun every earlier milestone's tests together, from a clean checkout, and treat any failure as a regression.
    - Pair every specification rule with a valid and an invalid test, and check each rejection's category and source span.
    - Compile every documented example according to its label, including rejecting programs labeled planned.
    - Document the compiler command, its supported features and its known limitations.
    - Apply the name v0.1.0 only after every other item in the gate passes.

## What this stage is for

The roadmap lists six items for
[Milestone 11](../../roadmap.md#milestone-11-v01-release-gate): run every
valid and invalid compiler test; compile every v0.1 example from the
documentation and check that each gives the result its label names; verify
diagnostic source locations and runtime error messages;
document the compiler command, supported features and known limitations;
confirm that a clean checkout can build and run the test suite; and mark the
release as `v0.1.0` "only after every item above passes".

Next to it stands the roadmap's
[Definition of done](../../roadmap.md#definition-of-done), six statements about
what Vortex v0.1 can do. Each one points back at stages you have already
built:

| Definition of done | Where it was built |
| --- | --- |
| Read, tokenize, parse and type-check a documented program | Stages [1](stage-1-source-and-diagnostics.md) to [5](stage-5-types-and-rules.md) |
| Reject invalid programs with useful source-based diagnostics | Stages [1](stage-1-source-and-diagnostics.md) to [5](stage-5-types-and-rules.md) |
| Produce and link a CPU executable | Stage [6](stage-6-first-machine-code.md) |
| Run the scalar, control-flow, function, string, struct, array and reference examples | Stages [6](stage-6-first-machine-code.md) to [8](stage-8-data-in-memory.md) |
| Detect the documented runtime safety errors | Stage [9](stage-9-runtime-safety.md) |
| Compile and correctly run naive matrix multiplication | Stage [10](stage-10-matrix-multiplication.md) |

So the job of this stage is to collect evidence for every row of that table,
all at once, from a fresh start.

## Words for this stage

release
: A version of the compiler that is given a name and offered to other people
  as working, with its limits stated.

release gate
: The list of checks that must all pass before a release may be named.

version number
: The name given to a release, such as `v0.1.0`. It lets people say exactly
  which compiler they used.

test suite
: The whole collection of automated tests for the compiler.

test runner
: The program that runs the test suite and reports which tests passed and
  which failed. Stage 0 built it.

regression
: Something that used to work and has stopped working because of a later
  change. Running old tests after every change is how regressions are caught.

valid and invalid pair
: Two small test programs, one that must be accepted and a nearly identical
  one that must be rejected, which together show a rule is enforced exactly
  where it should be.

expected output
: The exact text a test says a program or the compiler should print. The test
  passes only if the real output matches it.

end-to-end test
: A test that runs the whole compiler on a source file, runs the resulting
  executable if there is one, and checks what comes out.

clean checkout
: A fresh copy of the project's files, with nothing left over from earlier
  builds.

known limitation
: Something the language describes that this compiler does not yet do, written
  down so users are not surprised by it.

implementation limit
: A point where a compiler supports less than the language allows, such as a
  maximum array size. The conformance chapter requires these to be
  documented.

expected failure
: A test that is known to fail and is marked that way on purpose, usually
  because it tests a known limitation.

## The test suite as a whole

The [architecture page](../architecture.md#testing-strategy) says each pass
needs "both local and pipeline tests", and it names six kinds:

1. Lexer tests check token kinds, spellings and source spans.
2. Parser tests check tree shape, ordering and source locations.
3. Name and type tests pair one accepted program with the nearest rejected
   form.
4. Constant-evaluation tests separate dimensions that are merely grammatical
   from dimensions that also meet the compile-time rules, and pair each check
   on constant operands, such as `10 / 0`, with the same operation on
   variables, which must compile and fail at run time.
5. Lowering tests compare the observable output of valid programs.
6. End-to-end tests check diagnostics and executable behavior.

Each kind appeared in its own stage. At release, they all run together.
Figure 2 shows how the suite has grown: each kind starts at the milestone that
introduced it and then never stops running.

Every one of those kinds, however different its input, comes down to the same
comparison: run something, and check its output against what was expected.
Here is that comparison at its smallest, on a table of toy cases instead of
compiler runs:

--8<-- "includes/examples/build-v0.1/stage-11-release/expected_output.cpp.md"

<figure class="vx-figure">
<svg viewBox="0 0 760 340" role="img" aria-labelledby="s11-grid-title s11-grid-desc">
<title id="s11-grid-title">A grid of test kinds against milestones</title>
<desc id="s11-grid-desc">Six rows, one per kind of test: lexer, parser, names and types, constant evaluation, lowering, and end to end. Twelve columns, milestones 0 to 11. A row's cells are filled from the milestone where that kind of test first appears through milestone 11, so the filled area grows like a staircase. At milestone 11 every row is filled. The columns light up from left to right.</desc>
<text class="vx-text" x="20" y="30">Which tests run at each milestone</text>
<text class="vx-text-muted" x="190" y="94" text-anchor="end">Lexer</text>
<text class="vx-text-muted" x="190" y="130" text-anchor="end">Parser</text>
<text class="vx-text-muted" x="190" y="166" text-anchor="end">Names and types</text>
<text class="vx-text-muted" x="190" y="202" text-anchor="end">Constant evaluation</text>
<text class="vx-text-muted" x="190" y="238" text-anchor="end">Lowering</text>
<text class="vx-text-muted" x="190" y="274" text-anchor="end">End to end</text>
<rect class="vx-box" x="210" y="74" width="40" height="30"/><rect class="vx-box" x="254" y="74" width="40" height="30"/><rect class="vx-box" x="298" y="74" width="40" height="30"/><rect class="vx-box" x="342" y="74" width="40" height="30"/><rect class="vx-box" x="386" y="74" width="40" height="30"/><rect class="vx-box" x="430" y="74" width="40" height="30"/><rect class="vx-box" x="474" y="74" width="40" height="30"/><rect class="vx-box" x="518" y="74" width="40" height="30"/><rect class="vx-box" x="562" y="74" width="40" height="30"/><rect class="vx-box" x="606" y="74" width="40" height="30"/><rect class="vx-box" x="650" y="74" width="40" height="30"/><rect class="vx-box" x="694" y="74" width="40" height="30"/>
<rect class="vx-box" x="210" y="110" width="40" height="30"/><rect class="vx-box" x="254" y="110" width="40" height="30"/><rect class="vx-box" x="298" y="110" width="40" height="30"/><rect class="vx-box" x="342" y="110" width="40" height="30"/><rect class="vx-box" x="386" y="110" width="40" height="30"/><rect class="vx-box" x="430" y="110" width="40" height="30"/><rect class="vx-box" x="474" y="110" width="40" height="30"/><rect class="vx-box" x="518" y="110" width="40" height="30"/><rect class="vx-box" x="562" y="110" width="40" height="30"/><rect class="vx-box" x="606" y="110" width="40" height="30"/><rect class="vx-box" x="650" y="110" width="40" height="30"/><rect class="vx-box" x="694" y="110" width="40" height="30"/>
<rect class="vx-box" x="210" y="146" width="40" height="30"/><rect class="vx-box" x="254" y="146" width="40" height="30"/><rect class="vx-box" x="298" y="146" width="40" height="30"/><rect class="vx-box" x="342" y="146" width="40" height="30"/><rect class="vx-box" x="386" y="146" width="40" height="30"/><rect class="vx-box" x="430" y="146" width="40" height="30"/><rect class="vx-box" x="474" y="146" width="40" height="30"/><rect class="vx-box" x="518" y="146" width="40" height="30"/><rect class="vx-box" x="562" y="146" width="40" height="30"/><rect class="vx-box" x="606" y="146" width="40" height="30"/><rect class="vx-box" x="650" y="146" width="40" height="30"/><rect class="vx-box" x="694" y="146" width="40" height="30"/>
<rect class="vx-box" x="210" y="182" width="40" height="30"/><rect class="vx-box" x="254" y="182" width="40" height="30"/><rect class="vx-box" x="298" y="182" width="40" height="30"/><rect class="vx-box" x="342" y="182" width="40" height="30"/><rect class="vx-box" x="386" y="182" width="40" height="30"/><rect class="vx-box" x="430" y="182" width="40" height="30"/><rect class="vx-box" x="474" y="182" width="40" height="30"/><rect class="vx-box" x="518" y="182" width="40" height="30"/><rect class="vx-box" x="562" y="182" width="40" height="30"/><rect class="vx-box" x="606" y="182" width="40" height="30"/><rect class="vx-box" x="650" y="182" width="40" height="30"/><rect class="vx-box" x="694" y="182" width="40" height="30"/>
<rect class="vx-box" x="210" y="218" width="40" height="30"/><rect class="vx-box" x="254" y="218" width="40" height="30"/><rect class="vx-box" x="298" y="218" width="40" height="30"/><rect class="vx-box" x="342" y="218" width="40" height="30"/><rect class="vx-box" x="386" y="218" width="40" height="30"/><rect class="vx-box" x="430" y="218" width="40" height="30"/><rect class="vx-box" x="474" y="218" width="40" height="30"/><rect class="vx-box" x="518" y="218" width="40" height="30"/><rect class="vx-box" x="562" y="218" width="40" height="30"/><rect class="vx-box" x="606" y="218" width="40" height="30"/><rect class="vx-box" x="650" y="218" width="40" height="30"/><rect class="vx-box" x="694" y="218" width="40" height="30"/>
<rect class="vx-box" x="210" y="254" width="40" height="30"/><rect class="vx-box" x="254" y="254" width="40" height="30"/><rect class="vx-box" x="298" y="254" width="40" height="30"/><rect class="vx-box" x="342" y="254" width="40" height="30"/><rect class="vx-box" x="386" y="254" width="40" height="30"/><rect class="vx-box" x="430" y="254" width="40" height="30"/><rect class="vx-box" x="474" y="254" width="40" height="30"/><rect class="vx-box" x="518" y="254" width="40" height="30"/><rect class="vx-box" x="562" y="254" width="40" height="30"/><rect class="vx-box" x="606" y="254" width="40" height="30"/><rect class="vx-box" x="650" y="254" width="40" height="30"/><rect class="vx-box" x="694" y="254" width="40" height="30"/>
<g class="vx-seq" style="--vx-i: 0; --vx-n: 12"><text class="vx-text" x="230" y="62" text-anchor="middle">0</text></g>
<g class="vx-seq" style="--vx-i: 1; --vx-n: 12"><text class="vx-text" x="274" y="62" text-anchor="middle">1</text></g>
<g class="vx-seq" style="--vx-i: 2; --vx-n: 12"><text class="vx-text" x="318" y="62" text-anchor="middle">2</text><rect class="vx-cell-on" x="298" y="74" width="40" height="30"/></g>
<g class="vx-seq" style="--vx-i: 3; --vx-n: 12"><text class="vx-text" x="362" y="62" text-anchor="middle">3</text><rect class="vx-cell-on" x="342" y="74" width="40" height="30"/><rect class="vx-cell-on" x="342" y="110" width="40" height="30"/></g>
<g class="vx-seq" style="--vx-i: 4; --vx-n: 12"><text class="vx-text" x="406" y="62" text-anchor="middle">4</text><rect class="vx-cell-on" x="386" y="74" width="40" height="30"/><rect class="vx-cell-on" x="386" y="110" width="40" height="30"/><rect class="vx-cell-on" x="386" y="146" width="40" height="30"/></g>
<g class="vx-seq" style="--vx-i: 5; --vx-n: 12"><text class="vx-text" x="450" y="62" text-anchor="middle">5</text><rect class="vx-cell-on" x="430" y="74" width="40" height="30"/><rect class="vx-cell-on" x="430" y="110" width="40" height="30"/><rect class="vx-cell-on" x="430" y="146" width="40" height="30"/><rect class="vx-cell-on" x="430" y="182" width="40" height="30"/></g>
<g class="vx-seq" style="--vx-i: 6; --vx-n: 12"><text class="vx-text" x="494" y="62" text-anchor="middle">6</text><rect class="vx-cell-on" x="474" y="74" width="40" height="30"/><rect class="vx-cell-on" x="474" y="110" width="40" height="30"/><rect class="vx-cell-on" x="474" y="146" width="40" height="30"/><rect class="vx-cell-on" x="474" y="182" width="40" height="30"/><rect class="vx-cell-on" x="474" y="218" width="40" height="30"/><rect class="vx-cell-on" x="474" y="254" width="40" height="30"/></g>
<g class="vx-seq" style="--vx-i: 7; --vx-n: 12"><text class="vx-text" x="538" y="62" text-anchor="middle">7</text><rect class="vx-cell-on" x="518" y="74" width="40" height="30"/><rect class="vx-cell-on" x="518" y="110" width="40" height="30"/><rect class="vx-cell-on" x="518" y="146" width="40" height="30"/><rect class="vx-cell-on" x="518" y="182" width="40" height="30"/><rect class="vx-cell-on" x="518" y="218" width="40" height="30"/><rect class="vx-cell-on" x="518" y="254" width="40" height="30"/></g>
<g class="vx-seq" style="--vx-i: 8; --vx-n: 12"><text class="vx-text" x="582" y="62" text-anchor="middle">8</text><rect class="vx-cell-on" x="562" y="74" width="40" height="30"/><rect class="vx-cell-on" x="562" y="110" width="40" height="30"/><rect class="vx-cell-on" x="562" y="146" width="40" height="30"/><rect class="vx-cell-on" x="562" y="182" width="40" height="30"/><rect class="vx-cell-on" x="562" y="218" width="40" height="30"/><rect class="vx-cell-on" x="562" y="254" width="40" height="30"/></g>
<g class="vx-seq" style="--vx-i: 9; --vx-n: 12"><text class="vx-text" x="626" y="62" text-anchor="middle">9</text><rect class="vx-cell-on" x="606" y="74" width="40" height="30"/><rect class="vx-cell-on" x="606" y="110" width="40" height="30"/><rect class="vx-cell-on" x="606" y="146" width="40" height="30"/><rect class="vx-cell-on" x="606" y="182" width="40" height="30"/><rect class="vx-cell-on" x="606" y="218" width="40" height="30"/><rect class="vx-cell-on" x="606" y="254" width="40" height="30"/></g>
<g class="vx-seq" style="--vx-i: 10; --vx-n: 12"><text class="vx-text" x="670" y="62" text-anchor="middle">10</text><rect class="vx-cell-on" x="650" y="74" width="40" height="30"/><rect class="vx-cell-on" x="650" y="110" width="40" height="30"/><rect class="vx-cell-on" x="650" y="146" width="40" height="30"/><rect class="vx-cell-on" x="650" y="182" width="40" height="30"/><rect class="vx-cell-on" x="650" y="218" width="40" height="30"/><rect class="vx-cell-on" x="650" y="254" width="40" height="30"/></g>
<g class="vx-seq" style="--vx-i: 11; --vx-n: 12"><text class="vx-text-accent" x="714" y="62" text-anchor="middle">11</text><rect class="vx-cell-on" x="694" y="74" width="40" height="30"/><rect class="vx-cell-on" x="694" y="110" width="40" height="30"/><rect class="vx-cell-on" x="694" y="146" width="40" height="30"/><rect class="vx-cell-on" x="694" y="182" width="40" height="30"/><rect class="vx-cell-on" x="694" y="218" width="40" height="30"/><rect class="vx-cell-on" x="694" y="254" width="40" height="30"/></g>
<text class="vx-text-muted" x="210" y="310">Milestone number across the top. A filled cell means that kind of test exists and runs.</text>
<text class="vx-text-muted" x="210" y="328">Nothing is ever switched off: milestone 11 runs every row.</text>
</svg>
<figcaption>Figure 2. The test suite as a staircase. Each kind of test from the architecture page begins at its milestone and keeps running at every later one, because the roadmap asks you to "run all earlier milestone tests" each time. The release gate is simply the rightmost column: everything, together.</figcaption>
</figure>

The staircase shape is the point. The roadmap's working rule for every
milestone ends with "Run all earlier milestone tests to detect regressions".
If that rule was followed, the release gate holds no surprises: the suite has
been running in full all along. If it was not, this is where the surprises
come out.

??? check "Milestone 3 introduced the parser tests. If a change made while working on milestone 9 breaks one of them, and the working rule above was followed, when is that break caught?"

    Back at milestone 9, not at the release gate. The parser tests have been
    running at every milestone since milestone 3, so a break shows up the
    moment it happens. The release gate reruns the same staircase from a
    clean start; it finds new problems only when an earlier milestone skipped
    the rule.

## Valid and invalid pairs

The single most useful habit in a compiler test suite is to write tests in
pairs. The
[diagnostics chapter](../../specification/diagnostics.md#107-verification-requirements)
asks that each diagnostic rule have four things: "one minimal rejected
program", its expected category, an assertion that the error's source span
covers the right construct, and "a nearby accepted program that proves the
test is not rejecting a broader valid form".

Here is such a pair, using the chapter's own immutable-assignment example:

```vortex
// program: semantic error
fn main() {
    let value = 10;
    value = 20;
    // semantic error: value is immutable
}
```

```vortex
// program: valid
fn main() {
    let mut value = 10;
    value = 20;
    // accepted: the only change is mut
}
```

The two programs differ by one word. If the first is rejected and the second
accepted, the rule is enforced in exactly the right place. If only the first
test existed, a compiler that rejected every assignment would pass it. The
pair is what makes the test mean something.

??? check "To pair the rejected program above, someone writes an accepted twin that declares `let value = 10;` and never assigns to it. Does this pair catch a compiler that rejects every assignment?"

    No. The twin contains no assignment, so that compiler accepts it and
    passes both tests. The accepted program has to keep the assignment and
    change only `mut`, so that the one difference between the two is the
    property the rule is about.

The same pairing works on any rule, not only Vortex's own. Here is a small
checker with two ways to reject a string, reporting which one fired so that a
pair can prove each rejection is about its own rule and not the other:

--8<-- "includes/examples/build-v0.1/stage-11-release/valid_invalid_pair.cpp.md"

At release, every rule in the specification should have its pair, and the
test runner should check the category and the span of every rejection, not
only that the compiler failed. [I3](../../decisions/implementation.md#i3)
suggests comparing the exit status and, for every error in order, its
category and the line and column where its primary span starts, never the
message text. The words "in order" matter: two diagnostics in the wrong
order are as wrong as a missing one. A small
in-order matcher, in the spirit of LLVM's FileCheck, shows why position in
the sequence is part of the check:

--8<-- "includes/examples/build-v0.1/stage-11-release/filecheck_lines.cpp.md"

## Compiling every example in the documentation

The roadmap asks you to "compile every v0.1 example from the documentation",
and to "check that each gives the result its label names". The Vortex site has
many examples, in the specification, the language tour, the grammar reference
and the roadmap itself. Each one says what it is: its first line is a label,
such as `// items: type error`, that the
[conformance chapter](../../specification/conformance.md#18-specification-examples)
defines ([decision](../../decisions/documentation.md#d28)). The kind tells you
how to complete the block, and the result tells you what the compiler must do
with it:

| Result | What the compiler must do |
| --- | --- |
| `valid` | Accept it; when run, it finishes without a runtime error |
| `lexical error`, `syntax error` | Reject it while lexing or parsing, with that category |
| `name error`, `type error`, `semantic error`, `constant-evaluation error` | Parse it, then reject it in a later static phase with that category (the ["Static error"](../../decisions/documentation.md#d51) group) |
| `runtime error` | Accept it, and have the executable stop with a runtime error |
| `planned` | Reject it: it is not v0.1 |

That last row is easy to forget. The conformance chapter says a compiler must
"avoid accepting planned syntax as though it were standardized v0.1 syntax".
The tour's kernel example, for instance, uses a `kernel` keyword, slices and a
`.len()` method, and says plainly that the v0.1 parser should not accept it.
A test that confirms the rejection is as much a part of the release as a test
that confirms matrix multiplication works.

??? check "A test suite compiles every documented example labeled valid or with an error result, and every one behaves as its label says. The parser also accepts the tour's kernel example. Does the release gate pass?"

    No. The kernel example is labeled `planned`, so the compiler must reject
    it; accepting it is a conformance failure, because the compiler now takes
    syntax that v0.1 does not define. The suite missed it because it never
    tested the `planned` row of the table.

Two practical questions come with this item.

The first, how to test blocks that are not whole programs, is settled by the
labels. A `statements` block such as
`let row: [f32; 2 + 2] = [0.0; 2 + 2];` is tested inside `fn main() { ... }`,
because top-level statements are a syntax error. An `items` block gets
`fn main() {}` appended when it has no `main`. A `fragment`, such as the type
`[f32; 4]` on its own, is never compiled.

The second is how to keep the examples and the tests from drifting apart. If
examples are copied into the test suite by hand, a later edit to the
documentation will not reach the tests. Whatever method you choose, a changed
example should either be retested automatically or cause a visible failure.
[I10](../../decisions/implementation.md#i10) suggests a checker that reads
every block straight from the documentation, completes it by its label and
runs `vortex` on it, so nothing is copied.

## The clean checkout

The roadmap asks you to "confirm that a clean checkout can build and run the
test suite". A **clean checkout** is a fresh copy of the project, with none of
the files your own machine has accumulated: no old build folders, no cached
results, no tool installed by hand one afternoon and forgotten.

The reason is simple. A compiler that builds only on its author's machine is
not released; it is stranded. The one-command build from
[stage 0](stage-0-workbench.md) is what this test exercises. Run it in a new
directory, or better, on a different machine, and write down every step that
was needed beyond that one command. Each of those steps is either a bug to fix
or a requirement to document.

## Writing down what the compiler does

The roadmap asks for three pieces of documentation: the compiler command,
the supported features, and the known limitations.

**The compiler command** is the user's first contact: how to run `vortex` on
a file, what it produces, and where errors and output go. The
[command-line decision](../../decisions/program.md#d20) fixes the options, the
streams and the exit statuses; the release document restates them for users,
with an example of each option.

**Supported features** can mostly point at the specification. The useful
extra is the compiler's answer to each entry in the conformance chapter's
[list of implementation-defined behavior and limits](../../specification/conformance.md#110-implementation-defined-behavior-and-limits):
the width of `usize`, the stack size, the array and source limits, the
diagnostic layout, which warnings exist, the runtime message text, the layout
document from [stage 8](stage-8-data-in-memory.md), and the targets. For each
entry, the conformance chapter says the implementation "must document the
selected behavior and apply it consistently"
([decision](../../decisions/documentation.md#d55)). The form of the runtime
error line and the exit statuses are fixed by
[Diagnostics 10.6](../../specification/diagnostics.md#106-runtime-reporting)
([decision](../../decisions/program.md#d14)), so they are not choices.

**Known limitations** are where honesty pays. The conformance chapter says a
conforming compiler must "document any implementation limit that is narrower
than the language design", and the diagnostics chapter provides an
**implementation-limit error** category for specified behavior the compiler
has not yet built. The same category reports a program that runs out of stack
([record 46](../../decisions/diagnostics.md#d46)). The diagnostics chapter
adds that this category "must not be used to disguise a crash or silently
ignore source". A clear limitation is fine. A hidden one is a bug.

??? check "A compiler supports arrays of up to four dimensions. Given a fifth dimension, it silently computes with only the first four and returns an answer. The release notes list the four-dimension limit. Does this satisfy the conformance chapter?"

    No. Documenting the limit is not enough on its own: the compiler must also
    report an implementation-limit error when a program exceeds it, not
    compute a wrong answer and stay quiet. A written-down limitation still has
    to be enforced at the moment it is hit.

Earlier drafts of the specification left several questions open, such as
zero-length arrays, empty structs, the cast table and the forms of `print`.
The [decision records](../../decisions/index.md) settle each one, and the
specification now states the answers, so a release does not choose them. What
a release does choose is exactly the conformance list above: write down each
choice, so that a user who hits one finds an answer instead of a surprise.

## Naming the release

The last roadmap item is short: mark the release as `v0.1.0` "only after every
item above passes". The order matters. The name comes last because the name
is a promise that everything before it is true.

The language is called Vortex v0.1. The release gets a third number, the `.0`
at the end, which leaves room for later releases that fix bugs in this
compiler without changing the language it implements. Record in the release
notes exactly which commit the name refers to, so that "v0.1.0" always means
one specific compiler.

## What you need to have

<div class="vx-split" markdown="1">
<div markdown="1">

#### Need to have

- Every test from every earlier milestone passing in one run: the first item of the gate.
- Valid and invalid pairs for every rule, checking category and span: required by the diagnostics chapter.
- Every documented example tested according to its label, including rejection of planned syntax.
- Runtime error lines checked for kind and position, as the gate requires.
- A successful build and test run from a clean checkout with one command.
- Documentation of the compiler command, supported features, layout and runtime choices.
- A known limitations list, and the compiler's documented choice for every entry in the implementation-defined list.
- The name `v0.1.0`, applied last.

</div>
<div class="vx-not-yet" markdown="1">

#### Not yet

- Performance tests or benchmarks: v0.1 is about correctness.
- Packaging, installers or distribution through package managers: the roadmap does not ask for them.
- Support for more than the CPU target you chose: GPU work is after v0.1.
- Features from the after v0.1 list, however close they seem.

</div>
</div>

## What you do not need yet

The right-hand column is a reminder that a release is a line drawn on
purpose. Everything on it is worth doing, and none of it is needed for the
first version to be honest and correct.

## How you know it is finished

This is the one stage where the roadmap's completion condition is the whole
checklist. [Milestone 11](../../roadmap.md#milestone-11-v01-release-gate) is
complete when every one of its items passes, and the
[Definition of done](../../roadmap.md#definition-of-done) is met when each of
its six statements is backed by tests in the suite.

A practical way to check: take the Definition of done table near the top of
this page, and next to each row write the names of the tests that prove it.
If a row has no tests, it is not done. If every row has tests, and the suite
passes from a clean checkout, you may name the release.

## Traps

**Testing that the compiler fails, not how.** A rejected-program test that
only checks "the compiler exited with an error" will keep passing even after
a change makes it fail for the wrong reason. Check the category and the span.

**Letting known failures turn into noise.** If some tests always fail and
everyone learns to ignore them, a new failure will hide among them. Mark known
limitations explicitly as expected failures, so that the suite is either green
or has something new to say. [I9](../../decisions/implementation.md#i9)
suggests a mark in the test's own expected file that names the stage that will
make it pass, such as `xfail: stage 8`, and a failed run when a marked test
starts passing. That last case needs its own name, because it is neither a
plain pass nor a plain failure:

--8<-- "includes/examples/build-v0.1/stage-11-release/xfail_runner.cpp.md"

**Forgetting the planned examples.** Accepting future syntax by accident is a
conformance failure, and it is easy to do when the parser is written
generously.

**Trusting your own machine.** A build that relies on something only your
machine has will pass every time you run it and fail for the first person who
tries. The clean checkout exists to catch this.

**Documenting limitations nobody can find.** A limitation mentioned only in a
commit message does not count. Put it where a user of the compiler will look.

**Naming the release early.** Once a version name is public, it is hard to
take back. Apply it after the gate, not as a goal to hit by a date.

## Key ideas

!!! recap "Questions you can now answer"

    - **What does the release gate add that is new to the compiler?** Nothing. It reruns every earlier milestone's tests together, from a clean checkout, and the results decide whether the release may be named.
    - **Why does a rejected-program test need a nearby accepted program too?** Without one, a checker that rejects everything would still pass; the accepted case proves the rule fires exactly where it should and nowhere else.
    - **How does a documented example tell a checker what it must do?** Its first line is a label naming its kind and result, such as `// items: type error`, which fixes both how to complete the block and what the compiler must do with it.
    - **Why must a compiler reject examples labeled planned?** The conformance chapter says a compiler must not accept planned syntax as though it were v0.1, so accepting it is a conformance failure.
    - **What makes a checkout "clean"?** A fresh copy of the project with none of the build folders, caches or hand-installed tools that accumulate on one machine over time.
    - **What must an implementation-limit report never be used for?** To disguise a crash or silently ignore source; a limitation has to be reported at the moment it is hit, in addition to being documented.
    - **Why does the version name come last?** It is a promise that everything before it is already true; naming the release before the gate passes breaks that promise.

## Where this comes back

--8<-- "includes/next/compiler__guide__stage-11-release.md"

## How others teach this stage

**Ghuloum.** The incremental compiler's testing infrastructure has two parts:
test cases made of "sample programs and their expected output", and a test
driver that compiles each one, links it with a small runtime, runs it, and
compares the output, signaling an error if any step fails.[^ghuloum] His
method also insists on a compiler that passes all its tests at every step,
which is the same idea as Figure 2's staircase. Vortex adds one thing his
tests do not stress: checking what the compiler says about bad programs, not
only what good programs print.

**Sandler.** The first post of Nora Sandler's "Writing a C Compiler" series
points readers to a test script that "will compile a set of test programs
using your compiler, execute them, and make sure they return the right
value".[^sandler-blog] The tests are grouped by
stage and include invalid programs for each stage, the same valid and invalid
split this guide asks for.

**lit.** LLVM's own test runner describes itself as a tool for running test
suites and summarizing their results.[^lit] Two of its result kinds are worth
copying as ideas even if you never use the tool: an expected failure (a test
that fails, but was expected to) and an unexpected pass (a test that was
expected to fail but now succeeds, usually because something was
fixed).[^lit] The second is how you notice that a known limitation has quietly
gone away and the documentation needs updating.

**FileCheck.** Its companion tool reads a program's output and checks it
against patterns written in the test file: lines that must appear in order,
lines that must follow directly, and text that must not appear.[^filecheck]
It is a good model for checking diagnostics, where you care that the right
category and location appear, but may not want to freeze every word of the
message.

## Where to go after v0.1

The roadmap's [After v0.1](../../roadmap.md#after-v01) list is the map for
what comes next. It is deliberately kept out of the first release:

- named constants (`const` declarations), so that array dimensions can use
  names;
- dead-code elimination and constant folding;
- loop transformations, tiling and fusion;
- SIMD vectorization and multicore execution;
- vectors, slices and higher-level tensor types;
- kernels and GPU code generation;
- optimization diagnostics, cost models and auto-tuning;
- modules, packages, generics, traits and advanced ownership.

The first line is the first language addition planned after v0.1. The next two
are classic compiler optimization. The LLVM tutorial's
chapter 4 is a gentle introduction: it starts with trivial constant folding
and then adds a handful of standard passes that clean up generated
code.[^kal4] Everything you release in v0.1 becomes the known answer those
optimizations must preserve, which is exactly how the
[philosophy page](../../philosophy.md) describes the first milestone after
v0.1: make the straightforward matrix multiplication faster "while preserving
its semantics".

The later lines point toward tensors and other hardware. The MLIR Toy
tutorial is the closest worked example of that direction. Its language
computes on tensors with shapes inferred at compile time, and its chapters
lower the program step by step, through a dialect meant for loop
optimization, down to LLVM.[^toy] It stops at CPU code, but it shows the kind
of layered design that Vortex's architecture page already anticipates when it
says GPU lowering "must reuse the same validated language semantics".

Whatever comes next, the release you just made is the ground it stands on. A
faster compiler, a GPU backend or a richer type system is only trustworthy if
it can be checked against a v0.1 that is correct and says clearly what it
does.

[^ghuloum]: Abdulaziz Ghuloum, "An Incremental Approach to Compiler Construction", *Proceedings of the 2006 Scheme and Functional Programming Workshop*, University of Chicago Technical Report TR-2006-06, sections 2.6, "Development Methodology", and 2.7, "Testing Infrastructure". <http://scheme2006.cs.uchicago.edu/11-ghuloum.pdf>
[^sandler-blog]: Nora Sandler, "Writing a C Compiler, Part 1", 29 November 2017. <https://norasandler.com/2017/11/29/Write-a-Compiler.html>
[^lit]: LLVM Project, "lit - LLVM Integrated Tester". <https://llvm.org/docs/CommandGuide/lit.html>
[^filecheck]: LLVM Project, "FileCheck - Flexible pattern matching file verifier". <https://llvm.org/docs/CommandGuide/FileCheck.html>
[^kal4]: LLVM Project, "My First Language Frontend with LLVM Tutorial", chapter 4, "Adding JIT and Optimizer Support". <https://llvm.org/docs/tutorial/MyFirstLanguageFrontend/LangImpl04.html>
[^toy]: LLVM Project, "Toy Tutorial", MLIR documentation. <https://mlir.llvm.org/docs/Tutorials/Toy/>

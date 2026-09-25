# O12. Testing an optimizer

<p class="page-intro">An optimizer is a program that rewrites programs, and its bugs change other people's answers without a word of warning. This chapter builds the tests that catch them: checks inside the compiler, pinned outputs, two runs compared, programs generated and mutated by the thousand, and the reducer and bisector that turn a failure into a bug report a few lines long. Together they become the gate every Vortex optimization pass must pass before it merges.</p>

<p class="vx-meta" markdown="1">Level: Advanced · Reading time: about 45 minutes · Builds on: [O1. The optimizer's contract](o1-optimizer-contract.md), [11. Release](../compiler/guide/stage-11-release.md)</p>

???+ remember "Before you start, remember"

    ??? question "Which three things must a Vortex optimization preserve on every run?"

        What the program writes to standard output, the runtime error line it writes to standard error, and its exit status. Every bit of every floating-point result is fixed too, because printing and decision 56 make the bits observable.

        Introduced in [O1. The optimizer's contract](o1-optimizer-contract.md#vortexs-list).

    ??? question "What is a differential test, and what can it never do?"

        Compiling the same program two ways, for example with optimization off and on, and comparing what the two executables do. It finds counterexamples, but it never shows that none exist.

        Introduced in [O1. The optimizer's contract](o1-optimizer-contract.md#testing-finds-bugs-proofs-rule-them-out).

    ??? question "What does translation validation check, and what does it not check?"

        That one run of the compiler kept the meaning of one function, by comparing the function before and after. It does not prove the compiler correct for every program.

        Introduced in [O1. The optimizer's contract](o1-optimizer-contract.md#hand-it-to-a-solver).

    ??? question "What is a regression, and how is one caught?"

        Something that used to work and stopped working because of a later change. Running every earlier test after every change catches it.

        Introduced in [11. Release](../compiler/guide/stage-11-release.md#words-for-this-stage).

!!! goals "In this chapter"

    - Explain what a test oracle is, and name the oracle behind each kind of optimizer test.
    - Write pass tests, golden tests and invariant checks, and say what each can and cannot catch.
    - Build a differential test on generated programs, and explain why Vortex's contract lets its generator skip the work Csmith spends on undefined behavior.
    - Apply equivalence modulo inputs to turn one program and one input into many tests.
    - Reduce a failing program to a few lines and bisect a pipeline to the one transformation that broke it.

## A rewrite that is right most of the time

Dividing by 2 is slower than shifting right by one bit on most processors, so an optimizer is tempted to replace `x / 2` with `x >> 1`. In Vortex the two agree for many values but not all. Integer `/` truncates toward zero, so `-7 / 2` is -3, while `>>` on a signed integer is an arithmetic shift, which rounds toward negative infinity, so `-7 >> 1` is -4 ([Expressions 5.5](../specification/expressions.md#55-arithmetic-and-bitwise-expressions)). Figure 1 shows the pattern: the two differ exactly when `x` is negative and odd.

<figure class="vx-figure">
<svg viewBox="0 0 760 210" role="img" aria-label="A table of x from -4 to 4 with x / 2 and x >> 1 below each value; the two disagree only at -3 and -1" aria-describedby="o12-f1-desc">
<title id="o12-f1-title">Division by 2 against a right shift</title>
<desc id="o12-f1-desc">Nine columns, for x equal to -4, -3, -2, -1, 0, 1, 2, 3 and 4. The second row gives x / 2 rounded toward zero: -2, -1, -1, 0, 0, 0, 1, 1, 2. The third row gives x >> 1, rounded toward negative infinity: -2, -2, -1, -1, 0, 0, 1, 1, 2. The columns for -3 and -1 are drawn dashed, because there the rows differ by one. Every other column agrees.</desc>
<text class="vx-text-muted" x="20" y="54">x</text>
<text class="vx-text-muted" x="20" y="104">x / 2</text>
<text class="vx-text-muted" x="20" y="154">x &gt;&gt; 1</text>
<rect class="vx-box-bad" x="212" y="30" width="56" height="140" rx="4"/>
<rect class="vx-box-bad" x="352" y="30" width="56" height="140" rx="4"/>
<g class="vx-mono">
<text x="170" y="54" text-anchor="middle">-4</text><text x="240" y="54" text-anchor="middle">-3</text><text x="310" y="54" text-anchor="middle">-2</text><text x="380" y="54" text-anchor="middle">-1</text><text x="450" y="54" text-anchor="middle">0</text><text x="520" y="54" text-anchor="middle">1</text><text x="590" y="54" text-anchor="middle">2</text><text x="660" y="54" text-anchor="middle">3</text><text x="730" y="54" text-anchor="middle">4</text>
</g>
<text class="vx-mono" x="170" y="104" text-anchor="middle">-2</text><text class="vx-mono" x="240" y="104" text-anchor="middle">-1</text><text class="vx-mono" x="310" y="104" text-anchor="middle">-1</text><text class="vx-mono" x="380" y="104" text-anchor="middle">0</text><text class="vx-mono" x="450" y="104" text-anchor="middle">0</text><text class="vx-mono" x="520" y="104" text-anchor="middle">0</text><text class="vx-mono" x="590" y="104" text-anchor="middle">1</text><text class="vx-mono" x="660" y="104" text-anchor="middle">1</text><text class="vx-mono" x="730" y="104" text-anchor="middle">2</text>
<text class="vx-mono" x="170" y="154" text-anchor="middle">-2</text><text class="vx-text-accent" x="240" y="154" text-anchor="middle">-2</text><text class="vx-mono" x="310" y="154" text-anchor="middle">-1</text><text class="vx-text-accent" x="380" y="154" text-anchor="middle">-1</text><text class="vx-mono" x="450" y="154" text-anchor="middle">0</text><text class="vx-mono" x="520" y="154" text-anchor="middle">0</text><text class="vx-mono" x="590" y="154" text-anchor="middle">1</text><text class="vx-mono" x="660" y="154" text-anchor="middle">1</text><text class="vx-mono" x="730" y="154" text-anchor="middle">2</text>
<text class="vx-text-muted" x="20" y="198">Truncation moves a negative odd x up toward zero; the shift moves it down.</text>
</svg>
<figcaption>Figure 1. <code>x / 2</code> and <code>x &gt;&gt; 1</code> for x from -4 to 4. The dashed columns are the only places they differ: a negative odd x, where truncation and the shift round in opposite directions.</figcaption>
</figure>

A bug like this does not announce itself. It sits in a folding or strength-reduction routine and waits for a negative odd value to reach a division by the literal 2. A hand-written test suite that tries `x` = 10, 3, 0, -4 and -10 passes every case, because -4 and -10 are even. The rule looks right until someone remembers what happens below zero, and then only for half the numbers there.

LLVM's own optimizer shows what the correct version looks like. The first example sends four divisions by 2 through InstCombine, the pass of local algebraic simplifications, and keeps its output:

--8<-- "includes/examples/optimize/o12-testing-optimizers/halve.ll.md"

Only three of the four became shifts. `@half` keeps its `sdiv`, because `%x` may be negative and odd. `@half_exact` becomes `ashr exact`: the **`exact`** flag says that if the division would round, the result is poison, so the shift only has to agree where nothing rounds ([O11](o11-undefined-behavior.md)).[^langref] `@half_masked` becomes a logical shift, because after `and i32 %x, 1023` the value is never negative, and `@half_unsigned` was never signed. The rewrite is correct under a **precondition**, a fact about the operands that must hold first. A buggy optimizer is often a correct rule with its precondition missing.

??? check "A colleague tests the rewrite `x / 2` to `x >> 1` with x = 10, 3, 0, -4 and -10, and every case passes. Which one extra input would you add, and why does the suite miss the bug?"

    Any negative odd number, such as -3 or -7. The suite's two negative inputs are both even, and for an even x the division does not round, so truncation and the shift agree. The bug lives only where both conditions hold at once, which is why hand-picked inputs miss it.

## What a test needs: an input and an oracle

Every test in this chapter has two parts. The **input** is the program, and sometimes the data, the optimizer is given. The **oracle** is whatever decides whether the result is right. The example above shows where the difficulty lies: inputs cost little to produce, but deciding what the correct output of an optimization is takes as much knowledge as writing the optimization.

Optimizer testing uses three kinds of oracle, and each technique below is one of them paired with a source of inputs.

- **A stored answer.** Someone checked the output once, by hand, and saved it. Every later run is compared with the copy. Pass tests and golden tests work this way.
- **A second run.** Another way of computing the same thing, such as the program with optimization off, must agree. Differential testing, random program generation and equivalence modulo inputs work this way.
- **A property.** Something that must hold of every output, such as "every use is dominated by its definition". Verifiers and invariant checks work this way, and translation validation checks the strongest property of all, that the output refines the input.

A stored answer is precise but covers only the inputs someone wrote down. A second run covers any input a machine can produce, but only finds disagreements, and two runs that are wrong in the same way agree. A property needs no second implementation, but it catches only the bugs that break it.

## Checks inside the compiler

The cheapest oracle runs inside the compiler itself. An **invariant** is a rule the intermediate representation must satisfy at all times: in SSA form, every value has one definition, and that definition dominates every use ([O3](o3-ssa.md#for-vortex)). A **verifier** is a function that checks every invariant of the IR and reports the first one broken. Run after every pass in testing builds, it turns "the compiler crashed three passes later" into "this pass broke this rule on this instruction". LLVM's `opt` has an option for exactly this, `-verify-each`, which adds a verifier run after every pass.[^opt]

A second family checks what analyses claim against what programs do. An analysis that says a loop index lies between 0 and 63 ([O4](o4-dataflow.md#intervals-bounding-a-loop-index)) makes a claim about every run. In a testing build, the compiler can emit that claim as a check of its own, whose failure is an internal error rather than a program error. [O8](o8-loops.md#for-vortex) calls this a soundness mode: every bounds check the optimizer proved unnecessary is put back, and if one of them ever fails, the proof was wrong. LLVM tests analyses in a related way, by printing an analysis result as text for a checked comparison.[^testing]

A third property concerns passes that run to a fixed point, such as constant propagation ([O5](o5-constants-and-dead-code.md)). If such a pass has finished, running it a second time must change nothing. A second run that does change something means the first stopped early, or that the result depends on the order in which the pass visited the code.

These checks catch many broken rules, but not wrong answers from well-formed code. A pass that folds `-7 / 2` to -4 produces perfectly valid IR. For that, the oracle has to know what the program means.

## Pass tests and golden tests

The `halve.ll` example above is a **pass test**: one small input, one pass, and the output compared with a copy someone checked by hand. LLVM's regression tests are files of this kind, each run by a command line written in the file, with the output examined by FileCheck, a tool that matches chosen lines instead of the whole text.[^testing] [E4](../backend/e4-testing-backends.md#one-test-read-line-by-line) reads such a test line by line. The idea does not depend on LLVM: a Vortex pass test is a lowered function, the pass under test, and its checked output.

A **golden test** is the same idea applied to a whole program: run it once, check the output by hand, save it (the **golden file**), and compare every later run with it byte for byte. O1's exercise has one, the remark stream for the stage 10 kernel ([O1](o1-optimizer-contract.md#remarks-the-optimizers-report)), and [O10](o10-pass-pipelines.md#for-vortex) adds another, which shows that swapping two independent passes changes no remark.

A golden test cannot tell you that the saved output was right; only the person who checked it once can. What it buys afterward is cheap protection against a regression. When a reduced bug report is fixed, the reduced program becomes a golden test, as LLVM's testing guide asks for its own bugs, so that the same bug cannot come back unnoticed.[^testing]

The danger of golden files is updating them. When an intended change alters an output, someone replaces the golden file, and a replacement nobody reads blesses whatever the compiler now prints, including a new bug. A golden file changes only in a commit that shows the difference and says why it is correct.

## Comparing two runs: differential testing

A pass test covers the inputs someone wrote. To cover inputs nobody wrote, the oracle must work on any program, and the one that does is a second run. **Differential testing** runs the same program two ways and compares what happens ([O1](o1-optimizer-contract.md#testing-finds-bugs-proofs-rule-them-out)). The two ways can be many things:

- the whole optimizer off and on, which is O1's contract test;
- one pass or one parameter off and on, as the exercises of [O7](o7-inlining-and-sroa.md#for-vortex) (inlining), [O9](o9-alias-analysis.md#for-vortex) (alias attributes) and [P15](p15-choosing-parameters.md#for-vortex) (tuned parameters) ask;
- an interpreter of the IR against the compiled program, as in [O3](o3-ssa.md#for-vortex)'s round-trip test;
- two back ends, or two machines, which [E4](../backend/e4-testing-backends.md#differential-testing-across-back-ends) covers.

The comparator never needs to know why two runs might disagree, only what counts as agreement. For Vortex, that is the contract's list: the same bytes on standard output, the same runtime error line, and the same exit status. Floating-point output needs no tolerance. Decision 56 forbids the rewrites that change bits ([decision 56](../decisions/numbers.md#d56)), and `print` writes the shortest decimal that reads back as the same value, so equal bytes mean equal bits (except which NaN a computation produced, which O1 counts as unobservable). A matrix product with one wrong element in its last bit is a failed test.

The second example generates small integer expressions from a fixed seed and evaluates each one twice: once by the rules, and once by an "optimizer" that uses the shift. It stops at the first disagreement.

--8<-- "includes/examples/optimize/o12-testing-optimizers/differential_test.cpp.md"

Follow program 23 by hand. By the rules, `-3 / 2` is -1, so the middle `(-1 - -1)` is 0, `8 - 0` is 8, and the product is `8 * -1`, which is -8. The optimizer computes `-3 >> 1` as -2 in all three places. The middle becomes `(-2 - -2)`, still 0: two wrong values cancelled. The third does not cancel, and the product is `8 * -2`, which is -16.

The last line of output matters as much as the mismatch. Five earlier programs divided by 2 and agreed, because none of them divided a negative number: their dividends were 0, 5, 2, 2 and 2. A random test finds a bug only when an input happens to meet its condition, and so a harness runs thousands of programs, not dozens.

Two practical rules come with any comparator. First, a generated program may run forever, so each run has a time limit, and a run that exceeds it in only one of the two builds is a mismatch: an optimizer that makes an endless loop finish has changed the behavior ([O5](o5-constants-and-dead-code.md#loops-that-may-not-end)). Second, the comparison must be deterministic: the same seed must produce the same programs, or a failure found once cannot be reproduced.

A differential test checks only what differs between the two builds. If the front end lowers `-7 / 2` to -4, the unoptimized and optimized builds share the mistake and agree. Pass tests, golden files and the specification's own examples catch what both builds share.

## Random programs: testing what nobody thought to write

Hand-written tests contain only the bugs their authors imagined. The remedy is to generate programs by machine, which needs a generator whose programs are well formed and whose outputs are meaningful.

Csmith, from 2011, generates random C programs, compiles each with several compilers, runs them, and compares their outputs; each program ends by printing a checksum of its global variables. Over three years its authors reported more than 325 previously unknown bugs, and every compiler they tested both crashed and silently generated wrong code on valid input.[^csmith] One wrong-code bug in GCC is close to this chapter's example: a fold of a division comparison, `(x / c1) != c2`, whose overflow check misfired and folded `(x / -1) != 1` to 0.[^csmith]

Csmith aims at the **middle end**, the passes that transform the intermediate representation. Of the 79 GCC bugs in the paper's table, 49 were in the middle end, and of LLVM's 202, 75 were in the middle end and 74 in the back end.[^csmith]

Its hardest engineering problem is **undefined behavior**. A C program that overflows a signed integer, reads past an array or reads an uninitialized variable has no defined output, so two compilers that disagree on it are both right. Csmith avoids each such behavior with an analysis when it generates code, a check when the code runs, or both. Integer arithmetic goes through **safe math wrappers**, functions that return a defined result instead of overflowing, and array indices are forced into bounds.[^csmith] About 10% of its programs apparently never terminate; the authors kept them, because a compiler that makes such a program terminate has a bug, and handled them with time limits.[^csmith]

YARPGen took the problem further. Its first version, from 2020, avoids undefined behavior without any checks at run time, by tracking what values each expression can hold as it generates it. It also adds **generation policies**, choices that make particular optimizations more likely to apply, and its authors report more than 220 bugs in GCC, LLVM and the Intel C++ compiler.[^yarp20] The 2023 version targets loop optimizers: it avoids undefined behavior statically in generated loops, varies loop code in ways that trigger loop optimizations more often, and found 122 bugs in C++ and data-parallel compilers.[^yarp23]

A Vortex generator starts in a better place. [Conformance 1.5](../specification/conformance.md#15-undefined-behavior) says a well-formed Vortex program has no undefined behavior: every invalid operation is rejected by the compiler or fails with the documented runtime error. So every well-typed generated program is a fair test, and a program that overflows or indexes out of bounds is not thrown away. Its error line and exit status are outputs like any other.

The difficulty moves elsewhere. A random program that overflows in its third statement tests almost nothing, because everything after the failure never runs. Most of a Vortex generator's effort should go where YARPGen's does: keeping values in range, so that most programs run to the end, and aiming at the code the optimizer cares about. For Vortex that means loop nests over fixed-shape arrays like the stage 10 kernel, with a planned minority of programs that fail a check on purpose in the first, a middle or the last iteration, which is [O8](o8-loops.md#for-vortex)'s failing-check suite.

Stack exhaustion needs a rule of its own, because O1 left open whether it must happen at the same point in both builds; the generator can avoid deep recursion, or the comparator can follow your answer.

??? check "Csmith spends much of its complexity on avoiding undefined behavior. What does a Vortex generator spend that effort on instead, and why?"

    On keeping programs running and aiming them at optimizations: keeping values in range so that checks rarely fail early, and generating loop nests like the kernel's. It does not need to avoid invalid operations, because Vortex has no undefined behavior; an overflow is a defined runtime error, so a program that fails is still a fair test, only a weak one if it fails before reaching the interesting code.

## Equivalence modulo inputs: new programs from old ones

A generator must know the whole language. **Equivalence modulo inputs** (EMI), from 2014, gets new test programs from existing ones instead. Run a program P on an input I and record which statements executed, a **coverage profile**. Any statement that did not run on I can be deleted without changing what P does on I. Every such deletion gives a **variant**, a program that must print exactly what P prints on I. Compile each variant, run it on I, and any difference is a compiler bug.[^emi14]

The oracle is again a second run, but the two programs are no longer "unoptimized and optimized". They are two different programs that are equivalent on one input, and both go through the optimizer. The point is that a deletion changes what the optimizer sees. In one of the paper's examples, a program from GCC's test suite compiled correctly, but deleting unexecuted code made one function small enough for Clang to inline it, and after inlining, an interaction between GVN and SROA produced wrong code.[^emi14]

The third example plays this out on a toy language with one variable. Its optimizer has one rule, with a flaw: it deletes an unguarded `x = x / 2` that is immediately followed by `x = x * 2`, as if they cancelled.

--8<-- "includes/examples/optimize/o12-testing-optimizers/emi_prune.cpp.md"

Walk through input 3. `s0` sets x to 7 and `s1` halves it to 3. `s2` runs only when the input exceeds 5, so it is skipped. `s3` doubles x to 6, and `s4`, guarded by 9, is skipped. The original prints 6, and the optimizer leaves it alone, because `s2` sits between the division and the multiplication. A differential test of this program passes.

Figure 2 shows what EMI adds. Deleting `s2`, which never ran, makes `s1` and `s3` adjacent. The optimizer now deletes both, x stays 7, and the variant prints 7 instead of 6. Deleting only `s4` changes nothing, because the optimizer's rule never looks there. Two of the four variants expose the bug, and the original program never could.

<figure class="vx-figure">
<svg viewBox="0 0 760 330" role="img" aria-label="The toy program with its coverage for input 3, and the variant that deletes the unexecuted statement s2, which makes the optimizer's faulty rule apply" aria-describedby="o12-f2-desc">
<title id="o12-f2-title">An EMI variant exposes a faulty rule</title>
<desc id="o12-f2-desc">Left, the original program for input 3: s0 x = x + 7 ran, s1 x = x / 2 ran, s2 if input greater than 5 x = x + 1 did not run and is drawn dashed, s3 x = x * 2 ran, s4 if input greater than 9 x = x + 4 did not run and is drawn dashed. The optimizer's rule does not apply because s2 separates s1 and s3, and the result is 6. An arrow labelled delete s2, it never ran, leads to the right, the variant: s0, s1, s3, s4. Now s1 and s3 are adjacent and are bracketed as the pair the rule deletes. The optimized variant computes 7 instead of 6.</desc>
<defs><marker id="o12-f2-head" viewBox="0 0 10 10" refX="10" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker></defs>
<text class="vx-text" x="20" y="26">Original, input 3</text>
<text class="vx-text-muted" x="20" y="44">solid ran, dashed never ran</text>
<rect class="vx-box-strong" x="20" y="60" width="270" height="30" rx="4"/><text class="vx-mono" x="32" y="80">s0  x = x + 7</text>
<rect class="vx-box-strong" x="20" y="98" width="270" height="30" rx="4"/><text class="vx-mono" x="32" y="118">s1  x = x / 2</text>
<rect class="vx-box-bad" x="20" y="136" width="270" height="30" rx="4"/><text class="vx-mono" x="32" y="156">s2  if in &gt; 5: x = x + 1</text>
<rect class="vx-box-strong" x="20" y="174" width="270" height="30" rx="4"/><text class="vx-mono" x="32" y="194">s3  x = x * 2</text>
<rect class="vx-box-bad" x="20" y="212" width="270" height="30" rx="4"/><text class="vx-mono" x="32" y="232">s4  if in &gt; 9: x = x + 4</text>
<text class="vx-text-muted" x="20" y="270">s2 separates s1 and s3: the rule</text>
<text class="vx-text-muted" x="20" y="288">does not fire. Optimized x = 6.</text>
<path class="vx-flow" d="M300 151 L440 151" marker-end="url(#o12-f2-head)"/>
<text class="vx-text-accent" x="312" y="138">delete s2</text>
<text class="vx-text-muted" x="312" y="176">it never ran</text>
<text class="vx-text" x="460" y="26">Variant, input 3</text>
<text class="vx-text-muted" x="460" y="44">must still print 6</text>
<rect class="vx-box-strong" x="460" y="60" width="270" height="30" rx="4"/><text class="vx-mono" x="472" y="80">s0  x = x + 7</text>
<rect class="vx-box-accent" x="460" y="98" width="270" height="30" rx="4"/><text class="vx-mono" x="472" y="118">s1  x = x / 2</text>
<rect class="vx-box-accent" x="460" y="136" width="270" height="30" rx="4"/><text class="vx-mono" x="472" y="156">s3  x = x * 2</text>
<rect class="vx-box-bad" x="460" y="174" width="270" height="30" rx="4"/><text class="vx-mono" x="472" y="194">s4  if in &gt; 9: x = x + 4</text>
<path class="vx-line" d="M740 102 L750 102 L750 162 L740 162"/>
<text class="vx-text-muted" x="460" y="232">s1 and s3 are now adjacent, and the</text>
<text class="vx-text-muted" x="460" y="250">rule deletes both as if they cancelled.</text>
<text class="vx-text-accent" x="460" y="288">Optimized x = 7: miscompiled</text>
</svg>
<figcaption>Figure 2. Equivalence modulo inputs on the third example. Deleting a statement that never ran on input 3 cannot change what the program prints for input 3, but it changes what the optimizer sees. Here it brings a division and a multiplication together, and the faulty rule fires.</figcaption>
</figure>

EMI's appeal is its economy. The paper's tool, Orion, collected coverage with `gcov` from a build at `-O0 -coverage` and consisted of about 500 lines of shell scripts and 1,000 lines of C++, against the 30,000 to 40,000 lines of C++ in Csmith; in eleven months it produced 147 confirmed, unique bug reports for GCC and LLVM, most of them miscompilations.[^emi14] Its variants are mostly real code, so the bugs they find are likely to matter. It also combines with generation: EMI can take generated programs as its starting point, and the paper did so with Csmith's.[^emi14]

??? check "In the third example, why can a plain differential test of the original program never find the bug, when EMI does?"

    Because on the original program the optimizer's faulty rule never fires: `s2` sits between the division and the multiplication, so the optimized and unoptimized builds agree. The bug needs an input program with the two statements adjacent. EMI builds that program by deleting `s2`, which is safe for input 3 because it never ran, so the variant must still print 6.

## Test-case reduction: from a page to a sentence

A generated program that fails is rarely small. The failing statement sits among hundreds of lines that have nothing to do with the bug, and reading them costs more than the generator saved. **Test-case reduction** deletes parts of a failing program, keeping each deletion only if the smaller program is still **interesting**: it still shows the failure.

Zeller and Hildebrandt made this automatic in 2002 with **delta debugging**, whose algorithm `ddmin` repeatedly tries removing parts of a failing input and keeps any smaller input that still fails. In one case study it simplified 896 lines of HTML that crashed Mozilla to the single line that caused the failure.[^ddmin] It aims for a **1-minimal** result, one from which no single part can be removed without losing the failure. It also names a third outcome besides "fails" and "passes": **unresolved**, a candidate on which the test could not decide at all.[^ddmin]

For compilers, the dangerous candidate is one that should count as unresolved but looks interesting. Deleting a line of C can leave a variable used without a value, and the smaller program may still print the wrong answer, now for a reason that proves nothing: its behavior is undefined. The compiler teams the authors worked with ignore any report that depends on an uninitialized variable. Regehr and colleagues named this the **test-case validity problem** in 2012. Their reducer, C-Reduce, runs a set of pluggable transformations until none makes progress (a **fixed point**). Besides deleting lines, the transformations change identifiers and integer constants to 0 or 1, remove an operator and one of its operands, and inline small functions. On average its output was more than 25 times smaller than a line-based delta debugger's.[^creduce12]

The fourth example reduces a nine-line program in the toy language of this chapter's first bug. "Interesting" means two things at once: the program is valid, so no line reads a name that no earlier line assigned, and the run with the shift still prints something different from the run without it.

--8<-- "includes/examples/optimize/o12-testing-optimizers/reduce.cpp.md"

Figure 3 lays the log out as a grid. The reducer walks the lines from top to bottom and tries deleting each. In pass 1, deleting `a = 5` would leave `b = a - 8` reading a name that no longer exists, so the candidate is invalid and the line stays. Deleting `print c` gives a valid program without the mismatch, so that line is needed. Only `print e` and `print f` go.

<figure class="vx-figure">
<svg viewBox="0 0 760 400" role="img" aria-label="A grid of the nine starting lines against five reduction passes, showing in each pass whether deleting a line made the program invalid, lost the mismatch, or was kept" aria-describedby="o12-f3-desc">
<title id="o12-f3-title">Five passes of the line reducer</title>
<desc id="o12-f3-desc">Nine rows, one per line of the starting program, and five columns, one per pass of the reducer. Pass 1 removes print e and print f. Pass 2 removes f = e / 2, pass 3 removes e = d * 3 and pass 4 removes d = 4; each removal is possible only because the line that read its result went in the pass before. In every pass, deleting a = 5, b = a - 8 or c = b / 2 would leave a line reading a name that no longer exists, so those deletions are invalid, and deleting print c loses the mismatch. Pass 5 removes nothing, so the reducer stops with four lines.</desc>
<text class="vx-text-muted" x="20" y="30">line</text>
<text class="vx-text" x="220" y="30" text-anchor="middle">pass 1</text>
<text class="vx-text" x="332" y="30" text-anchor="middle">pass 2</text>
<text class="vx-text" x="444" y="30" text-anchor="middle">pass 3</text>
<text class="vx-text" x="556" y="30" text-anchor="middle">pass 4</text>
<text class="vx-text" x="668" y="30" text-anchor="middle">pass 5</text>
<text class="vx-mono" x="20" y="77">a = 5</text>
<g class="vx-seq" style="--vx-i: 0; --vx-n: 5"><rect class="vx-box" x="170" y="62" width="100" height="24" rx="3"/><text class="vx-text-muted" x="220" y="78" text-anchor="middle">invalid</text></g>
<g class="vx-seq" style="--vx-i: 1; --vx-n: 5"><rect class="vx-box" x="282" y="62" width="100" height="24" rx="3"/><text class="vx-text-muted" x="332" y="78" text-anchor="middle">invalid</text></g>
<g class="vx-seq" style="--vx-i: 2; --vx-n: 5"><rect class="vx-box" x="394" y="62" width="100" height="24" rx="3"/><text class="vx-text-muted" x="444" y="78" text-anchor="middle">invalid</text></g>
<g class="vx-seq" style="--vx-i: 3; --vx-n: 5"><rect class="vx-box" x="506" y="62" width="100" height="24" rx="3"/><text class="vx-text-muted" x="556" y="78" text-anchor="middle">invalid</text></g>
<g class="vx-seq" style="--vx-i: 4; --vx-n: 5"><rect class="vx-box" x="618" y="62" width="100" height="24" rx="3"/><text class="vx-text-muted" x="668" y="78" text-anchor="middle">invalid</text></g>
<text class="vx-mono" x="20" y="107">b = a - 8</text>
<g class="vx-seq" style="--vx-i: 0; --vx-n: 5"><rect class="vx-box" x="170" y="92" width="100" height="24" rx="3"/><text class="vx-text-muted" x="220" y="108" text-anchor="middle">invalid</text></g>
<g class="vx-seq" style="--vx-i: 1; --vx-n: 5"><rect class="vx-box" x="282" y="92" width="100" height="24" rx="3"/><text class="vx-text-muted" x="332" y="108" text-anchor="middle">invalid</text></g>
<g class="vx-seq" style="--vx-i: 2; --vx-n: 5"><rect class="vx-box" x="394" y="92" width="100" height="24" rx="3"/><text class="vx-text-muted" x="444" y="108" text-anchor="middle">invalid</text></g>
<g class="vx-seq" style="--vx-i: 3; --vx-n: 5"><rect class="vx-box" x="506" y="92" width="100" height="24" rx="3"/><text class="vx-text-muted" x="556" y="108" text-anchor="middle">invalid</text></g>
<g class="vx-seq" style="--vx-i: 4; --vx-n: 5"><rect class="vx-box" x="618" y="92" width="100" height="24" rx="3"/><text class="vx-text-muted" x="668" y="108" text-anchor="middle">invalid</text></g>
<text class="vx-mono" x="20" y="137">d = 4</text>
<g class="vx-seq" style="--vx-i: 0; --vx-n: 5"><rect class="vx-box" x="170" y="122" width="100" height="24" rx="3"/><text class="vx-text-muted" x="220" y="138" text-anchor="middle">invalid</text></g>
<g class="vx-seq" style="--vx-i: 1; --vx-n: 5"><rect class="vx-box" x="282" y="122" width="100" height="24" rx="3"/><text class="vx-text-muted" x="332" y="138" text-anchor="middle">invalid</text></g>
<g class="vx-seq" style="--vx-i: 2; --vx-n: 5"><rect class="vx-box" x="394" y="122" width="100" height="24" rx="3"/><text class="vx-text-muted" x="444" y="138" text-anchor="middle">invalid</text></g>
<g class="vx-seq" style="--vx-i: 3; --vx-n: 5"><rect class="vx-box-bad" x="506" y="122" width="100" height="24" rx="3"/><text class="vx-text-accent" x="556" y="138" text-anchor="middle">removed</text></g>
<text class="vx-mono" x="20" y="167">e = d * 3</text>
<g class="vx-seq" style="--vx-i: 0; --vx-n: 5"><rect class="vx-box" x="170" y="152" width="100" height="24" rx="3"/><text class="vx-text-muted" x="220" y="168" text-anchor="middle">invalid</text></g>
<g class="vx-seq" style="--vx-i: 1; --vx-n: 5"><rect class="vx-box" x="282" y="152" width="100" height="24" rx="3"/><text class="vx-text-muted" x="332" y="168" text-anchor="middle">invalid</text></g>
<g class="vx-seq" style="--vx-i: 2; --vx-n: 5"><rect class="vx-box-bad" x="394" y="152" width="100" height="24" rx="3"/><text class="vx-text-accent" x="444" y="168" text-anchor="middle">removed</text></g>
<text class="vx-mono" x="20" y="197">print e</text>
<g class="vx-seq" style="--vx-i: 0; --vx-n: 5"><rect class="vx-box-bad" x="170" y="182" width="100" height="24" rx="3"/><text class="vx-text-accent" x="220" y="198" text-anchor="middle">removed</text></g>
<text class="vx-mono" x="20" y="227">c = b / 2</text>
<g class="vx-seq" style="--vx-i: 0; --vx-n: 5"><rect class="vx-box" x="170" y="212" width="100" height="24" rx="3"/><text class="vx-text-muted" x="220" y="228" text-anchor="middle">invalid</text></g>
<g class="vx-seq" style="--vx-i: 1; --vx-n: 5"><rect class="vx-box" x="282" y="212" width="100" height="24" rx="3"/><text class="vx-text-muted" x="332" y="228" text-anchor="middle">invalid</text></g>
<g class="vx-seq" style="--vx-i: 2; --vx-n: 5"><rect class="vx-box" x="394" y="212" width="100" height="24" rx="3"/><text class="vx-text-muted" x="444" y="228" text-anchor="middle">invalid</text></g>
<g class="vx-seq" style="--vx-i: 3; --vx-n: 5"><rect class="vx-box" x="506" y="212" width="100" height="24" rx="3"/><text class="vx-text-muted" x="556" y="228" text-anchor="middle">invalid</text></g>
<g class="vx-seq" style="--vx-i: 4; --vx-n: 5"><rect class="vx-box" x="618" y="212" width="100" height="24" rx="3"/><text class="vx-text-muted" x="668" y="228" text-anchor="middle">invalid</text></g>
<text class="vx-mono" x="20" y="257">f = e / 2</text>
<g class="vx-seq" style="--vx-i: 0; --vx-n: 5"><rect class="vx-box" x="170" y="242" width="100" height="24" rx="3"/><text class="vx-text-muted" x="220" y="258" text-anchor="middle">invalid</text></g>
<g class="vx-seq" style="--vx-i: 1; --vx-n: 5"><rect class="vx-box-bad" x="282" y="242" width="100" height="24" rx="3"/><text class="vx-text-accent" x="332" y="258" text-anchor="middle">removed</text></g>
<text class="vx-mono" x="20" y="287">print c</text>
<g class="vx-seq" style="--vx-i: 0; --vx-n: 5"><rect class="vx-box-strong" x="170" y="272" width="100" height="24" rx="3"/><text class="vx-text" x="220" y="288" text-anchor="middle">needed</text></g>
<g class="vx-seq" style="--vx-i: 1; --vx-n: 5"><rect class="vx-box-strong" x="282" y="272" width="100" height="24" rx="3"/><text class="vx-text" x="332" y="288" text-anchor="middle">needed</text></g>
<g class="vx-seq" style="--vx-i: 2; --vx-n: 5"><rect class="vx-box-strong" x="394" y="272" width="100" height="24" rx="3"/><text class="vx-text" x="444" y="288" text-anchor="middle">needed</text></g>
<g class="vx-seq" style="--vx-i: 3; --vx-n: 5"><rect class="vx-box-strong" x="506" y="272" width="100" height="24" rx="3"/><text class="vx-text" x="556" y="288" text-anchor="middle">needed</text></g>
<g class="vx-seq" style="--vx-i: 4; --vx-n: 5"><rect class="vx-box-strong" x="618" y="272" width="100" height="24" rx="3"/><text class="vx-text" x="668" y="288" text-anchor="middle">needed</text></g>
<text class="vx-mono" x="20" y="317">print f</text>
<g class="vx-seq" style="--vx-i: 0; --vx-n: 5"><rect class="vx-box-bad" x="170" y="302" width="100" height="24" rx="3"/><text class="vx-text-accent" x="220" y="318" text-anchor="middle">removed</text></g>
<rect class="vx-box-bad" x="20" y="354" width="22" height="14" rx="3"/><text class="vx-text-muted" x="48" y="366">deleted: still valid, still mismatched</text>
<rect class="vx-box-strong" x="330" y="354" width="22" height="14" rx="3"/><text class="vx-text-muted" x="358" y="366">kept: the mismatch needs it</text>
<rect class="vx-box" x="560" y="354" width="22" height="14" rx="3"/><text class="vx-text-muted" x="588" y="366">kept: a later line reads it</text>
</svg>
<figcaption>Figure 3. The fourth example's log as a grid. Each column is one pass from top to bottom; a blank cell is a line already gone. Every line that goes is freed by the removal of the line that read it, one pass earlier, which is why the chain <code>print f</code>, <code>f</code>, <code>e</code>, <code>d</code> takes four passes.</figcaption>
</figure>

The later passes each remove one more line: `f = e / 2` could only go once `print f` had gone, then `e = d * 3`, then `d = 4`. Pass 5 removes nothing, and the result is 1-minimal: `a = 5`, `b = a - 8`, `c = b / 2`, `print c`. Deleting lines alone cannot go further, but a C-Reduce-style transformation that replaces `a` in `b = a - 8` with its value would. Three lines, `b = -3`, `c = b / 2` and `print c`, would make a complete bug report.

Three rules follow for any reducer. The interestingness test must be a program, because a reducer calls it once per candidate. It must check for the same failure, not any failure, or reduction can wander from a miscompilation to an unrelated crash. And it must reject invalid candidates: for Vortex, a candidate that no longer compiles is unresolved, never interesting. LLVM's `llvm-reduce` follows the same design for IR: it takes the interestingness test as a script given with `--test`.[^llvmreduce]

??? check "The reducer needed five passes, removing one link of the chain `d`, `e`, `f`, `print f` per pass. In which order could it try the lines so that one pass removes all five lines that go, and why?"

    From the last line to the first. Then `print f` goes before `f = e / 2` is tried, so `f` is no longer read when its turn comes; `print e` goes before `e = d * 3`; and `e` is gone before `d = 4` is tried. Every deletion frees the line above it, so one pass removes all five and a second pass confirms that nothing more can go. Top to bottom, each line is tried while its reader still exists.

## Bisection: which transformation broke it

A reduced program says what fails, not which pass is to blame. With forty passes in a pipeline, trying each one off in turn costs forty builds. **Bisection** costs far fewer. Number every transformation the optimizer makes, in order, and add an option that performs only the first N and skips the rest. If the program is right with N = 0 and wrong with all of them, a binary search on N finds the first transformation after which it is wrong. Each run halves the range, so 1,000 numbered transformations take about ten runs.

LLVM provides this as `-opt-bisect-limit`. Passes that may be skipped check the limit before they transform anything; passes that must run, and analyses, which change nothing, are never skipped. A limit of -1 runs everything and prints the number of each step that could have been skipped.[^optbisect]

Two conditions make bisection trustworthy. The compiler must be deterministic, so that step 517 is the same transformation in every run. And skipping a step must leave a correct program, which is why required passes are exempt. Bisection finds the first transformation after which the output is wrong, which is not always the faulty one: a correct transformation can expose an earlier mistake. The reduced program and the named transformation are where debugging starts.

## Checking each compilation: translation validation

Every oracle so far samples: some inputs, some programs. **Translation validation** checks one compilation of one function against every input. [O1](o1-optimizer-contract.md#hand-it-to-a-solver) and [O11](o11-undefined-behavior.md#checking-a-rewrite-instead-of-trusting-it) introduced Alive2, which takes an LLVM function before and after optimization and asks an SMT solver whether the second refines the first. Run over LLVM's unit tests, it found 47 new bugs. It is bounded, unrolling loops only so far, so it can miss bugs, but it avoids false alarms.[^alive2]

Translation validation and differential testing fit together. A differential test finds a failing program and reduction shrinks it; translation validation then shows, for the reduced function and every input, whether a given pass was at fault. If Vortex emits LLVM IR, Alive2 can check LLVM's passes on Vortex's output. Your own passes, on your own IR, are checked only by the tests you build.

## The loop that ties these together

Generation, two runs, comparison, reduction and bisection form one loop, and its output is a regression test. Figure 4 shows it.

<figure class="vx-figure">
<svg viewBox="0 0 760 360" role="img" aria-label="The testing loop: generate or mutate a program, run it two ways, compare; on agreement draw again, on disagreement reduce, bisect, fix, and keep the reduced program as a golden test" aria-describedby="o12-f4-desc">
<title id="o12-f4-title">The optimizer testing loop</title>
<desc id="o12-f4-desc">Top row, left to right: Generate, a random program or an EMI variant; Run twice, optimizer off and on; Compare, standard output, error line and exit status. From Compare, an arrow labelled agree returns along the top to Generate for the next seed. Another arrow, labelled differ, leads down to a bottom row, right to left: Reduce, delete while still valid and still failing; Bisect, find the first bad transformation; Fix and keep, the reduced program becomes a golden test. A dashed arrow from Fix and keep returns up to the test suite that runs before every merge.</desc>
<defs><marker id="o12-f4-head" viewBox="0 0 10 10" refX="10" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker></defs>
<g class="vx-seq" style="--vx-i: 0; --vx-n: 6">
<rect class="vx-box-strong" x="20" y="60" width="200" height="70" rx="4"/>
<text class="vx-text" x="120" y="88" text-anchor="middle">Generate</text>
<text class="vx-text-muted" x="120" y="110" text-anchor="middle">random program or EMI variant</text>
</g>
<g class="vx-seq" style="--vx-i: 1; --vx-n: 6">
<rect class="vx-box-strong" x="280" y="60" width="200" height="70" rx="4"/>
<text class="vx-text" x="380" y="88" text-anchor="middle">Run twice</text>
<text class="vx-text-muted" x="380" y="110" text-anchor="middle">optimizer off and on</text>
</g>
<g class="vx-seq" style="--vx-i: 2; --vx-n: 6">
<rect class="vx-box-strong" x="540" y="60" width="200" height="70" rx="4"/>
<text class="vx-text" x="640" y="88" text-anchor="middle">Compare</text>
<text class="vx-text-muted" x="640" y="110" text-anchor="middle">output, error line, status</text>
</g>
<path class="vx-flow" d="M220 95 L278 95" marker-end="url(#o12-f4-head)"/>
<path class="vx-flow" d="M480 95 L538 95" marker-end="url(#o12-f4-head)"/>
<path class="vx-line" d="M640 60 L640 34 L120 34 L120 58" marker-end="url(#o12-f4-head)"/>
<text class="vx-text-muted" x="380" y="26" text-anchor="middle">agree: next seed</text>
<path class="vx-line" d="M640 130 L640 208" marker-end="url(#o12-f4-head)"/>
<text class="vx-text-accent" x="650" y="174">differ</text>
<g class="vx-seq" style="--vx-i: 3; --vx-n: 6">
<rect class="vx-box-bad" x="540" y="210" width="200" height="70" rx="4"/>
<text class="vx-text" x="640" y="238" text-anchor="middle">Reduce</text>
<text class="vx-text-muted" x="640" y="260" text-anchor="middle">valid and still failing</text>
</g>
<g class="vx-seq" style="--vx-i: 4; --vx-n: 6">
<rect class="vx-box-bad" x="280" y="210" width="200" height="70" rx="4"/>
<text class="vx-text" x="380" y="238" text-anchor="middle">Bisect</text>
<text class="vx-text-muted" x="380" y="260" text-anchor="middle">first bad transformation</text>
</g>
<g class="vx-seq" style="--vx-i: 5; --vx-n: 6">
<rect class="vx-box-accent" x="20" y="210" width="200" height="70" rx="4"/>
<text class="vx-text" x="120" y="238" text-anchor="middle">Fix and keep</text>
<text class="vx-text-muted" x="120" y="260" text-anchor="middle">reduced case becomes golden</text>
</g>
<path class="vx-line" d="M540 245 L482 245" marker-end="url(#o12-f4-head)"/>
<path class="vx-line" d="M280 245 L222 245" marker-end="url(#o12-f4-head)"/>
<path class="vx-flow" d="M120 280 L120 320 L400 320"/>
<text class="vx-text-muted" x="410" y="324">into the suite every merge must pass</text>
</svg>
<figcaption>Figure 4. The testing loop. Most draws agree and are discarded. A disagreement is reduced to a small valid program, bisected to the first transformation after which it fails, fixed, and kept as a golden test so the same bug cannot return.</figcaption>
</figure>

No single test covers every kind of bug, so the question for each pass is which ones to pair. The earlier chapters each named a risk; the table matches each with the test that catches it.

| Risk, and where it came from | Test that catches it |
| --- | --- |
| Broken SSA after a pass ([O3](o3-ssa.md#for-vortex)) | Verifier after every pass |
| An analysis claims a fact that a run contradicts ([O4](o4-dataflow.md#intervals-bounding-a-loop-index), [O8](o8-loops.md#for-vortex)) | Soundness mode: proven checks put back as internal errors |
| A fold that disagrees with execution ([O5](o5-constants-and-dead-code.md)) | Differential test on generated expressions; a second run of the pass changes nothing |
| Work moved into a loop that runs zero times, or checks reordered ([O6](o6-redundancy.md#what-vortex-allows)) | Generated loops with zero trips and failing checks, compared on the error line |
| An inlining or aliasing decision changes a result ([O7](o7-inlining-and-sroa.md#for-vortex), [O9](o9-alias-analysis.md#for-vortex)) | Differential test with the one feature off and on |
| A wrong remark or a changed pipeline order ([O1](o1-optimizer-contract.md#remarks-the-optimizers-report), [O10](o10-pass-pipelines.md#for-vortex)) | Golden test of the remark stream |
| A rule wrong only in a rare context | Random generation and EMI variants |

## For Vortex

!!! vortex "Exercise"

    **Build** a testing harness that every optimization pass must pass before it merges, starting from O1's contract test.

    1. **A generator** of well-typed Vortex programs from a narrow grammar: `fn main` with fixed-size arrays of the shapes your tests already use, nested `for` loops with constant bounds, integer and `f32` arithmetic on elements, and `print`. It takes a seed and produces the same program for the same seed. Keep values in range so that most programs run to the end, and make a small, fixed share fail one check on purpose in the first, a middle or the last iteration.
    2. **A comparator** that builds each program with your optimizer off and on, runs both with a time limit, and compares standard output, the runtime error line and the exit status byte for byte. A run that times out in only one build is a mismatch. State in the harness how it treats stack exhaustion, following your answer to O1's question.
    3. **A reducer** that deletes statements, and then whole loops, while the candidate still compiles and the same mismatch remains. A candidate that no longer compiles is unresolved, never interesting.
    4. **A transformation counter** in your optimizer: an option that performs only the first N transformations and reports the number of each, and a script that bisects on it.
    5. **A gate**: a fixed, written-down number of seeds that every pass runs against, plus every reduced program found so far as a golden test.

    **Not yet:** equivalence modulo inputs, which needs statement coverage for one input and your compiler does not produce it yet; translation validation of your own IR; generating programs with functions, structs or recursion; running the search on several machines; and any timing comparison between the two builds, which belongs to [P1](p1-measure-first.md).

    **Proof that it works:**

    - Plant a wrong rewrite behind a hidden debugging option, such as `x / 2` to `x >> 1` on `i32`. The harness finds a mismatch within the fixed number of seeds, and the same seed finds it again on a second run.
    - The reducer turns that failure into a program of at most ten lines that still compiles and still shows the same mismatch, and it becomes a golden test.
    - Bisection names the planted rewrite's transformation number.
    - With the planted rewrite off, every pass merged so far runs the whole gate with zero mismatches, and the verifier reports nothing after any pass. From then on, that zero is what every new pass must keep.

## Key ideas

!!! recap "Questions you can now answer"

    - **What is a test oracle, and which three kinds does optimizer testing use?** Whatever decides whether an output is right: a stored answer, a second run, or a property every output must have.
    - **What does a golden test catch, and what can it not check?** It catches any change from a checked output, which makes it the regression test; it cannot tell whether the checked output was right.
    - **Why can a Vortex program generator keep programs that fail a runtime check?** Vortex has no undefined behavior, so the error line and exit status are defined outputs that both builds must match; they are only weak tests if they fail early.
    - **What does equivalence modulo inputs delete, and why does that find compiler bugs?** Code that did not run on one input; the variant must behave the same on that input, but the optimizer sees a different program and may make different decisions.
    - **What must a reducer's interestingness test check besides the failure?** That the candidate is still valid and shows the same failure; an invalid candidate is unresolved, not interesting.
    - **How many runs does bisection need over 1,000 numbered transformations?** About ten, since each run halves the range.

## Where this comes back

!!! next "You will use this again in"

    - [P1. Measure first](p1-measure-first.md): *the correctness gate that runs before any timing counts*
    - [P15. Choosing parameters: models or search](p15-choosing-parameters.md): *golden outputs compared with a parameter on and off*
    - [P16. Capstone: the ladder, measured](p16-capstone.md): *the bits gate, comparing every rung with the reference*
    - [E4. Testing back ends](../backend/e4-testing-backends.md): *differential testing across back ends*, *reduction with llvm-reduce*

## Sources and further reading

For depth, read the Csmith paper's section on how its generator avoids each undefined behavior, the EMI paper's illustrative examples, and the C-Reduce paper's account of the validity problem.

[^langref]: LLVM Project, "LLVM Language Reference Manual", sections "'sdiv' Instruction" (the quotient is rounded towards zero; with `exact`, a result that would be rounded is poison) and "'ashr' Instruction". <https://llvm.org/docs/LangRef.html#sdiv-instruction>
[^opt]: LLVM Project, "opt - LLVM optimizer", option `-verify-each`. <https://llvm.org/docs/CommandGuide/opt.html>
[^testing]: LLVM Project, "LLVM Testing Infrastructure Guide", sections "Regression tests" (a regression test for each bug found, with only enough code to reproduce it), "Testing Analysis" (a printer pass for FileCheck) and "Writing new regression tests" (RUN lines checked with FileCheck). <https://llvm.org/docs/TestingGuide.html>
[^csmith]: Xuejun Yang, Yang Chen, Eric Eide and John Regehr, "Finding and Understanding Bugs in C Compilers", *Proceedings of the 32nd ACM SIGPLAN Conference on Programming Language Design and Implementation (PLDI)*, 2011: abstract, Table 1 (strategies for avoiding undefined behavior), "No guarantee of termination", Table 4 (bugs by compiler stage) and "GCC Bug #1". <https://doi.org/10.1145/1993498.1993532> (free copy: <https://users.cs.utah.edu/~regehr/papers/pldi11-preprint.pdf>; project: <https://github.com/csmith-project/csmith>)
[^yarp20]: Vsevolod Livinskii, Dmitry Babokin and John Regehr, "Random Testing for C and C++ Compilers with YARPGen", *Proceedings of the ACM on Programming Languages* 4(OOPSLA), 2020, abstract; the project README describes value ranges known at generation time. <https://doi.org/10.1145/3428264> (project: <https://github.com/intel/yarpgen>)
[^yarp23]: Vsevolod Livinskii, Dmitry Babokin and John Regehr, "Fuzzing Loop Optimizations in Compilers for C++ and Data-Parallel Languages", *Proceedings of the ACM on Programming Languages* 7(PLDI), 2023, abstract. <https://doi.org/10.1145/3591295>
[^emi14]: Vu Le, Mehrdad Afshari and Zhendong Su, "Compiler Validation via Equivalence Modulo Inputs", *Proceedings of the 35th ACM SIGPLAN Conference on Programming Language Design and Implementation (PLDI)*, 2014: abstract, section 1, section 2 (the Clang inlining example) and section 3 (Orion's implementation). <https://doi.org/10.1145/2594291.2594334> (free copy: <https://web.cs.ucdavis.edu/~su/publications/emi.pdf>)
[^ddmin]: Andreas Zeller and Ralf Hildebrandt, "Simplifying and Isolating Failure-Inducing Input", *IEEE Transactions on Software Engineering* 28(2), 2002: abstract and sections on test outcomes and 1-minimality. <https://doi.org/10.1109/32.988498> (free copy: <https://www.st.cs.uni-saarland.de/papers/tse2002/tse2002.pdf>)
[^creduce12]: John Regehr, Yang Chen, Pascal Cuoq, Eric Eide, Chucky Ellison and Xuejun Yang, "Test-Case Reduction for C Compiler Bugs", *Proceedings of the 33rd ACM SIGPLAN Conference on Programming Language Design and Implementation (PLDI)*, 2012: abstract, section 1, "The Validity Problem" and the section on C-Reduce's transformations. <https://doi.org/10.1145/2254064.2254104> (project: <https://github.com/csmith-project/creduce>)
[^llvmreduce]: LLVM Project, "llvm-reduce - LLVM automatic testcase reducer", option `--test`. <https://llvm.org/docs/CommandGuide/llvm-reduce.html>
[^optbisect]: LLVM Project, "Using -opt-bisect-limit to debug optimization errors", sections "Introduction" and "Getting Started". <https://llvm.org/docs/OptBisect.html>
[^alive2]: Nuno P. Lopes, Juneyoung Lee, Chung-Kil Hur, Zhengyang Liu and John Regehr, "Alive2: Bounded Translation Validation for LLVM", *Proceedings of the 42nd ACM SIGPLAN International Conference on Programming Language Design and Implementation (PLDI)*, 2021, abstract and section 1. <https://doi.org/10.1145/3453483.3454030> (free copy: <https://users.cs.utah.edu/~regehr/alive2-pldi21.pdf>)

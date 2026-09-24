# O12. Testing an optimizer

<p class="page-intro">An optimizer is code, and code has bugs. This chapter shows how to find them without a proof: by running a program two ways and comparing, by generating programs a person would never think to write, and by shrinking a five-hundred-line failure down to the two lines that matter.</p>

<p class="vx-meta" markdown="1">Level: Advanced · Reading time: about 20 minutes · Builds on: [O1. The optimizer's contract](o1-optimizer-contract.md), [11. Release](../compiler/guide/stage-11-release.md)</p>

???+ remember "Before you start, remember"

    ??? question "What must a Vortex optimization preserve, on every input?"

        Standard output, the runtime error line and its message, and the exit status, with every floating-point operation rounded exactly as written. Nothing else is observable.

        Introduced in [O1. The optimizer's contract](o1-optimizer-contract.md#what-an-optimizer-must-keep).

    ??? question "What is a differential test, and what can it never do?"

        Compiling the same program two ways and comparing what the two executables do. It is the cheapest check to build and it finds real bugs, but it only ever produces counterexamples: it never shows that none exist.

        Introduced in [O1. The optimizer's contract](o1-optimizer-contract.md#testing-finds-bugs-proofs-rule-them-out).

    ??? question "What is a regression?"

        Something that used to work and has stopped working because of a later change. Running old tests after every change is how regressions are caught.

        Introduced in [11. Release](../compiler/guide/stage-11-release.md#words-for-this-stage).

    ??? question "How many kinds of test does the v0.1 suite run at release, and what does an end-to-end test check that the others do not?"

        Six kinds, from lexer tests to end-to-end tests. Only an end-to-end test runs the whole compiler on a source file, runs the resulting executable, and checks what comes out.

        Introduced in [11. Release](../compiler/guide/stage-11-release.md#the-test-suite-as-a-whole).

!!! goals "In this chapter"

    - Explain why comparing two runs of the same program finds bugs that neither a proof nor a single test would catch.
    - Distinguish golden tests, differential tests, random program generation and equivalence modulo inputs, and say what each can and cannot show.
    - Explain why Vortex's runtime-error contract lets a program generator skip work that a C fuzzer like Csmith cannot.
    - Build a test-case reducer that shrinks a failing program down to the lines that matter, automatically.

## A rewrite that is right most of the time

Here is a rewrite an optimizer might make: replace `x / 2` with `x >> 1`, a right shift. Division is one of the more expensive integer operations on most processors; a shift is nearly free. For `x = 10`, both give 5. For `x = -3`, they do not: C++ division truncates toward zero, so `-3 / 2` is `-1`, while an arithmetic right shift rounds toward negative infinity, so `-3 >> 1` is `-2`. The rewrite is correct exactly when `x` is never negative, and wrong otherwise.

A bug like this does not announce itself. It sits in a constant-folding or strength-reduction routine, waits for a negative operand next to a literal `2`, and quietly changes an answer. No single hand-written test exercises every combination of operator, operand and sign that a real program might contain, and no amount of staring at the rewrite rule in isolation proves it wrong: the rule looks right until you remember what happens below zero.

The way to find this kind of bug without first suspecting it is to stop reasoning about the rewrite and start comparing its output against something you trust. Run the same computation two ways, and check whether they agree.

## Comparing two runs: differential testing

[O1](o1-optimizer-contract.md#testing-finds-bugs-proofs-rule-them-out) already named this idea: compiling the same program two ways and comparing what the two executables do is a **differential test**. "Two ways" can mean two optimization levels of the same compiler, two different compilers, or, as in the example below, two implementations of the same evaluation rule standing in for "unoptimized" and "optimized". The comparator does not need to know anything about *why* the two might disagree; it only needs to know what counts as agreement, which for Vortex is exactly the three things the reminder above lists.

The example below generates small integer-expression trees from a fixed seed, so the same 500 programs are built on every run, and evaluates each one two ways: once with a plain recursive evaluator, once with an evaluator that applies the `x / 2` to `x >> 1` rewrite. It stops at the first disagreement.

--8<-- "includes/examples/optimize/o12-testing-optimizers/differential_test.cpp.md"

Program 5 is the first one whose tree happens to divide a negative value by the literal 2. Nothing about the harness knew to look for that combination; it only knew to keep generating programs and comparing. That is the whole method: no proof, no advance understanding of the bug, only two implementations and a comparator that never gets bored.

Differential testing is also why the naive and the fast versions of a matmul kernel are worth compiling and running against each other on the same inputs, rather than compared by eye. A mis-tiled or mis-vectorized kernel still produces numbers that look like a matrix product: plausible magnitudes, roughly the right shape of output. Only a bitwise comparison against a build with every optimization pass turned off catches the one entry that is wrong. Vortex's fixed-shape arrays make this cheap to set up: the shapes are known at compile time, so generating well-typed inputs for a kernel needs no dynamic allocation or shape inference, only a loop over the array's declared extent.

??? check "Why does a differential test never prove an optimizer correct?"

    Because it only compares the programs it was given. Five hundred programs that agree rule out those five hundred as counterexamples; they say nothing about the five hundred and first. A differential test can only find a wrong rewrite, never confirm that no wrong rewrite remains ([O1](o1-optimizer-contract.md#testing-finds-bugs-proofs-rule-them-out)).

## Golden tests: pin one answer down forever

A differential test needs two implementations to compare. Sometimes there is only one, and the question is simpler: does it still produce the output it produced last time? A **golden test** runs a program once, saves its exact output, checks that saved copy into the repository, and from then on compares every future run against it byte for byte. [O1](o1-optimizer-contract.md#your-turn-the-stage-10-kernel-under-the-contract)'s own exercise is one: the optimization-remark stream for the stage 10 kernel is expected to match a checked-in copy exactly, span for span.

A golden test cannot tell you the saved output was correct; only a person checking it by hand at the moment it is first saved can do that. What it buys afterward is cheap: no second implementation to build or keep in sync, only a stored answer and a comparison. It is also exactly what catches a **regression**, something that used to work and stopped working because of a later change, which is why the v0.1 release gate runs the whole suite of golden and end-to-end tests together rather than trusting that old passes still pass ([11. Release](../compiler/guide/stage-11-release.md#the-test-suite-as-a-whole)).

The two techniques answer different questions. A golden test asks "does this still match what I decided was right, once, by hand?" A differential test asks "do these two things, built to mean the same, still agree?" A test suite for an optimizer needs both: golden tests for output whose exact shape matters (remark streams, generated assembly for a fixed loop), differential tests for the much larger space of programs no one had time to pin down one by one.

## Random programs: testing what nobody thought to write

A hand-written test suite only contains the bugs its author imagined. The strength-reduction bug above is the kind that survives review because it needs an unusual combination, a negative value meeting a specific literal, to show up. The fix is to stop writing test programs by hand and generate them.

Csmith, from 2011, generates random C programs and compiles each with several production compilers, comparing their output. Its hardest engineering problem is not generation, it is **avoiding undefined behavior**: a C program that reads an uninitialized variable, overflows a signed integer or dereferences past the end of an array has no defined output at all, so a mismatch on such a program proves nothing about the compilers and only wastes the person reading the bug report. Csmith's grammar and a runtime bounds-and-initialization checker exist to rule those programs out before comparing. Even with that filtering, testing production-quality C compilers over three years turned up more than 325 previously unknown bugs, many in code paths that had shipped for years.[^csmith]

A Vortex program generator does not need Csmith's hardest piece of machinery, because Vortex has no undefined behavior to avoid. Every operation is either well-defined or a documented, checked runtime error that stops the program and prints a specific line ([O1](o1-optimizer-contract.md#what-an-optimizer-must-keep)). A generated program that divides by zero or reads past an array's declared extent does not need to be thrown away: its runtime error line and exit status are exactly as comparable as any other output, because the contract makes them part of what an optimizer must preserve. The generator still has real work, staying inside the grammar and the fixed shapes the type checker requires, but it is spared the open-ended UB-avoidance logic that makes Csmith's own generator large.

A companion project, YARP (short for "yet another random program"), takes the same idea and narrows it: rather than generating arbitrary C or C++, it targets constructs that specifically stress loop optimizations, the transformations [O7](o7-inlining-and-sroa.md) through [O9](o9-alias-analysis.md) cover.[^yarp20] A later extension generates loop nests aimed even more directly at the vectorizer, the dependence analyses and the loop transformations of [P6](p6-dependence-analysis.md) and [P7](p7-loop-transformations.md).[^yarp23] For a compiler whose flagship kernel is a triple-nested loop, that narrower target matters more than breadth: a generator for Vortex is worth biasing the same way, toward nested loops over fixed-shape arrays, rather than toward the full grammar at once.

??? check "Why does Csmith spend so much of its complexity avoiding undefined behavior, and why can a Vortex generator skip that?"

    Because a mismatch on a C program with undefined behavior proves nothing: the language allows any output at all, so disagreement between two compilers is not evidence that either is wrong. Vortex has no such gap. Every operation is well-defined or a documented runtime error, so any well-typed, well-shaped generated program is a fair test, and a generator can spend its effort on the grammar and the shapes instead of on ruling out UB.

## Equivalence modulo inputs: mutating instead of generating

Generating a program from nothing needs a grammar for the whole language. A different way to get a fresh test case is to start from a program that already exists and change it slightly, for one specific input.

**Equivalence modulo inputs (EMI)** picks one input, runs a profiler to find code the program never reaches for that input, deletes some of that unreached code, and requires the result to agree with the original on that same input; a program and a version of it with dead code removed for a given input must behave identically on that input.[^emi14] The comparator is the same idea as a differential test, but the two programs being compared are not two independent implementations. They are the same program before and after a mutation that a correct analysis would consider meaning-preserving, for one input.

The example below plays out what happens when that analysis is wrong. Its "profiler" decides whether a guarded statement is reachable by comparing the sign of the input against the sign of a threshold, ignoring magnitude, which is exactly the kind of shortcut a real reachability analysis might take under time pressure. Ten inputs, no randomness needed, are enough to catch it.

--8<-- "includes/examples/optimize/o12-testing-optimizers/emi_prune.cpp.md"

The first input already disagrees. Ten fixed inputs, checked by hand once and printed in the source, are all this technique needs; nothing here depends on a lucky random draw, because the flaw is in the reachability logic itself, not in some rare combination of values. Applied to production compilers instead of a toy, this method produced 147 confirmed unique bug reports against GCC and LLVM in eleven months.[^emi14]

EMI complements random generation rather than replacing it. It needs no grammar for the whole language, since most of a mutant is the unchanged original program; its mutants read like ordinary code, because they mostly are ordinary code with a few lines missing; and it directly exercises the same reachability reasoning that dead-code elimination ([O5](o5-constants-and-dead-code.md)) and bounds-check removal ([O8](o8-loops.md)) depend on. A bounds-check-removal pass that wrongly decides a check can never fail is exactly an EMI-style bug: it prunes something that was, for at least one input, still reachable.

??? check "How does equivalence modulo inputs differ from generating a random program from scratch?"

    It starts from an existing program and one fixed input instead of building a new program from a grammar. It only removes code that a profile says the chosen input cannot reach, so its mutants stay close to real, already-compiling code, and it directly tests the same reachability analysis that dead-code and bounds-check elimination rely on.

## Test-case reduction: from a page to a sentence

The bug the first example found needed a program with six nodes to trigger. A real fuzzer run for hours can produce a failing program thousands of lines long, most of it irrelevant to the bug. Handing a compiler maintainer that program, or debugging it yourself, spends more time than the fuzzer saved by finding it.

**Test-case reduction** takes a failing program and repeatedly deletes parts of it, keeping each deletion only if the result is still "interesting", meaning it still exhibits the property being tracked, usually the same mismatch or crash that made the program failing in the first place. Once no single further deletion keeps the property, the result is a **minimized** test case. CReduce, built for C and C++, generalizes this beyond deleting whole lines: it applies a series of language-aware passes that remove statements, then simplify expressions, then shrink identifiers and literals, interleaving these passes until none of them can make further progress.[^creduce12]

The example below reduces a synthetic eight-line failing program down to the two lines that matter, using nothing more than "try deleting this line; keep the deletion if the program is still interesting":

--8<-- "includes/examples/optimize/o12-testing-optimizers/reduce.cpp.md"

Six lines disappear, one pass, because none of them affects whether the property holds. What survives is `x = -3` followed by `divide_by_two(x)`, precisely the two facts the strength-reduction bug in this chapter's first example needs: a negative value, and a division by the literal 2. A bug report built from this reduced case says everything the original 500-program run said, in two lines instead of one.

The "interesting" predicate has to be something a program can check, not only something a person can see. Reduction calls it once per candidate deletion, which for even a modest program means many thousands of calls; a person re-judging each candidate by hand could not keep pace, and would not judge two candidates the same way twice. For a differential test, that predicate is "the two evaluations still disagree." For a compiler, it might be "the compiler still crashes" or "the compiled program still exits with the wrong status." Whatever it is, it must be automatic.

??? check "Why must the 'interesting' property in test-case reduction be checked by a script, not by a person?"

    Because reduction evaluates it once for every candidate deletion while shrinking a case, which is thousands of calls even for a modest program. Only an automatable predicate can run at that rate and give the same answer every time; a human judgment would have to be repeated, consistently, thousands of times.

## The loop that ties these together

Random generation (or a profile-guided mutation), two runs, a comparison and, on failure, a reduction: these four pieces form one loop, not four separate techniques to pick between. Figure 1 lays it out.

<figure class="vx-figure">
<svg viewBox="0 0 760 380" role="img" aria-label="The differential-testing loop: generate, run twice, compare, then discard or reduce" aria-describedby="o12-f1-desc">
<title id="o12-f1-title">The differential-testing loop</title>
<desc id="o12-f1-desc">Three boxes in a row: Generate, Run twice, Compare. An arrow leads from Compare down and left to a box labelled match, discard, try the next draw, and from there a line loops back up and around to Generate. Another arrow leads from Compare down and right to a box labelled mismatch, reduce, delete lines while it keeps failing, which itself points down to a final box, minimal failing case, small enough to read and to fix.</desc>
<text class="vx-text" x="20" y="26">One test, four steps</text>
<g class="vx-seq" style="--vx-i: 0; --vx-n: 6">
<rect class="vx-box-strong" x="20" y="46" width="180" height="64" rx="4"/>
<text class="vx-text" x="110" y="72" text-anchor="middle">Generate</text>
<text class="vx-text-muted" x="110" y="92" text-anchor="middle">random draw, or a</text>
<text class="vx-text-muted" x="110" y="106" text-anchor="middle">profile-guided prune</text>
</g>
<g class="vx-seq" style="--vx-i: 1; --vx-n: 6">
<rect class="vx-box-strong" x="290" y="46" width="180" height="64" rx="4"/>
<text class="vx-text" x="380" y="72" text-anchor="middle">Run twice</text>
<text class="vx-text-muted" x="380" y="92" text-anchor="middle">unoptimized vs optimized,</text>
<text class="vx-text-muted" x="380" y="106" text-anchor="middle">or original vs mutant</text>
</g>
<g class="vx-seq" style="--vx-i: 2; --vx-n: 6">
<rect class="vx-box-strong" x="560" y="46" width="180" height="64" rx="4"/>
<text class="vx-text" x="650" y="72" text-anchor="middle">Compare</text>
<text class="vx-text-muted" x="650" y="92" text-anchor="middle">stdout, error line,</text>
<text class="vx-text-muted" x="650" y="106" text-anchor="middle">exit status</text>
</g>
<line class="vx-flow" x1="200" y1="78" x2="282" y2="78"/>
<polygon class="vx-arrowhead" points="282,73 290,78 282,83"/>
<line class="vx-flow" x1="470" y1="78" x2="552" y2="78"/>
<polygon class="vx-arrowhead" points="552,73 560,78 552,83"/>
<line class="vx-line" x1="620" y1="110" x2="480" y2="190"/>
<polygon class="vx-arrowhead" points="483,182 475,192 490,195"/>
<line class="vx-line" x1="680" y1="110" x2="700" y2="190"/>
<polygon class="vx-arrowhead" points="694,183 704,190 697,198"/>
<g class="vx-seq" style="--vx-i: 3; --vx-n: 6">
<rect class="vx-box" x="310" y="196" width="190" height="64" rx="4"/>
<text class="vx-text" x="405" y="220" text-anchor="middle">match: discard</text>
<text class="vx-text-muted" x="405" y="240" text-anchor="middle">try the next draw</text>
</g>
<g class="vx-seq" style="--vx-i: 4; --vx-n: 6">
<rect class="vx-box-bad" x="600" y="196" width="190" height="64" rx="4"/>
<text class="vx-text" x="695" y="220" text-anchor="middle">mismatch: reduce</text>
<text class="vx-text-muted" x="695" y="240" text-anchor="middle">delete while it keeps failing</text>
</g>
<line class="vx-line" x1="695" y1="260" x2="695" y2="300"/>
<polygon class="vx-arrowhead" points="690,300 695,308 700,300"/>
<g class="vx-seq" style="--vx-i: 5; --vx-n: 6">
<rect class="vx-box-accent" x="600" y="312" width="190" height="60" rx="4"/>
<text class="vx-text" x="695" y="336" text-anchor="middle">Minimal failing case</text>
<text class="vx-text-muted" x="695" y="354" text-anchor="middle">small enough to read and fix</text>
</g>
<path class="vx-line" d="M 310 228 L 60 228 L 60 110" fill="none"/>
<polygon class="vx-arrowhead" points="55,118 60,108 65,118"/>
</svg>
<figcaption>Figure 1. The differential-testing loop. A generated or mutated program is run two ways and compared on the contract's three observables. A match is discarded and the loop draws again; a mismatch is reduced, deleting parts of the program while the disagreement survives, down to a minimal failing case worth reading.</figcaption>
</figure>

## For Vortex

!!! vortex "Exercise"

    **Build** a differential-testing harness for programs shaped like the stage 10 kernel: fixed-size arrays, `&mut` outputs, loops with constant bounds.

    1. **A generator** that emits well-typed, well-shaped Vortex programs from a narrow grammar: a handful of fixed array shapes (start with the ones your own test suite already declares), nested `for` loops over constant ranges, and arithmetic on their elements. Because every Vortex operation is either well-defined or a documented runtime error, a generated program never needs to dodge undefined behavior the way Csmith does; a program that trips a bounds check is an equally usable test case, since the error line and exit status are part of what the comparator checks.
    2. **A comparator** that builds each generated program with your optimizer's passes off and with them on, as [O1](o1-optimizer-contract.md#your-turn-the-stage-10-kernel-under-the-contract)'s contract test already does for hand-written programs, runs both, and compares standard output, the runtime error line and the exit status byte for byte.
    3. **A reducer** that, on a mismatch, deletes statements, then loop iterations, then array elements one at a time, keeping only deletions that leave the mismatch in place, until nothing more can be removed.
    4. **A gate**: every optimization pass you add from here on must run against a fixed, written-down number of generated programs, and merges only when all of them agree with the unoptimized build.

    **Not yet:** a grammar that reaches every corner of the language, start with the shapes the stage 10 kernel already uses; equivalence modulo inputs, it needs a working profiler first, which is downstream of the analyses in [O4](o4-dataflow.md) and [O8](o8-loops.md); running the search across several machines at once; anything that reports a *speed* difference between the two builds, that measurement belongs to [P1](p1-measure-first.md), not to this gate.

    **Proof that it works:**

    - Plant a wrong rewrite behind a hidden flag, for example this chapter's `x / 2` to `x >> 1` substitution, ported to Vortex's checked division, and confirm the harness finds a mismatch within the fixed, written-down program count from step 4.
    - Confirm the reducer turns that failure into a program small enough to read in full at once, and that the reduced program still produces the same mismatch as the original.
    - Run the harness against every optimization pass merged so far and confirm it reports zero mismatches. From this point on, that zero is the gate the next pass has to pass too.

## Key ideas

!!! recap "Questions you can now answer"

    - **What is a differential test?** Running the same program two ways and comparing what happens; for Vortex, comparing standard output, the runtime error line and the exit status.
    - **What can a differential test never do?** Prove an optimizer correct. It only ever finds counterexamples among the programs it was given.
    - **What does a golden test check that a differential test cannot, and what can it not check?** It pins one output down exactly, with no second implementation needed; it cannot tell you the pinned output was right, only that it has not changed.
    - **Why does Csmith need machinery to avoid undefined behavior, and why does a Vortex generator not?** A mismatch on a C program with undefined behavior proves nothing, since the language allows any result. Every Vortex operation is well-defined or a documented runtime error, so any well-typed, well-shaped program is a fair test.
    - **What does equivalence modulo inputs mutate, and what must the mutant still do?** It deletes code a profile says one specific input cannot reach; the mutant must still agree with the original on that same input.
    - **What must the "interesting" predicate in test-case reduction be able to do?** Run automatically, many thousands of times, and give the same answer every time; reduction calls it once per candidate deletion.

## Where this comes back

!!! next "You will use this again in"

    - [O10. Pass managers and pipelines](o10-pass-pipelines.md): *bisecting which pass in a pipeline introduced a mismatch*
    - [P1. Measure first](p1-measure-first.md): *the correctness gate that must pass before any timing counts*
    - [P16. Capstone: the ladder, measured](p16-capstone.md): *the same gate, still required at the top of the ladder*
    - [E4. Testing back ends](../backend/e4-testing-backends.md): *the same differential-testing method, one layer closer to the machine*

## Sources and further reading

For depth, read the Csmith paper's account of how its grammar stays expressive while avoiding undefined behavior, the EMI paper for how a profile turns into a set of safe deletions, and the CReduce paper for the pass structure a general-purpose reducer needs beyond deleting one line at a time.

[^csmith]: Xuejun Yang, Yang Chen, Eric Eide and John Regehr, "Finding and Understanding Bugs in C Compilers", *Proceedings of the 32nd ACM SIGPLAN Conference on Programming Language Design and Implementation (PLDI)*, 2011, abstract. <https://doi.org/10.1145/1993498.1993532> (free copy: <https://users.cs.utah.edu/~regehr/papers/pldi11-preprint.pdf>; project: <https://github.com/csmith-project/csmith>)
[^emi14]: Vu Le, Mehrdad Afshari and Zhendong Su, "Compiler Validation via Equivalence Modulo Inputs", *Proceedings of the 35th ACM SIGPLAN Conference on Programming Language Design and Implementation (PLDI)*, 2014, abstract. <https://doi.org/10.1145/2594291.2594334> (free copy: <https://web.cs.ucdavis.edu/~su/publications/emi.pdf>)
[^yarp20]: Vsevolod Livinskii, Dmitry Babokin and John Regehr, "Random Testing for C and C++ Compilers with YARPGen", *Proceedings of the ACM on Programming Languages* 4(OOPSLA), 2020, abstract. <https://doi.org/10.1145/3428264> (project: <https://github.com/intel/yarpgen>)
[^yarp23]: Vsevolod Livinskii, Dmitry Babokin and John Regehr, "Fuzzing Loop Optimizations in Compilers for C++ and Data-Parallel Languages", *Proceedings of the 44th ACM SIGPLAN Conference on Programming Language Design and Implementation (PLDI)*, 2023, abstract. <https://doi.org/10.1145/3591295>
[^creduce12]: John Regehr, Yang Chen, Pascal Cuoq, Eric Eide, Chucky Ellison and Xuejun Yang, "Test-Case Reduction for C Compiler Bugs", *Proceedings of the 33rd ACM SIGPLAN Conference on Programming Language Design and Implementation (PLDI)*, 2012, abstract. <https://doi.org/10.1145/2254064.2254104> (project: <https://github.com/csmith-project/creduce>)

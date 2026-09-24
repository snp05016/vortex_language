# O11. Undefined behavior, poison and correct optimization

<p class="page-intro">An optimizer can only justify a rewrite by first fixing what "the program's behavior" is allowed to mean. This chapter builds LLVM's answer: two tiers of undefined behavior, the poison value that carries a failure without reporting it, the freeze instruction that stops it spreading, and the tool that checks a rewrite against all of this by asking a solver instead of a human. For Vortex, where the language calls the same events runtime errors instead of undefined behavior, it ends at the rule a compiler must never break: a flag that is not proved is a promise to delete the check that was supposed to guard it.</p>

<p class="vx-meta" markdown="1">Level: Advanced · Reading time: about 40 minutes · Builds on: [O1. The optimizer's contract](o1-optimizer-contract.md), [Build v0.1, stage 9](../compiler/guide/stage-9-runtime-safety.md)</p>

???+ remember "Before you start, remember"

    ??? question "How does a front end tell LLVM what it may assume?"

        With flags and attributes such as `nsw`, the fast-math flags and `noalias`, each a promise the language must back.

        Introduced in [O1. The optimizer's contract](o1-optimizer-contract.md#the-contract-travels-in-the-ir).

    ??? question "What must a Vortex optimization preserve, on every input?"

        What the program prints, its runtime error line and its exit status, with every floating-point operation rounded as written.

        Introduced in [O1. The optimizer's contract](o1-optimizer-contract.md#refinement-a-rule-for-every-input).

    ??? question "When is an array index checked at compile time instead of at run time?"

        Only when every operand that decides the check is an integer constant expression. Otherwise the generated code must keep the check, even where the compiler could in principle work out that it always passes.

        Introduced in [Build v0.1, stage 9](../compiler/guide/stage-9-runtime-safety.md#one-rule-two-moments).

    ??? question "What did LLVM's InstCombine do with `add nsw i32 %x, 1` compared against `%x`?"

        With the flag, it deleted the comparison and replaced it with the constant `true`. Without the flag, the comparison stayed as the exact test for overflow.

        Introduced in [O8. Loops: structure, induction variables and bounds checks](o8-loops.md#no-wrap-flags).

!!! goals "In this chapter"

    - Explain why LLVM IR keeps two tiers of undefined behavior, immediate and deferred, instead of one.
    - Recognize what a poison value stands for, which instructions produce one, and which absorb one without spreading it further.
    - Trace what `freeze` does to a poison or undef value, and explain why an optimizer needs it at all.
    - Read what a translation-validation tool checks and why it settles a question that testing a rewrite on a handful of inputs cannot.
    - Judge, for a Vortex checked operation, whether a no-wrap or `inbounds` flag has the proof it needs, and what happens to the check if it does not.

## A check that used to be there

[O8](o8-loops.md#no-wrap-flags) ran one instruction through LLVM 18.1.8's InstCombine twice, changed by one word. The instruction computed a sum and then tested whether it overflowed, in the style a checked language uses to catch the error before it does anything with the bad result: add one, then ask whether the result came out smaller than what you started with. Written without a flag, the test survived exactly as written: the exact condition for a signed overflow. Written with the flag `nsw` on the `add`, the same test collapsed to the constant `true`, on the same machine and the same date. One word turned a test that runs on every input into one whose answer the compiler had already decided, and the two programs are not the same program: on an input that does not overflow, `true` is wrong.

Nothing about the arithmetic changed between the two runs. What changed is what the compiler was told it could assume, and it is worth being precise about what a flag actually says before going further, because the rest of the chapter is about exactly this: which promises an optimizer may believe, what happens the moment one turns out false, and how to know, before shipping a compiler, whether a given promise has anything behind it.

## Two tiers of undefined behavior

**Undefined behavior** is a promise from the language to the compiler, in the other direction from a runtime check: instead of the language promising the compiler that a value is checked, the language releases the compiler from any obligation about what happens for a class of inputs at all. C's classic example is signed integer overflow: `INT_MAX + 1` has no defined result, and a conforming compiler may assume no program ever computes it. That freedom is not a curiosity. It is what licenses an entire family of textbook optimizations, from keeping a loop counter in a wider register than the source type to proving that a pointer arithmetic never wraps around the address space.

LLVM IR needs the same freedom, because it is compiled from languages that have it, and because the operations of a machine are themselves often only partially defined (dividing by zero is not a number on any CPU this book's readers will target). But LLVM's documentation on the subject draws a line C does not: some instructions carry **immediate undefined behavior**, where the operation itself is already meaningless the moment it runs, and some instructions carry **deferred undefined behavior**, where the operation produces an ordinary-looking value that only becomes a problem if something later depends on it in a specific way.[^llvm-ub] Dividing an integer by a runtime-computed zero is immediate: there is no defined value to hand back, so the Language Reference simply says the result is undefined.[^langref] An `add` marked `nsw` that overflows is deferred: the instruction still returns something, a special **poison** value, and nothing has gone wrong yet. Something only goes wrong if a later instruction uses that poison value in one of a specific list of ways.

The distinction sounds like a technicality, and for years it effectively was one, applied inconsistently enough that LLVM's own optimizations were not always sound in the presence of the deferred kind. Lee et al., in the paper that gave poison and `freeze` much of their current shape, describe having found real, longstanding miscompilations in transformations as ordinary as loop unswitching and global value numbering, caused by exactly this gap between what the IR's informal rules said and what the optimizations built on top of it actually needed to be true.[^lee17] Their fix was not to remove deferred undefined behavior. It was to define it precisely enough that a machine, not only a person reading the source of a pass, could check whether a rewrite respects it. The rest of this chapter is that precise definition, and the tool built once it existed.

??? check "A function computes `%n = udiv i32 %x, %y` where `%y` is a parameter that could be zero at run time. Is this immediate or deferred undefined behavior, and what follows from the answer?"

    Immediate. Integer division by zero has no result to defer: the instruction itself is where the problem lives, not something that reads its result later. A pass may not, on the strength of this instruction alone, assume `%y` is nonzero everywhere in the function and then leave the division as dead code if the result is unused; if `%y` can be zero on some path that reaches the division, the division is a bug wherever it was inserted, and this is the reason LLVM IR generated from a checked language must test the divisor first and branch around the division, the way [stage 9](../compiler/guide/stage-9-runtime-safety.md#the-checks-vortex-requires) requires Vortex to.

## Poison: a value that remembers nothing went wrong

A **poison value** stands for the result of a computation that broke one of its operation's side conditions, without the program stopping or reporting anything. The Language Reference's list of side conditions is the list of no-wrap and similar flags: `nsw` and `nuw` on the arithmetic instructions, the `exact` flag on `udiv`, `sdiv`, `lshr` and `ashr` (which says the division or shift has no remainder), and `inbounds` on `getelementptr`, among others.[^langref] Each flag is a claim about every execution of that instruction, and poison is what the instruction returns on the executions where the claim turns out false. Reading a poison value back is not itself wrong. Nothing is printed, nothing traps, and the bits are not even fixed: LLVM is free to implement poison however is convenient at each point, including as if it were a different concrete value each time it is inspected.

What makes poison useful, rather than merely permissive, is how it moves through the rest of the program. The general rule is **propagation**: an instruction whose operand is poison usually produces poison itself, so a value that started poisoned stays poisoned all the way to wherever it is finally consumed.[^langref] That is what let InstCombine, in the O8 example, treat the whole comparison as poison-or-true and then simplify poison-or-true to `true`: an optimizer is allowed to resolve a poison value to whichever concrete value makes the surrounding code simplest, because every concrete value is an equally legal reading of "poison."

Propagation is the default, not a law. Some instructions can consume a poison operand and still produce a value with no poison in it at all, because the operation has an **absorbing** case that does not depend on that operand. `and i1 %anything, false` is always `false`, whether or not `%anything` is poison, because `false` alone already pins down the answer. `or i1 %anything, true` is always `true` for the same reason in the other direction. A `select` behaves the same way at the level of a whole value: if the condition picks the branch that is not poisoned, the poisoned branch's value never escapes, even though computing it may have executed the poisoning instruction along the way. These are not special cases carved out for convenience; they follow from what poison means. Poison denotes "any value would be a correct reading here," and `and`ing "any value" with `false` still has only one correct reading, `false`, so there is nothing left for the poison to affect.

The first example puts three of these together: two absorbing cases and one place where a poisoned value survives inside a `select` and disappears anyway when the surrounding logic discards it, run through LLVM 18.1.8's instruction combiner.

--8<-- "includes/examples/optimize/o11-undefined-behavior/poison_absorb.ll.md"

`@and_false` and `@or_true` fold to a constant with the overflowing add removed entirely: the absorbing operand decided the answer before poison had anywhere to go. `@select_unused` is more interesting, because the poison-producing addition is not removed; InstCombine still has to compute a value for the branch the `select` might take. What disappears is not the computation but the flag: the transformed function no longer carries `nsw` on the surviving arithmetic, because once the two additions are folded together algebraically, the result is defined for every input of `%x`, no overflow claim required. The result the caller sees depends only on `%c`, never on whether the discarded branch would have overflowed.

??? check "Is `and i1 %a, %a` also always safe when `%a` might be poison?"

    No, and the difference from `and i1 %a, false` is the whole point of absorption. `%a` alone does not pin the answer down the way a literal `false` does: if `%a` is poison, both operands of the `and` are the same poison value, and the result is poison too, by the ordinary propagation rule. Absorption needs a second operand whose own value already determines the result regardless of the first; a repeated poisoned operand gives the instruction nothing to absorb into.

## When poison becomes a real problem

Propagation and absorption describe what happens to poison as long as it stays a value flowing between instructions. That is deliberately a weak statement: it says a poisoned value can sit in a register, get compared, get selected between, and still nothing has technically gone wrong, because reading poison is not itself observable. The Language Reference is specific about where the line actually is. Poison stops being harmless the moment it reaches one of a fixed list of uses where the instruction's own meaning depends on having an actual, single value to act on rather than "any value would do": branching or switching on a poisoned condition, and calling through a poisoned function pointer, are the clearest members of the list, because both of those require the machine to commit to one concrete choice, and every choice is simultaneously "correct" for a poison operand, which is exactly what makes committing to any of them unconstrained.[^langref] At that point the undefined behavior is no longer deferred. It has arrived, in the ordinary C sense: the rest of the execution is unconstrained.

This two-step design, quiet propagation followed by a sharp trigger, is what gives an optimizer room to work. A pass that moves a poison-producing instruction earlier, later, or into a branch that used not to reach it has not, by itself, broken anything, because poison flowing through ordinary data instructions was never a promise about behavior. The pass only has to be careful at the short list of places where poison turns into undefined behavior for real, and those are exactly the places a correctness checker needs to examine.

## `freeze`: fixing a value once

Propagation has one consequence that is easy to miss and that motivated a dedicated instruction. If a poisoned value is used more than once, each use is separately free to resolve the poison however is most convenient for the code around it, because "poison" does not mean "one unknown value used consistently everywhere"; it means "any value, decided per use." For a value that a single instruction reads once, that freedom is harmless. For a value that a transformation duplicates, for instance when unswitching a loop copies a branch condition into two separate places in the program, the freedom stops being harmless: the two copies can now resolve to different concrete values, producing a combination of outcomes that no single execution of the original, undivided program could ever have produced. Lee et al. give this exact failure mode, arising from duplicated conditions in loop unswitching, as one of the concrete miscompilations that motivated defining `freeze` precisely.[^lee17]

The **`freeze`** instruction is the fix: `freeze T %v` returns some single, arbitrary value of type `T`, chosen once, for every input where `%v` is poison or undef, and returns `%v` unchanged for every input where `%v` was already a normal value.[^langref] "Arbitrary" here does not mean "different every time." It means the compiler may pick any fixed choice it likes when generating code, but from that point on the frozen value behaves like any other SSA value: every use of the same `freeze` instruction reads the same thing. Duplicating a frozen value is safe in a way that duplicating a raw poison value is not, because there is now exactly one value to duplicate.

The second example shows `freeze` on both a genuinely poisoned operand and on an ordinary one, through the same instruction combiner as the first example.

--8<-- "includes/examples/optimize/o11-undefined-behavior/freeze.ll.md"

`@raw_poison` and `@raw_undef` fold straight to the constant `0`: InstCombine is free to pick any concrete value for a frozen poison or undef, and it picks the simplest one it can. `@freeze_of_a_parameter` does not fold at all, because `%x` is an ordinary function argument with no poison anywhere near it; freezing an already-defined value changes nothing, so the instruction survives untouched, exactly as intended. `@freeze_of_an_overflow` is the one worth reading twice. The source freezes the *result* of a possibly-overflowing `add nsw`, and the optimized IR does not keep the freeze there at all. It moves the freeze down onto `%x`, the operand, and drops `nsw` from the `add`. That single rewrite is the mechanism in miniature: instead of computing a value that might be poison and then pinning it down afterward, LLVM pins down the input first and performs an ordinary, always-defined addition on the fixed input. The no-wrap claim is gone, because it is no longer needed or true in general; what is left is a computation that is well-defined for every possible frozen choice of `%x`, which is exactly what the original, unfrozen program was not.

??? check "After InstCombine, does `@freeze_of_an_overflow` still compute the same result as the unoptimized source on an input that does not overflow?"

    Yes. `%x.fr` is `%x` unchanged, because a non-poison value passes through `freeze` untouched; freezing only has an effect on the inputs where the original value was already poison, and on every other input it is the identity. So the rewritten add computes `%x + 1` exactly as the source did, and only the flag, not the arithmetic, has changed.

<figure class="vx-figure">
<svg viewBox="0 0 760 470" role="img" aria-label="Poison spreading through a small dependency graph of instructions, absorbed by one operation and stopped by freeze" aria-describedby="o11-f1-desc">
<title id="o11-f1-title">Poison propagation, absorption and freeze</title>
<desc id="o11-f1-desc">At the top, one instruction, an add marked nsw, may produce poison when its input is large enough to overflow. Its result feeds three paths. On the left path, an or with a non-constant operand keeps propagating the poison downward through two more instructions, each drawn as an alarmed box, ending at a result marked still poison. In the middle, an and with the constant 0 absorbs the poison: its result is drawn as an ordinary box holding the fixed value 0, with no poison marking, even though its input was poison. On the right, a freeze instruction turns the poison into one fixed, ordinary value, drawn as a value box with no alarm marking; a multiply after it is also an ordinary box, computing on the frozen value like any other instruction.</desc>
<defs><marker id="o11-f1-head" viewBox="0 0 10 10" refX="10" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker></defs>
<rect class="vx-box-strong" x="300" y="10" width="200" height="46" rx="4"/>
<text class="vx-mono" x="400" y="38" text-anchor="middle">%a = add nsw i32 %x, C</text>
<text class="vx-text-muted" x="400" y="68" text-anchor="middle">poison if the add overflows</text>
<line class="vx-flow" x1="330" y1="56" x2="180" y2="112" marker-end="url(#o11-f1-head)"/>
<line class="vx-flow" x1="400" y1="56" x2="400" y2="112" marker-end="url(#o11-f1-head)"/>
<line class="vx-flow" x1="470" y1="56" x2="620" y2="112" marker-end="url(#o11-f1-head)"/>
<text class="vx-text" x="20" y="100">still poison</text>
<rect class="vx-box-bad" x="80" y="112" width="200" height="40" rx="4"/>
<text class="vx-mono" x="180" y="137" text-anchor="middle">%b = or i32 %a, %y</text>
<line class="vx-flow" x1="180" y1="152" x2="180" y2="192" marker-end="url(#o11-f1-head)"/>
<rect class="vx-box-bad" x="80" y="192" width="200" height="40" rx="4"/>
<text class="vx-mono" x="180" y="217" text-anchor="middle">%c = mul i32 %b, 3</text>
<line class="vx-flow" x1="180" y1="232" x2="180" y2="266" marker-end="url(#o11-f1-head)"/>
<rect class="vx-box-bad" x="80" y="266" width="200" height="40" rx="4"/>
<text class="vx-text" x="180" y="291" text-anchor="middle">result: still poison</text>
<text class="vx-text" x="320" y="100">absorbed</text>
<rect class="vx-box" x="300" y="112" width="200" height="40" rx="4"/>
<text class="vx-mono" x="400" y="137" text-anchor="middle">%d = and i32 %a, 0</text>
<line class="vx-line" x1="400" y1="152" x2="400" y2="192" marker-end="url(#o11-f1-head)"/>
<rect class="vx-box-accent" x="300" y="192" width="200" height="40" rx="4"/>
<text class="vx-mono" x="400" y="217" text-anchor="middle">result: 0, never poison</text>
<text class="vx-text" x="640" y="100">frozen</text>
<rect class="vx-box-accent" x="540" y="112" width="200" height="40" rx="4"/>
<text class="vx-mono" x="640" y="137" text-anchor="middle">%f = freeze i32 %a</text>
<line class="vx-line" x1="640" y1="152" x2="640" y2="192" marker-end="url(#o11-f1-head)"/>
<rect class="vx-box-accent" x="540" y="192" width="200" height="40" rx="4"/>
<text class="vx-mono" x="640" y="217" text-anchor="middle">%g = mul i32 %f, 3</text>
<line class="vx-line" x1="640" y1="232" x2="640" y2="266" marker-end="url(#o11-f1-head)"/>
<rect class="vx-box-accent" x="540" y="266" width="200" height="40" rx="4"/>
<text class="vx-text" x="640" y="291" text-anchor="middle">result: one fixed value</text>
<text class="vx-text-muted" x="400" y="330">Only the left path can still turn into real undefined behavior, for example if %c is later used as a branch condition.</text>
<text class="vx-text-muted" x="400" y="352">The middle and right paths are ordinary values from here on, whatever %x was.</text>
</svg>
<figcaption>Figure 1. One add that may be poison feeds three paths. Poison keeps spreading through ordinary operations on the left. An absorbing operand, <code>and ... 0</code>, throws it away in the middle. A <code>freeze</code> pins it to one fixed value on the right, after which every further computation is as ordinary as if the add had never been able to overflow.</figcaption>
</figure>

## Checking a rewrite instead of trusting it

Everything so far describes what a single instruction or a short chain of them is allowed to do. An optimizer applies thousands of small rewrites, each supposedly justified by reasoning like this, and the reasoning is exactly the kind of thing that is easy to get right in the common case and wrong at the edges: an overlooked poison-producing operand, a flag copied onto a new instruction that does not deserve it, a fold that is correct for the concrete-value semantics but not for poison. Reading source code to decide whether a rewrite handles every case is not a scalable way to catch these mistakes, and testing a rewrite on a handful of chosen inputs proves only that those inputs happened to agree.

**Alive2** is a tool built to answer the question completely instead: given a pair of LLVM functions, a "before" and an "after," does the after function **refine** the before function on every possible input, accounting for poison and undefined behavior, within a bounded but exhaustive model of the instructions involved?[^alive2] It works by translating both functions into formulas in a decidable logic and handing the question to an SMT solver, which either proves the refinement holds for every input in the modeled domain or returns a concrete counterexample: specific operand values on which the two functions disagree.[^alive2] This is **translation validation**: instead of proving a compiler pass correct once, in general, for every program it might ever see, the tool checks each individual rewrite the pass actually performed, after the fact, against the two IR snippets involved.

The approach found real bugs, not hypothetical ones. Run across LLVM's own optimizations, Alive2 found 47 new miscompilation bugs, 28 of which were fixed by the time its authors published, and its authors' work on precisely specifying poison's behavior led to eight patches to the Language Reference itself, tightening wording that the tool had shown to be ambiguous or wrong.[^alive2] An earlier tool by an overlapping set of authors, Alive, had already shown the same approach could mechanically prove individual peephole optimizations correct, one rewrite rule at a time, before Alive2 extended the idea to whole functions and to a wider set of instructions.[^alive15] A public interface to the same checker, Alive2's online instance, takes a before-and-after pair of LLVM IR functions typed directly into a browser and reports either "Transformation seems to be correct" or a specific input the two disagree on.[^alive2ce]

??? check "You propose adding `nuw` to a subtraction the language has never proved cannot underflow. What would a translation-validation check most likely find, and why does testing the change on a few inputs not settle the same question?"

    A counterexample: specific operand values for which the un-flagged subtraction returns a defined, wrapped result, while the flagged version is poison for that same input and so is free to behave as anything downstream, including feeding a later fold that deletes a check the way [O8](o8-loops.md#no-wrap-flags)'s example did. Running the two versions on a handful of chosen inputs can only show that those particular inputs happened to agree; it cannot show agreement on the input that actually underflows unless that exact input is one of the ones tried. A solver-backed check considers the whole modeled input domain in one pass and either proves agreement everywhere in it or hands back the disagreement, which is a different kind of answer than a sample ever gives.

## What this means for a Vortex compiler

Vortex does not have undefined behavior in the C sense. [Expressions 5.5](../specification/expressions.md#checked-integer-operations) requires overflow, division and remainder by zero, an out-of-range shift and an out-of-range cast each to be checked, and [decision 14](../decisions/program.md#d14) makes the result of a failed check fully specified: the program stops, reports one line naming the kind of error and where it happened, and exits with status 101. An out-of-bounds array access is checked the same way.[^arrays-bounds] None of this is a gap the compiler may fill in however is convenient. It is observable behavior, as fixed as anything the program prints, and [O1](o1-optimizer-contract.md#what-an-optimizer-must-keep) already listed it among what an optimization must preserve on every input.

That is precisely why this chapter matters for Vortex rather than being a curiosity about someone else's language. Every one of Vortex's checks compiles down to ordinary control flow: a comparison, a conditional branch, and a call into the runtime on the failing path. Take a function no more complicated than one row of the matmul kernel's inner loop:

```vortex
// items: valid
fn dot(a: &[f32; 64], b: &[f32; 64], k: usize) -> f32 {
    a[k] * b[k]
}
```

`k` is a `usize` chosen at run time, so [7.6 Indexing](../specification/arrays.md#76-indexing) requires both `a[k]` and `b[k]` to be checked: compare `k` against 64, branch to the error path if the comparison fails, and only then load. Nothing about that shape is undefined-behavior-flavored by itself. The danger is downstream, in how the arithmetic *inside* the checked operation gets marked. If a Vortex front end attaches `nsw` to an addition before establishing that the addition cannot overflow, or `inbounds` to a `getelementptr` before establishing that the index is in range, it has told LLVM the same thing C's compiler is told about ordinary `+`: that the failing case cannot happen. LLVM will believe it, exactly as InstCombine believed it in the O8 example, and a later pass is then entitled to fold away the very comparison the runtime check depended on. The check would not merely become redundant. It would silently stop existing on inputs where it was needed, while the executable still contains a call to the error path that nothing reaches, and the process would carry on past a genuine overflow instead of stopping the way [decision 14](../decisions/program.md#d14) requires.

The rule, restated from [O1](o1-optimizer-contract.md#the-contract-travels-in-the-ir) and made specific to this chapter, is that a no-wrap or `inbounds` flag is not a hint about the common case. It is a claim about every execution, and it may be attached only once something has actually ruled out the failing execution, whether that is a proof from the language's own rules (the counter of a `for` loop, as [O8](o8-loops.md#no-wrap-flags) works out in detail), a constant range known at compile time, or an explicit runtime check the generated code already performs first. [Decision I8](../decisions/implementation.md#i8) points at where that ordering has to hold: a checked operation belongs before the unchecked arithmetic it guards, whether as an explicit branch or as one of LLVM's overflow-detecting intrinsics, never after it. A flag added on faith is not an optimization. It is a promise the compiler cannot keep, made on the language's behalf, about a program it has not actually looked at closely enough.

One flag is safe to add without any of this machinery, because it makes no claim about values at all: marking the runtime's error-reporting call `noreturn` tells the optimizer only that control never comes back from it, which [decision 14](../decisions/program.md#d14) already guarantees unconditionally, on every path, for every input. `noreturn` does not touch whether the check before it was needed; it only lets the optimizer treat the code after the call as unreachable, which is true regardless of why the call was reached. It cannot make a real overflow disappear the way an unproved `nsw` can, because it makes no claim about the operands at all.

## For Vortex

!!! vortex "Exercise"

    **Build a proof-obligation table**, one row per checked operation Vortex's specification defines (checked add, subtract, multiply, divide, remainder, shift and cast, and array indexing). For each row, name:

    1. the LLVM flag or attribute you would eventually want to attach (`nsw`, `nuw`, an `exact` shift, `inbounds`, or none);
    2. the proof obligation that flag requires, stated as a fact about the operands;
    3. where in your compiler that fact could come from once it exists: a language rule that holds unconditionally (as a `for` loop's bound does), a constant known at compile time, or a runtime check already performed on the same path;
    4. what happens to the row today, before any such analysis exists: no flag is attached, and the explicit check stays.

    Then pick three rows and validate the proposed rule with [Alive2's online checker](https://alive2.llvm.org/ce/):[^alive2ce] write a small pair of LLVM IR functions, "before" using the flag under the exact condition your proof would establish, "after" showing what an optimizer is then entitled to simplify it to, and confirm the tool reports the transformation correct. For one row, deliberately write the "before" function with the flag attached under a *weaker* condition than the proof actually gives, and record the counterexample the tool returns.

    **Not yet:** any pass in your own compiler that attaches these flags; any analysis that computes the ranges or proofs the table's third column names; a homegrown correctness checker of your own.

    **Proof that it works:** the table has one row per checked operation with no blanks in columns 1 through 3. Three rows have a saved Alive2 transcript showing "Transformation seems to be correct" for the justified version. The deliberately weakened row has a saved transcript showing a concrete counterexample instead, and one sentence connecting that counterexample to which of your compiler's checks it would have deleted if the flag had shipped.

## Key ideas

!!! recap "Questions you can now answer"

    - **What separates immediate undefined behavior from deferred undefined behavior?** Immediate: the operation itself is already meaningless the instant it runs, such as integer division by zero. Deferred: the operation returns a poison value, and nothing has gone wrong until that value reaches one of a specific list of later uses.
    - **What does a poison value mean, in plain terms?** "This instruction's side condition, such as no overflow, was violated, but the program has not observed that yet; treat the result as if it could be anything."
    - **Does poison always spread to every instruction that touches it?** No. An absorbing operand, such as `and` with a constant `0` or `or` with a constant `1`, can decide an instruction's result regardless of a poisoned operand, and a `select` that picks the non-poisoned branch never exposes the poisoned one.
    - **What does `freeze` do, and why does an optimizer need it?** It turns a poison or undef value into one arbitrary but fixed value, unchanged for every further use; without it, a value duplicated by a transformation could resolve to a different concrete value at each copy, producing behavior no single execution of the source program could have.
    - **What does translation validation check that testing a rewrite does not?** Agreement between a "before" and "after" version of a rewrite across every input in a bounded but exhaustive model, accounting for poison and undefined behavior, rather than agreement on the handful of inputs someone thought to try.
    - **When may a Vortex compiler attach `nsw`, `nuw` or `inbounds`?** Only once something (a language rule, a constant range, or a runtime check already performed) has ruled out the failing case for every input; otherwise the flag licenses the optimizer to delete the very check the flag was supposed to sit beside.

## Where this comes back

!!! next "You will use this again in"

    - [O12. Testing an optimizer](o12-testing-optimizers.md): *differential testing*, *translation validation*
    - [C7. Peephole optimization](../backend/c7-peephole.md): *rewrite rules with preconditions*, *proving a rewrite correct*
    - [M3. Passes and pattern rewriting](../mlir/m3-passes-and-rewriting.md): *rewrites that must keep meaning*

## Sources and further reading

[^langref]: LLVM Project, "LLVM Language Reference Manual", sections "Undefined Values", "Poison Values", "'freeze' Instruction", "'add' Instruction" (`nsw`, `nuw`), "'getelementptr' Instruction" (`inbounds`) and "'udiv' Instruction" (`exact`). <https://llvm.org/docs/LangRef.html>
[^llvm-ub]: LLVM Project, "LLVM IR Undefined Behavior Manual". <https://llvm.org/docs/UndefinedBehavior.html>
[^lee17]: Juneyoung Lee, Yoonseung Kim, Youngju Song, Chung-Kil Hur, Sanjoy Das, David Majnemer, John Regehr and Nuno P. Lopes, "Taming Undefined Behavior in LLVM", *Proceedings of the 38th ACM SIGPLAN Conference on Programming Language Design and Implementation (PLDI)*, 2017, abstract and section 1. <https://doi.org/10.1145/3062341.3062343> (free copy: <https://users.cs.utah.edu/~regehr/papers/undef-pldi17.pdf>)
[^alive2]: Nuno P. Lopes, Juneyoung Lee, Chung-Kil Hur, Zhengyang Liu and John Regehr, "Alive2: Bounded Translation Validation for LLVM", *Proceedings of the 42nd ACM SIGPLAN International Conference on Programming Language Design and Implementation (PLDI)*, 2021, abstract and section 1. <https://doi.org/10.1145/3453483.3454030> (free copy: <https://users.cs.utah.edu/~regehr/alive2-pldi21.pdf>)
[^alive15]: Nuno P. Lopes, David Menendez, Santosh Nagarakatte and John Regehr, "Provably Correct Peephole Optimizations with Alive", *Proceedings of the 36th ACM SIGPLAN Conference on Programming Language Design and Implementation (PLDI)*, 2015, abstract and section 3. <https://doi.org/10.1145/2737924.2737965> (free copy: <https://users.cs.utah.edu/~regehr/papers/pldi15.pdf>)
[^alive2ce]: The Alive2 Project, online interpreter and source repository. <https://alive2.llvm.org/ce/> and <https://github.com/AliveToolkit/alive2>
[^arrays-bounds]: [Specification: Arrays](../specification/arrays.md#76-indexing) section 7.6, and [decision 12](../decisions/arrays.md#d12).

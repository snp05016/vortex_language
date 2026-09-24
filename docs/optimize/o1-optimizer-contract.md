# O1. The optimizer's contract

<p class="page-intro">An optimizer may change how a program runs, never what it does. This chapter pins down what "what it does" means for Vortex, shows three ways to check that a rewrite keeps it, and shows how a compiler reports each decision it makes, so that the contract exists before the first optimization pass does.</p>

<p class="vx-meta" markdown="1">Level: Foundations · Reading time: about 40 minutes · Builds on: [5. Types and language rules](../compiler/guide/stage-5-types-and-rules.md), [6. The first machine code](../compiler/guide/stage-6-first-machine-code.md)</p>

???+ remember "Before you start, remember"

    ??? question "What may the lowering pass decide for itself?"

        Nothing about meaning. It writes out, step by step, what earlier stages settled: the shape of each expression, what each name refers to, the type of every value and the left-to-right order of operands. It never re-decides any of them.

        Introduced in [6. The first machine code](../compiler/guide/stage-6-first-machine-code.md#lowering-the-first-program).

    ??? question "What does the program that leaves stage 5 promise the stages after it?"

        That it passed every static rule. Lowering and the back end never see an ill-formed program, so they do not check those rules again.

        Introduced in [5. Types and language rules](../compiler/guide/stage-5-types-and-rules.md#the-gate).

    ??? question "What happens when an `i32` addition overflows at run time?"

        The program stops. It writes out everything printed so far, then one line on standard error, `runtime error[overflow]: <message> at <file>:<line>:<column>`, and exits with status 101.

        Introduced in [Expressions 5.5](../specification/expressions.md#checked-integer-operations) and [decision 14](../decisions/program.md#d14).

    ??? question "May a compiler turn `sum += a[row, k] * b[k, column]` into one fused multiply-add?"

        No. Each `f32` operation is one IEEE 754 operation, rounded once to nearest with ties to even, in the written order: no fused multiply-add, no reordering, no wider format, no flush-to-zero.

        Introduced in [6. The first machine code](../compiler/guide/stage-6-first-machine-code.md#lowering-the-first-program) and [decision 56](../decisions/numbers.md#d56).

    ??? question "What does passing `&mut c` promise about the other arguments of the same call?"

        That none of them is `c`. A variable lent as `&mut` may appear in no other argument of that call, so storage reached through a `&mut` parameter is reachable through no other parameter.

        Introduced in [5. Types and language rules](../compiler/guide/stage-5-types-and-rules.md#shared-and-mutable-references), [References 9.8](../specification/references.md#98-aliasing) and [decision 25](../decisions/references.md#d25).

!!! goals "In this chapter"

    - Explain what an optimization must preserve in Vortex, and why that list comes from the language rather than from the optimizer.
    - Decide whether a rewrite is correct by checking it against every input, and name the precondition that turns a wrong rewrite into a correct one.
    - Recognize rewrites that are correct in C, under fast-math or with LLVM's flags, but wrong for Vortex.
    - Read LLVM's optimization remarks and records, and tell a refusal on legality from a refusal on cost.
    - Design the contract and the remark stream for your compiler before its first optimization pass.

## One line, two answers

Here is a function that asks whether adding one makes a number larger:

```vortex
// items: valid
fn grows(x: i32) -> bool {
    return x + 1 > x;
}
```

For almost every input the answer is `true`, and anyone who trusts algebra would replace the body with `return true;`. A C compiler does exactly that for the same line written with `int`. Apple clang 21 at `-O2` compiles `int grows(int x) { return x + 1 > x; }` to an AArch64 function that puts 1 in the result register and returns, without looking at `x` (checked on the owner's M4 Pro, 2026-09-24). cppreference describes the same effect on an overflow test, `if (n + 1 < n)`, which some compilers delete outright.[^asif]

The C compiler is right about C. Signed overflow in C is **undefined behavior**: the language places no requirement at all on what the program does. So for the one input where `x + 1` overflows, the C program has no required behavior, and returning 1 is as good as anything else. Vortex gives that input a definite meaning. With `x` equal to 2147483647, the largest `i32`, the checked `+` fails: the program must write its runtime error line and exit with status 101 ([Expressions 5.5](../specification/expressions.md#checked-integer-operations), [decision 14](../decisions/program.md#d14)). A Vortex compiler that returned `true` would turn a reported error into a wrong answer.

The same rewrite is correct in one language and wrong in the other. An **optimization** is a rewrite of a program that makes it faster or smaller, and whether a rewrite is allowed never depends on the optimizer alone. It depends on what the language promises about programs. Those promises, written down from the optimizer's side, form the **optimizer's contract**: what every rewrite must keep, and what it is free to change.

Vortex v0.1 lists "an optimizer contract or performance guarantee" among the things it leaves out ([specification](../specification/index.md)), and the [roadmap](../roadmap.md#after-v01) places optimization, with "optimization diagnostics and cost models", after v0.1. This chapter assembles the contract from rules Vortex already has, so that the first pass you write has something to be correct against.

## What an optimizer must keep

### The as-if rule

C and C++ state their contract as the **as-if rule**: a compiler may transform a program in any way, provided the program's **observable behavior**, the part of its behavior the outside world can see, stays the same.[^asif] In C++ that means accesses to `volatile` objects, the data written to files, and prompts shown on an interactive device before the program waits for input. Everything else is invisible: which instructions run, which registers hold which values, whether a computation happens at all.

The rule gives the optimizer its freedom and its limits in one sentence. Deleting a variable that is never read is fine, because no one can see the variable. Swapping two writes to a file is not, because the file is what the world sees.

### Vortex's list

Vortex has its own version, and it is short. [Conformance 1.3](../specification/conformance.md#13-implementation-conformance) defines a program's observable behavior as what it writes to standard output, the runtime error line it writes to standard error, and its exit status. [Conformance 1.6](../specification/conformance.md#16-implementation-defined-behavior) adds that optimization strategy is an implementation choice that must not change a program's meaning. Every clause of the contract follows from those three items and the rules around them.

- **Every byte that `print` writes, in order.** `print` evaluates its arguments left to right and writes them with one space between ([decision 4](../decisions/program.md#d4)). An optimizer may compute a printed value earlier or by another route, but the bytes and their order are fixed.
- **Whether the program stops, where, and after what.** A failed check writes out everything printed so far, then exactly one error line naming the kind of check and the position of the failing operation, then exits with status 101 ([Diagnostics 10.6](../specification/diagnostics.md#106-runtime-reporting)). So a check cannot move above a `print`, cannot trade places with another check that might fail first, and cannot disappear unless it provably never fails: [Conformance 1.4](../specification/conformance.md#14-static-and-dynamic-rules) allows omitting a runtime check only when the implementation proves it cannot fail.
- **No failure added, none lost.** A check that might fail must not run where the original ran none, as it would if a division moved out of a loop that might run zero times. A check the compiler can prove will fail stays as well: unless its operands are integer constant expressions, it fails at run time, after the output that comes before it ([decision 39](../decisions/diagnostics.md#d39)).
- **Every bit of every floating-point result.** `print` writes a float as the shortest decimal that reads back as the same value, and writes `0.0` and `-0.0` differently ([Programs and declarations 3.9](../specification/declarations.md#39-built-in-functions)), so a change to any bit of a printed float changes the output. [Decision 56](../decisions/numbers.md#d56) goes further and forbids fused, reordered, widened or flushed operations whether or not their results are ever printed, and the rule covers values computed during compilation too. One detail is not observable: which NaN a computation produces, because `print` writes every NaN as `NaN`.
- **The exit status.** It is 0 when `main` returns and 101 after a runtime error. A program that loops forever never produces a status, so deleting a loop that might never finish would turn "no status" into 0. Compiler testers count exactly this, a program that ends when it should have run forever, as a wrong-code bug.[^csmith] The Vortex specification does not say it in so many words. It follows from counting the exit status as observable, and your contract should state it.

Everything outside that list belongs to the optimizer: time, memory, instruction choice, the order of computations whose results nobody can tell apart, and every value that affects none of the three.

The list reaches one level deeper than it first appears. Decision 14 leaves the message inside the error line implementation-defined, and the suggested default, [I7](../decisions/implementation.md#i7), names the values involved, as in `2147483647 + 1 does not fit in i32`. An implementation must apply its documented choice consistently (Conformance 1.6). Suppose an optimizer rewrites `x * 2` as `x + x`. The failing operation keeps its kind and, if the new operation keeps the old source span, its position; but a message built from the new operation would print `+` where the program says `*`. Either the rewrite carries the original operation along for the message, or the documented message leaves the operator out. The contract has to choose.

### Code that looks dead can still fail

The difference from C shows most in code whose result is never used:

```vortex
// items: valid
fn keep_total(total: i32, count: i32) -> i32 {
    let ratio = total / count;   // never read again
    return total;
}
```

`ratio` is never read, so a C compiler would delete the division. Xavier Leroy's account of CompCert, a compiler for a large subset of C whose correctness proof is checked by machine, uses exactly this case to explain why its correctness statement lets compiled code fail less often than its source: if the source could go wrong on a division by zero whose result is unused, the compiled code may drop the division and not go wrong.[^compcert]

In Vortex, division by zero is not undefined. It is a runtime error of kind `divide-by-zero`, and that is observable. The quotient is dead, but the check is alive. [Expressions 5.10](../specification/expressions.md#510-evaluation-order) says it in one sentence: optimization may remove an evaluation only when the observable behavior and the required diagnostics stay the same. This division can fail in two ways ([Expressions 5.5](../specification/expressions.md#checked-integer-operations)): `count` is zero, or `total` is -2147483648 and `count` is -1, a quotient too large for `i32`. An optimizer may delete the whole line only after proving both impossible. Otherwise it may drop the quotient, but it must keep both tests.

??? check "A function runs `let unused = values[i];` and never reads `unused`. When may an optimizer delete the line?"

    When it can prove the index is in bounds, for example because `i` comes from `for i in 0..4` and `values` has type `&[f32; 4]`. Without that proof the bounds check must stay, since a failure writes an error line and exits with status 101. The load itself may go either way: nobody can observe it.

## Refinement: a rule for every input

"Keep the observable behavior" needs one more piece before anyone can check it: the behavior on which input? The precise answer compares the two programs input by input. For each input, the original program allows a set of behaviors, and the rewritten program is correct when, for every input, what it does is in that set. Alive2, a tool that checks LLVM's optimizations, calls this relation **refinement**: for every input, the rewritten program shows a subset of the behaviors of the original.[^alive2] A rewrite may remove possibilities. It may never add one.

An input here is anything the rewritten code does not control. A Vortex v0.1 program as a whole reads nothing: `main` takes no parameters and `print` is the only built-in function ([Conformance 1.2](../specification/conformance.md#12-programs), [Programs and declarations 3.9](../specification/declarations.md#39-built-in-functions)). An optimizer, though, rewrites one function or one loop at a time, so its inputs are that function's arguments and the values that flow into that loop.

<figure class="vx-figure">
<svg viewBox="0 0 760 350" role="img" aria-label="What each input allows, in C and in Vortex" aria-describedby="o1-f1-desc">
<title id="o1-f1-title">What each input allows, in C and in Vortex</title>
<desc id="o1-f1-desc">Two columns, C on the left and Vortex on the right, each with two rows of boxes. In every box, an outlined region shows what the original program allows for one input, and a dot shows what the rewritten program, return true, does. For x equal to 5, both languages allow only true, and the dot lies inside. For x equal to 2147483647, C's undefined overflow allows every behavior, so the region fills the whole box and the dot lies inside. Vortex allows only the overflow error followed by exit status 101, so the region is small and the dot, true, lies outside it: the rewrite is wrong for Vortex.</desc>
<text class="vx-text" x="40" y="24">C, with int</text>
<text class="vx-text-muted" x="40" y="42">overflow is undefined</text>
<text class="vx-text" x="400" y="24">Vortex, with i32</text>
<text class="vx-text-muted" x="400" y="42">overflow is a runtime error</text>
<text class="vx-text-muted" x="40" y="74">input x = 5</text>
<rect class="vx-box" x="40" y="84" width="320" height="64" rx="4"/>
<rect class="vx-box-accent" x="52" y="94" width="124" height="44" rx="4"/>
<text class="vx-mono" x="68" y="121">true</text>
<circle class="vx-dot" cx="150" cy="116" r="6"/>
<text class="vx-mono" x="200" y="112">false</text>
<text class="vx-text-muted" x="200" y="132">an error, a crash</text>
<text class="vx-text-muted" x="400" y="74">input x = 5</text>
<rect class="vx-box" x="400" y="84" width="320" height="64" rx="4"/>
<rect class="vx-box-accent" x="412" y="94" width="124" height="44" rx="4"/>
<text class="vx-mono" x="428" y="121">true</text>
<circle class="vx-dot" cx="510" cy="116" r="6"/>
<text class="vx-mono" x="560" y="112">false</text>
<text class="vx-text-muted" x="560" y="132">an error, a crash</text>
<text class="vx-text-muted" x="40" y="180">input x = 2147483647, the largest value</text>
<rect class="vx-box" x="40" y="190" width="320" height="110" rx="4"/>
<rect class="vx-box-accent" x="48" y="198" width="304" height="94" rx="4"/>
<text class="vx-text-muted" x="62" y="220">undefined: every behavior is allowed</text>
<text class="vx-mono" x="68" y="254">true</text>
<circle class="vx-dot" cx="150" cy="249" r="6"/>
<text class="vx-mono" x="200" y="254">false</text>
<text class="vx-text-muted" x="200" y="274">an error, a crash</text>
<text class="vx-text-muted" x="400" y="180">input x = 2147483647, the largest value</text>
<rect class="vx-box" x="400" y="190" width="320" height="110" rx="4"/>
<rect class="vx-box-accent" x="412" y="200" width="200" height="58" rx="4"/>
<text class="vx-mono" x="424" y="224">runtime error[overflow]</text>
<text class="vx-text-muted" x="424" y="244">then exit status 101</text>
<circle class="vx-dot vx-pulse" cx="646" cy="228" r="6"/>
<text class="vx-mono" x="660" y="233">true</text>
<text class="vx-mono" x="660" y="262">false</text>
<text class="vx-text-accent" x="412" y="284">the dot lands outside: wrong</text>
<circle class="vx-dot" cx="46" cy="330" r="6"/>
<text class="vx-text-muted" x="58" y="334">what the rewritten program does</text>
<rect class="vx-box-accent" x="272" y="322" width="22" height="16" rx="3"/>
<text class="vx-text-muted" x="302" y="334">what the original program allows</text>
<rect class="vx-box" x="524" y="322" width="22" height="16" rx="3"/>
<text class="vx-text-muted" x="554" y="334">everything a program could do</text>
</svg>
<figcaption>Figure 1. For each input, the original program allows a set of behaviors (the outlined regions), and the rewritten program <code>return true;</code> must land inside it (the dots). At x = 5 both languages allow only <code>true</code>. At the largest value, C's undefined overflow allows everything, so the rewrite is correct for C. Vortex allows only the overflow error, and <code>true</code> lands outside.</figcaption>
</figure>

Figure 1 shows why the two languages disagree about `grows`. When `x + 1` overflows in C, the set of allowed behaviors is everything, so any rewrite lands inside it. That is where a C optimizer's room comes from: every input that triggers undefined behavior is an input the optimizer need not respect.

Vortex has no such inputs. [Conformance 1.5](../specification/conformance.md#15-undefined-behavior) says a well-formed program exposes no undefined behavior: an invalid operation is either rejected before the program runs or fails through the documented runtime report. The implementation-defined choices, such as the message text and the stack size, are fixed and documented once per implementation ([Conformance 1.10](../specification/conformance.md#110-implementation-defined-behavior-and-limits)). So for a given Vortex implementation every input allows exactly one behavior (running out of stack is the one subtle case, and the exercise returns to it), and the subset rule collapses to equality. The Alive2 authors say the same of LLVM IR: "In the absence of undefined behaviors, refinement degenerates to simple equivalence."[^alive2]

That is the whole contract in one sentence. **A Vortex optimization is correct when, for every input, the optimized program writes the same output, the same error line and the same exit status as the original.**

Many useful rewrites are not correct for every input on their own. They are correct given a fact, and that fact is the rewrite's **precondition**. `x + 1 > x` becomes `true` correctly when `x` is known to be below the largest `i32`, which is exactly what is known about the variable of `for i in 0..64`. Finding such facts is the work of the analyses in later chapters ([O4](o4-dataflow.md), [O8](o8-loops.md)). The contract only asks that every fact a rewrite relies on has been proved.

## Three ways to know a rewrite is correct

### Try every input

If a rewrite has few enough inputs, run both versions on all of them and compare. The first example does this for four integer rewrites over 8-bit integers, trying the first of them a second time with a precondition. One 8-bit input has 256 values, and three inputs have 16,777,216 combinations.

The example checks each rule under three meanings of overflow: the result **wraps** around, as with LLVM's plain `add`; overflow is **undefined**, as for C's signed `int`; or overflow is **checked** and stops the program, as in Vortex. Under the undefined meaning, an input on which the original overflows cannot be a counterexample, since anything is allowed there. Under the other two meanings both sides must agree, and an error counts as an outcome.

--8<-- "includes/examples/optimize/o1-optimizer-contract/rewrite_check.cpp.md"

Read the output one meaning at a time. Wrapping arithmetic allows the algebraic rules, `(x + y) - y` to `x` and reassociation (regrouping `(x + y) + z` as `x + (y + z)`), because arithmetic modulo 256 obeys those laws, and it rejects `x + 1 > x` to `true` at 127.

Undefined overflow allows `x + 1 > x` to `true`, the fold from the opening section, but rejects reassociation: at `x = -128, y = 1, z = 127` the original computes 0 without overflowing, while the rewritten version overflows in `y + z`, so the rewrite has introduced undefined behavior where there was none. Checked arithmetic is the strictest of the three. Only `x * 2` to `x + x` survives unconditionally, and the precondition `x < 127` rescues `x + 1 > x`.

Each "wrong" line is a **counterexample**: one input on which the two versions differ. A single counterexample settles the question. No number of agreeing inputs does, unless the inputs are all of them.

Eight bits stand in for 32 here because three 32-bit inputs have 2<sup>96</sup> combinations, far too many to try. That is a real limit: a rule that holds at 8 bits is evidence, not proof, for `i32`. The Alive tool treats a rule as generic over bit widths and checks it at every width it can take, up to 64 bits by default.[^alive]

### Prove it with a precondition

For the rules that matter, argue once for every input. Take `x * 2` to `x + x` for Vortex's checked `i32`. Both sides compute the exact value 2x. Both fail exactly when 2x lies outside the range of `i32`, so they fail on the same inputs, with the same kind, `overflow`, at the same position if the new operation keeps the old operation's source span. (Whether the message may change is the question the previous section left open.) On every other input both give 2x. Every input is covered, so the rule is correct.

A precondition shrinks the set of inputs the argument has to cover. Take `x + 1 > x` to `true`, given that `x` is below 2147483647. On every such input `x + 1` fits in `i32`, so the addition does not fail and yields the exact value x + 1, which is greater than x. The comparison is `true` on every input the precondition admits, and those are the only inputs the rewrite is applied to.

Proofs like this go wrong at the edges: zero, the largest and smallest values, a negative operand. Every counterexample in the example sits at such an edge.

### Hand it to a solver

A **solver** is a program that searches for values satisfying a logical formula. Alive, from 2015, lets a compiler writer state a peephole rewrite, a local rewrite of a few instructions, together with its precondition, and asks an SMT (satisfiability modulo theories) solver, one that reasons about bit vectors and similar data, for an input on which source and target differ. The answer is either a proof for every input or a counterexample. Its authors translated more than 300 of LLVM's optimizations into Alive, and found eight incorrect ones in InstCombine, LLVM's pass of local algebraic simplifications.[^alive]

Alive2, from 2021, turns the question around. Instead of proving each rule once, it checks each compilation: given a function before and after optimization, it asks whether the second refines the first. That approach is **translation validation**, checking that one run of the compiler kept the meaning, instead of proving the compiler correct for every program. Run over LLVM's own unit tests, Alive2 found 47 new bugs, 28 of them fixed by the time of publication, and led to eight changes in the LLVM Language Reference. It is bounded, for example unrolling loops only up to a limit, so it can miss bugs, but it is designed to avoid false alarms.[^alive2]

At the far end is the **verified compiler**, whose correctness is proved once for every program it accepts. CompCert is the standard example: it comes with a machine-checked proof that the generated code behaves as the semantics of the source prescribes.[^compcert] The Csmith project compiled randomly generated programs with many C compilers and reported more than 325 previously unknown bugs. As of early 2011, the development version of CompCert was the only compiler in which it could not find wrong-code errors, and the wrong-code bugs it had found in earlier CompCert versions were in parts that had not been proved.[^csmith]

### Testing finds bugs; proofs rule them out

Compiling the same program two ways, with two compilers or with optimization on and off, and comparing what the two executables do is a **differential test**. It is how Csmith found its bugs, and it is the cheapest check to build, but it only ever finds counterexamples; it never shows that none exist. [O12](o12-testing-optimizers.md) builds such tests for a whole optimizer. For Vortex the natural first one compares every test program with optimization on and off, and it is part of this chapter's exercise.

??? check "Are `x - x` to `0` and `(x * 2) / 2` to `x` correct for Vortex's `i32`?"

    The first, yes: the exact value of `x - x` is 0 for every `x`, so it never overflows, and both sides give 0 on every input. The second, no: for `x` = 1073741824, `x * 2` overflows and the original stops with an overflow error, while the rewritten code returns `x`. It becomes correct under the precondition that `x * 2` fits in `i32`, that is, -1073741824 ≤ x ≤ 1073741823.

## Floating point: identities that are false

For floating-point numbers the contract is the strictest part of Vortex, and the part least like school algebra. Each operation rounds its exact result to the nearest representable value, so the laws of real numbers hold only sometimes. David Goldberg's survey gives the classic case: with x = 10<sup>30</sup>, y = −10<sup>30</sup> and z = 1, `(x + y) + z` is 1 while `x + (y + z)` is 0.[^goldberg]

The second example tries eight rewrites that hold for real numbers on sample `f32` values, and compares the results bit for bit. It treats any two NaNs as equal, because a Vortex program cannot tell them apart.

--8<-- "includes/examples/optimize/o1-optimizer-contract/float_rewrites.cpp.md"

Each failure is a way a well-meaning optimizer could change what a Vortex program prints.

- `x + 0.0` to `x` fails at `-0.0`, because `-0.0 + 0.0` is `+0.0` when rounding to nearest. Adding `-0.0` instead returns every input unchanged, so that rewrite is correct.
- `x - x` to `0.0` fails at infinity, where the difference is NaN.
- `x / 2.0` to `x * 0.5` is correct: 0.5 is exact, and both sides round the same exact value. `x / 10.0` to `x * 0.1` is wrong at `x = 9`, because 0.1 has no exact binary form. Goldberg uses exactly this pair to show how compiler textbooks, by ignoring floating point, lead their readers astray.[^goldberg]
- Reassociating `(x + y) + z` fails, and so does computing the whole sum in `double` and rounding once at the end: both give 1 where the program as written gives 0. Decision 56 forbids both, reordering and wider formats alike.
- Fusing `x * y + z` into one fused multiply-add rounds once instead of twice, and turns 0 into 2<sup>−24</sup>.

The last line hides a trap for anyone who writes such a checker. The example is built with `-ffp-contract=off`. Without that flag, Apple clang 21 on the M4 Pro fuses `x * y + z` itself, and the example reports the fused multiply-add rule as correct, because both sides have become the same fused operation (checked 2026-09-24). Clang's default floating-point model allows **contraction**, fusing a multiplication and an addition into one operation, within an expression,[^clang-um] so any C or C++ program used as a reference for Vortex's results needs the flag as well, as [stage 6](../compiler/guide/stage-6-first-machine-code.md#generating-c) says for a back end that generates C.

Strictness is not pedantry. Compensated summation, a standard way to add many numbers accurately, depends on exactly the rounding that an algebra-minded optimizer would remove:

```vortex
// items: valid
fn compensated_sum(values: &[f32; 1024]) -> f32 {
    let mut sum: f32 = 0.0;
    let mut carry: f32 = 0.0;
    for i in 0..1024 {
        let y = values[i] - carry;
        let t = sum + y;
        carry = (t - sum) - y;   // the part of y that the addition lost
        sum = t;
    }
    return sum;
}
```

In real arithmetic `t - sum` equals `y`, so `carry` is always zero, and an optimizer that believed algebra would delete it and leave a plain sum. Goldberg presents this algorithm, Kahan's summation formula, as one that such an optimization ruins.[^goldberg] Vortex's rule keeps it working.

The optimizer still has plenty of room: every rewrite that produces the same rounded result on every input, such as `x + (-0.0)` to `x` and `x / 2.0` to `x * 0.5`, and everything that leaves the arithmetic alone. It may compute `a * b` once where the program writes it twice, keep values in registers, move work out of loops, and process independent elements side by side, as with the separate elements of a result array ([P7](p7-loop-transformations.md), [P10](p10-vectorization.md)).

??? check "May an optimizer replace `x / 4.0` with `x * 0.25` for `f32`? `x / 3.0` with `x * (1.0 / 3.0)`?"

    The first, yes: 0.25 is a power of two, so it is exact, and both sides round the same exact quotient. The second, no: `1.0 / 3.0` is already rounded, so multiplying by it can differ from dividing by 3 in the last bit, as `x * 0.1` differs from `x / 10.0` at `x = 9`. For `f32`, `x = 5` is one such input.

## The contract travels in the IR

An optimizer such as LLVM never sees Vortex. It sees LLVM IR, and the Clang manual points out that the back end knows nothing about the source language.[^clang-um] Whatever the language promises has to be written into the IR, instruction by instruction, or the optimizer cannot use it; and whatever the IR claims, the optimizer will believe.

LLVM IR carries the contract in flags and attributes:[^langref]

- A plain `add` wraps around. With `nsw` (no signed wrap) or `nuw` (no unsigned wrap), an overflow produces a **poison value**, a value that marks the result of an invalid operation. Poison may be replaced by any value of its type, so for an input that overflows, the optimizer may pick whatever result suits it.
- A plain `fadd`, `fmul` or `fdiv` follows IEEE 754 in LLVM's default floating-point environment, which assumes rounding to nearest, keeps subnormal values (the tiny values nearest zero), and treats floating-point exceptions as invisible. When its result is not a NaN, it gives the same bits on every machine and at every optimization level. Only a NaN result's sign and payload are left open, and that is the one detail Vortex cannot observe.
- **Fast-math flags** relax those rules one instruction at a time: `nnan` and `ninf` let the optimizer assume no NaN or infinity, `nsz` lets the sign of a zero change, `arcp` allows multiplying by a reciprocal, `contract` allows fusing, `afn` allows approximate functions, and `reassoc` allows reassociation. `fast` means all of them.
- Attributes carry other promises. `noalias` on a pointer parameter promises that, during the call, memory that is written and reached through that pointer is reached through no other pointer; the Language Reference calls this definition intentionally similar to C99's `restrict`. `mustprogress` on a function models C++'s forward-progress rule: a loop in it that does nothing observable may be assumed to finish, and may be removed.

The third example hands the rewrites from the first two examples to LLVM 18's instruction combiner, with and without flags:

--8<-- "includes/examples/optimize/o1-optimizer-contract/ir_flags.ll.md"

In each function the optimizer goes exactly as far as the flags allow. Without `nsw`, `x + 1 > x` becomes `x != 127`, which is the precise truth for wrapping 8-bit arithmetic. With `nsw` it becomes `true`, the C answer. `x + 0.0` stays until `nsz` says zeros may lose their sign. `x / 2.0` becomes a multiplication with no flag at all, because that rewrite is exact; `x / 10.0` becomes one only with `arcp`.

The Alive2 authors observe that front ends use undefined behavior deliberately, to tell the optimizer facts about the code.[^alive2] Every flag is a statement of that kind, a promise or a permission the front end gives on the language's behalf. For a Vortex front end that emits LLVM IR, that gives a rule: attach a flag only when a rule of the language or a proof stands behind it.

Some promises Vortex can make where C needs the programmer to write `restrict` by hand, such as `noalias` on every `&mut` parameter. [References 9.8](../specification/references.md#98-aliasing) guarantees that no other parameter of the call reaches the same storage, and says that code generation may rely on it ([decision 25](../decisions/references.md#d25)). Some promises Vortex must never make in v0.1: every fast-math flag, because of decision 56, and `mustprogress`, because no rule of Vortex says that a loop terminates. And `nsw` promises that an addition never overflows, which a checked `+` does not promise by itself; the flag needs a proof behind it. When a Vortex program misbehaves only under optimization, the first suspect is a flag that promised more than the language does.

## Legal, then worth doing

Every optimization decision asks two questions, and they have different owners. The first is **legality**: does the contract allow the change? The language answers it, and the answer is the same on every machine. The second is **profitability**: will the change make this program faster on this machine? A **cost model**, the optimizer's estimate of what each version would cost on the target, answers it, and the answer changes with the target.

<figure class="vx-figure">
<svg viewBox="0 0 760 300" role="img" aria-label="Legality first, then profitability, and the remark each outcome produces" aria-describedby="o1-f2-desc">
<title id="o1-f2-title">Legality first, then profitability</title>
<desc id="o1-f2-desc">A candidate transformation flows right into a box labelled Legal, decided by the contract, then into a box labelled Profitable, decided by the cost model, then into a box labelled Transform. From Legal, a no arrow leads down to a missed remark with an analysis remark naming the rule. From Profitable, a no arrow leads down to a missed remark naming the estimate that lost. From Transform, an arrow leads down to a passed remark saying what was done. Under the legality box: same answer on every machine. Under the profitability box: the answer depends on the target.</desc>
<rect class="vx-box" x="16" y="40" width="150" height="60" rx="4"/>
<text class="vx-text" x="91" y="66" text-anchor="middle">A candidate</text>
<text class="vx-text-muted" x="91" y="86" text-anchor="middle">vectorize this loop</text>
<rect class="vx-box-strong" x="206" y="40" width="160" height="60" rx="4"/>
<text class="vx-text" x="286" y="66" text-anchor="middle">Legal?</text>
<text class="vx-text-muted" x="286" y="86" text-anchor="middle">the contract decides</text>
<rect class="vx-box-strong" x="406" y="40" width="160" height="60" rx="4"/>
<text class="vx-text" x="486" y="66" text-anchor="middle">Profitable?</text>
<text class="vx-text-muted" x="486" y="86" text-anchor="middle">the cost model decides</text>
<rect class="vx-box-accent" x="606" y="40" width="138" height="60" rx="4"/>
<text class="vx-text" x="675" y="66" text-anchor="middle">Transform</text>
<text class="vx-text-muted" x="675" y="86" text-anchor="middle">then report it</text>
<line class="vx-flow" x1="166" y1="70" x2="198" y2="70"/>
<polygon class="vx-arrowhead" points="198,65 206,70 198,75"/>
<line class="vx-flow" x1="366" y1="70" x2="398" y2="70"/>
<polygon class="vx-arrowhead" points="398,65 406,70 398,75"/>
<text class="vx-text-muted" x="386" y="60" text-anchor="middle">yes</text>
<line class="vx-flow" x1="566" y1="70" x2="598" y2="70"/>
<polygon class="vx-arrowhead" points="598,65 606,70 598,75"/>
<text class="vx-text-muted" x="586" y="60" text-anchor="middle">yes</text>
<line class="vx-line" x1="286" y1="100" x2="286" y2="160"/>
<polygon class="vx-arrowhead" points="281,160 286,168 291,160"/>
<text class="vx-text-muted" x="296" y="136">no</text>
<line class="vx-line" x1="486" y1="100" x2="486" y2="160"/>
<polygon class="vx-arrowhead" points="481,160 486,168 491,160"/>
<text class="vx-text-muted" x="496" y="136">no</text>
<line class="vx-line" x1="675" y1="100" x2="675" y2="160"/>
<polygon class="vx-arrowhead" points="670,160 675,168 680,160"/>
<g class="vx-seq" style="--vx-i: 0; --vx-n: 3">
<rect class="vx-box" x="206" y="168" width="160" height="72" rx="4"/>
<text class="vx-text" x="286" y="192" text-anchor="middle">missed</text>
<text class="vx-text-muted" x="286" y="211" text-anchor="middle">plus an analysis remark:</text>
<text class="vx-text-muted" x="286" y="228" text-anchor="middle">the rule that forbids it</text>
</g>
<g class="vx-seq" style="--vx-i: 1; --vx-n: 3">
<rect class="vx-box" x="406" y="168" width="160" height="72" rx="4"/>
<text class="vx-text" x="486" y="192" text-anchor="middle">missed</text>
<text class="vx-text-muted" x="486" y="211" text-anchor="middle">with the estimate</text>
<text class="vx-text-muted" x="486" y="228" text-anchor="middle">that lost</text>
</g>
<g class="vx-seq" style="--vx-i: 2; --vx-n: 3">
<rect class="vx-box" x="606" y="168" width="138" height="72" rx="4"/>
<text class="vx-text" x="675" y="192" text-anchor="middle">passed</text>
<text class="vx-text-muted" x="675" y="211" text-anchor="middle">what was done,</text>
<text class="vx-text-muted" x="675" y="228" text-anchor="middle">with its numbers</text>
</g>
<text class="vx-text-accent" x="286" y="266" text-anchor="middle">legality:</text>
<text class="vx-text-accent" x="286" y="284" text-anchor="middle">same on every machine</text>
<text class="vx-text-accent" x="486" y="266" text-anchor="middle">profitability:</text>
<text class="vx-text-accent" x="486" y="284" text-anchor="middle">depends on the target</text>
</svg>
<figcaption>Figure 2. Every optimization decision asks two questions in order. Legality comes from the contract and gives the same answer on every machine; profitability comes from a cost model and depends on the target. Each exit produces a remark: a refusal on legality names the rule, a refusal on cost names the estimate, and a transformation reports what it did.</figcaption>
</figure>

The same loop can pass on one machine and fail on another. Take a small C file, `sum.c`, whose loop on line 3 adds `x[i]` into a `float` total on line 4. Compiled by Apple clang 21 at `-O2` for the M4 Pro, it is vectorized, rewritten to use vector instructions that work on four values at a time. Each group of four loaded values is still added into the total through an **ordered reduction**, an operation that adds the lanes of a vector into a running total one at a time, in their original order, so the result bits match the plain loop.[^langref] Whether that is faster than the plain loop is a question for measurement ([P1](p1-measure-first.md)).

Compiled by the same compiler for x86-64 (`--target=x86_64-apple-macos14`), the loop is refused, and Clang says why (both checked 2026-09-24, with `-Rpass=`, `-Rpass-missed=` and `-Rpass-analysis=loop-vectorize`):

```text
sum.c:4:15: remark: loop not vectorized: cannot prove it is safe to reorder floating-point operations; allow reordering by specifying '#pragma clang loop vectorize(enable)' before the loop or by providing the compiler option '-ffast-math' [-Rpass-analysis=loop-vectorize]
sum.c:3:5: remark: loop not vectorized [-Rpass-missed=loop-vectorize]
```

The refusal is about legality: the vectorizer would have to reorder the additions. The contract is the same on both machines, since neither may reorder them. What differs is what LLVM 18 offers each target. Its AArch64 description turns ordered reductions on, and x86-64 keeps the default, which turns them off, so for x86-64 every vector version of this loop would reorder the sum.[^tti][^ordered]

Now read the advice at the end of the first remark. Both suggestions change the contract. `-ffast-math` does so openly. The pragma looks like a request for speed, but in LLVM 18 a hint that enables vectorization or sets a vector width also permits the vectorizer to reorder floating-point operations. A hidden option, on by default, controls this, and the comment in the source explains that reordering changes how round-off error accumulates.[^lv-legality][^clang-diag] A request to go faster has quietly become permission to change the answer.

Vortex's philosophy rules that out: "Making the program faster must not quietly change its answer" ([principle 3](../philosophy.md#3-do-not-surprise-the-programmer)). If Vortex ever gains hints, its contract must keep two kinds of request apart: requests that choose among legal versions, and permissions that change what is legal. Only a permission may change bits, and only when the programmer grants it explicitly.

## Remarks: the optimizer's report

Vortex's philosophy asks for more than correct decisions: "If the compiler cannot apply an expected optimization, it should explain why" ([principle 6](../philosophy.md#6-explain-performance-decisions)). Production compilers do this with **optimization remarks**, messages that report a decision at a place in the source. Clang and LLVM sort them into three kinds:[^clang-um][^llvm-remarks]

- a **passed** remark reports a transformation that was made;
- a **missed** remark reports a transformation that was attempted and not made;
- an **analysis** remark reports something a pass worked out, often the reason behind a missed remark.

Remarks are off until requested, and they are selected by pass name. Clang has `-Rpass=`, `-Rpass-missed=` and `-Rpass-analysis=`, each taking a regular expression that matches pass names; LLVM's `opt` and `llc` have the same three as `-pass-remarks=`, `-pass-remarks-missed=` and `-pass-remarks-analysis=`.[^clang-um][^llvm-remarks] GCC's equivalent is `-fopt-info`, whose kinds are `optimized`, `missed` and `note`, narrowed to groups of passes such as `vec` or `loop`.[^gcc-dev]

For tools rather than people, the same remarks can be saved as an **optimization record**, a machine-readable file with one entry per remark:[^clang-um][^llvm-remarks][^gcc-dev]

| Compiler | Option | Default format |
| --- | --- | --- |
| Clang | `-fsave-optimization-record` | YAML, written to `<base>.opt.yaml`; LLVM bitstream on request |
| LLVM `opt` and `llc` | `-pass-remarks-output=<file>` | YAML; bitstream with `-pass-remarks-format` |
| GCC | `-fsave-optimization-record` | compressed JSON, marked experimental |

Each YAML record carries its kind, the pass, a short name that identifies the remark, the function, a source location when there is one, and a list of arguments, some of them numbers.[^llvm-remarks] Because every record has a name, tools can count and compare them. LLVM's `opt-viewer.py` renders records as an HTML page, `opt-stats.py` summarizes them, and `opt-diff.py` shows how the records changed between two compilers, or between two versions of the source.[^llvm-remarks]

The fourth example produces a record stream from LLVM 18's loop vectorizer for three small loops:

--8<-- "includes/examples/optimize/o1-optimizer-contract/remarks.ll.md"

<figure class="vx-figure">
<svg viewBox="0 0 760 320" role="img" aria-label="Six remark records in three lanes: passed, missed and analysis" aria-describedby="o1-f3-desc">
<title id="o1-f3-title">A remark stream in three lanes</title>
<desc id="o1-f3-desc">Three rows, one per loop from the remarks example, and three lanes, passed, missed and analysis. For scale, the missed lane holds two records: vectorizing not beneficial, and interleaving not beneficial. For sum, the missed lane holds loop not vectorized, and the analysis lane holds reordering float additions not proven safe. For sum_hinted, the analysis lane holds interleaving not beneficial, and the passed lane holds vectorized with 4 lanes. The records light up one after another in the order the compiler wrote them.</desc>
<text class="vx-text" x="16" y="28">Loop</text>
<text class="vx-text" x="275" y="28" text-anchor="middle">passed</text>
<text class="vx-text" x="470" y="28" text-anchor="middle">missed</text>
<text class="vx-text" x="663" y="28" text-anchor="middle">analysis</text>
<line class="vx-line" x1="176" y1="40" x2="176" y2="304"/>
<line class="vx-line" x1="372" y1="40" x2="372" y2="304"/>
<line class="vx-line" x1="568" y1="40" x2="568" y2="304"/>
<line class="vx-line" x1="16" y1="135" x2="756" y2="135"/>
<line class="vx-line" x1="16" y1="218" x2="756" y2="218"/>
<text class="vx-mono" x="16" y="84">@scale</text>
<text class="vx-text-muted" x="16" y="104">y[i] = 2 * x[i]</text>
<text class="vx-mono" x="16" y="168">@sum</text>
<text class="vx-text-muted" x="16" y="188">total += x[i]</text>
<text class="vx-mono" x="16" y="252">@sum_hinted</text>
<text class="vx-text-muted" x="16" y="272">the same loop,</text>
<text class="vx-text-muted" x="16" y="288">with a 4-lane hint</text>
<g class="vx-seq" style="--vx-i: 0; --vx-n: 6">
<rect class="vx-box-bad" x="382" y="62" width="176" height="26" rx="4"/>
<text class="vx-text-muted" x="470" y="80" text-anchor="middle">vectorize: not beneficial</text>
</g>
<g class="vx-seq" style="--vx-i: 1; --vx-n: 6">
<rect class="vx-box-bad" x="382" y="96" width="176" height="26" rx="4"/>
<text class="vx-text-muted" x="470" y="114" text-anchor="middle">interleave: not beneficial</text>
</g>
<g class="vx-seq" style="--vx-i: 2; --vx-n: 6">
<rect class="vx-box-strong" x="578" y="150" width="170" height="42" rx="4"/>
<text class="vx-text-muted" x="663" y="167" text-anchor="middle">reordering float additions:</text>
<text class="vx-text-muted" x="663" y="184" text-anchor="middle">not proven safe</text>
</g>
<g class="vx-seq" style="--vx-i: 3; --vx-n: 6">
<rect class="vx-box-bad" x="382" y="158" width="176" height="26" rx="4"/>
<text class="vx-text-muted" x="470" y="176" text-anchor="middle">loop not vectorized</text>
</g>
<g class="vx-seq" style="--vx-i: 4; --vx-n: 6">
<rect class="vx-box-strong" x="578" y="238" width="170" height="26" rx="4"/>
<text class="vx-text-muted" x="663" y="256" text-anchor="middle">interleave: not beneficial</text>
</g>
<g class="vx-seq" style="--vx-i: 5; --vx-n: 6">
<rect class="vx-box-accent" x="187" y="252" width="176" height="26" rx="4"/>
<text class="vx-text-muted" x="275" y="270" text-anchor="middle">vectorized: 4 lanes</text>
</g>
</svg>
<figcaption>Figure 3. The six records from the remarks example, in short form, sorted into lanes by kind and placed beside the loop they describe. A missed record says that nothing happened; the analysis record beside it says why. The refusal for <code>@sum</code> names a rule of the contract, and the refusals for <code>@scale</code> name the cost model.</figcaption>
</figure>

The three loops show the three exits of Figure 2. `@scale` is legal, but the file names no target, so opt falls back to LLVM's default description of a machine, whose registers are only 32 bits wide, too narrow for a vector of `float` values, and the cost model refuses.[^tti] Given an AArch64 target on the command line (`-mtriple=aarch64`), the same loop is vectorized four lanes wide and interleaved twice, working on two vectors per step (LLVM 18.1.8, checked 2026-09-24).

`@sum` is refused on legality, in an `AnalysisFPCommute` record, the YAML form of Clang's remark above: the default description, like x86-64's, leaves ordered reductions off. `@sum_hinted` differs from `@sum` only by a width hint, and it is vectorized with four separate partial sums: the hint has licensed exactly the reordering the plain loop was refused.

Two limits of these records matter for a language like Vortex. The Clang manual notes that remarks name functions by their mangled names, the encoded names that functions carry in object files, because remarks come from the back end, which knows nothing of the source language. It also notes that their locations are translated from debug information, the tables that map machine code back to source lines, and that the translation can lose detail or drop the location entirely.[^clang-um]

A remark from LLVM about a Vortex program talks about LLVM IR. Remarks that speak Vortex, with Vortex names and exact source spans, are far easier to produce in passes that run on Vortex's own representation, whose nodes can keep the source spans that [Conformance 1.7](../specification/conformance.md#17-source-locations) already requires of the tree.

## What a good remark says

The records above suggest what a remark needs, before anyone designs a format:

- **Where**: a source span precise enough to point at one operation, as the runtime error line does.
- **Who and what**: the pass, the transformation, and a stable name that tests can match.
- **The decision**: passed, missed or analysis.
- **Why**, naming which question decided it: a rule of the contract, such as "would reorder `f32` additions" with a link to decision 56, or a cost, such as "estimated slower".
- **The numbers** behind a cost decision, labelled as estimates. The philosophy forbids "presenting an unverified performance estimate as a measured result" ([compiler responsibilities](../philosophy.md#programmer-and-compiler-responsibilities)), and GCC's record already marks whether each execution count came from profile data or from an estimate.[^gcc-dev]
- **No effect on the program.** Asking for remarks must not change the executable, the same rule that [decision 48](../decisions/diagnostics.md#d48) sets for warnings.

A missed remark with a precise reason is the most useful message a performance-minded programmer can get. It says what to change, or that nothing should change, because the contract forbids it.

## Your turn: the stage 10 kernel under the contract

Here is the [stage 10](../compiler/guide/stage-10-matrix-multiplication.md#the-program-the-milestone-asks-for) kernel at a size where speed begins to matter:

```vortex
// items: valid
fn multiply(a: &[f32; 64, 64], b: &[f32; 64, 64], c: &mut [f32; 64, 64]) {
    for row in 0..64 {
        for column in 0..64 {
            let mut sum: f32 = 0.0;
            for k in 0..64 {
                sum += a[row, k] * b[k, column];
            }
            c[row, column] = sum;
        }
    }
}
```

Below are seven changes an optimizer might make. The first three are decided. Decide the other four with the rules of this chapter before you open the answer.

| # | Change | Allowed? |
| --- | --- | --- |
| 1 | Keep `sum` in a register for the whole `k` loop | Yes. No one can observe where `sum` lives. |
| 2 | Fuse `a[row, k] * b[k, column]` and the addition into one fused multiply-add | No. One rounding instead of two can change the bits (decision 56). |
| 3 | In a version that adds straight into `c[row, column]`, keep that element in a register during the `k` loop and store it once at the end | Yes. `c` is `&mut`, so neither `a` nor `b` reaches its storage (References 9.8), and nothing reads `c` before the loop ends. |
| 4 | Remove the bounds checks on `a[row, k]` and `b[k, column]` | ? |
| 5 | Split the `k` loop into four running sums, over `k` = 0, 4, 8, ..., over `k` = 1, 5, 9, ..., and so on, then add the four at the end | ? |
| 6 | Compute two columns at once, `column` and `column + 1`, each with its own `sum` added in increasing `k` | ? |
| 7 | Skip the multiply-add whenever `a[row, k]` is `0.0` | ? |

??? check "Rows 4 to 7: which changes are allowed?"

    - **4, yes.** Every index comes from a range with constant bounds, `0..64`, into a dimension of extent 64, so no check can fail, and Conformance 1.4 allows omitting a check exactly when that is proved.
    - **5, no.** Four partial sums add the products in another order, which can change the rounding: it is reassociation.
    - **6, yes.** Each `sum` still starts at `0.0` and adds its products one at a time in increasing `k`. Only the order across elements changes, and no one can observe it here, because no check in the loop can fail (row 4). If one could, the new order might report a different failure first. [P7](p7-loop-transformations.md) calls this transformation unroll-and-jam.
    - **7, no.** If `b[k, column]` is an infinity or NaN, `0.0 * b[k, column]` is NaN and makes `sum` NaN; skipping the step would leave a number in `c` where the original leaves `NaN`.

## For Vortex

!!! vortex "Exercise"

    **Build** the contract and the tools that enforce it, before your compiler has any optimization pass.

    1. **A contract page** in your compiler's repository, one rule per line, each tied to the rule it comes from:
        - what must be preserved (Conformance 1.3 to 1.6, decisions 14, 39 and 56);
        - your answers to the questions this chapter left open. Is a loop that never finishes observable? May an error message change when the operation behind it is rewritten ([I7](../decisions/implementation.md#i7))? Must a recursion that runs out of stack at one optimization level also run out at another (Conformance 1.5)?
        - if your back end is LLVM, a table of every flag and attribute you plan to emit, with the rule or analysis that justifies each; if it generates C, the C compiler options that keep the contract, such as `-ffp-contract=off`.
    2. **A contract test**: build every test program with optimization off and on (your compiler's option, even while "on" changes nothing), run both builds, and compare standard output byte for byte, the runtime error line and the exit status.
    3. **A remark stream**: an option that writes one record per decision to a file, with a source span, the pass, the kind, a stable remark name, a message and arguments, where every number that comes from a cost model is marked as an estimate. Give it one real producer: an analysis remark for each function that counts its runtime checks by kind, a number that [O8](o8-loops.md) will later compare with the checks it removes.

    **Not yet:** any pass that transforms the program, cost models, profile data, binary record formats, a viewer, and any option that relaxes decision 56.

    **Proof that it works:**

    - The contract test passes on your whole test suite, and it fails, naming the program and its first difference, when you plant a miscompilation behind a hidden debugging option, such as folding `x + 1 > x` to `true`.
    - A golden test: the remark file for the stage 10 program matches a checked-in copy byte for byte, and every span points at the right line and column (Conformance 1.7).
    - A build with remarks and a build without them produce byte-identical executables.

## Key ideas

!!! recap "Questions you can now answer"

    - **What must a Vortex optimization preserve?** What the program prints, its runtime error line and its exit status, on every input, with every floating-point operation rounded as written.
    - **Why may C fold `x + 1 > x` to `true` while Vortex may not?** C's overflow is undefined and allows any behavior; Vortex's is a runtime error, which is observable.
    - **What is refinement, and what does it become for Vortex?** The optimized program's behaviors must be a subset of the original's on every input; with no undefined behavior, the two must be equal.
    - **How can you know a rewrite is correct?** Try every input when there are few, prove it with its precondition, or let a solver search for a counterexample; tests alone can only find counterexamples.
    - **Why is `x + 0.0` to `x` wrong for floats while `x + (-0.0)` to `x` is right?** `-0.0 + 0.0` is `+0.0`, while adding `-0.0` returns every input unchanged.
    - **How does a front end tell LLVM what it may assume?** With flags and attributes such as `nsw`, the fast-math flags and `noalias`, each a promise the language must back.
    - **What separates a refusal on legality from a refusal on profitability?** The first comes from the contract and holds on every machine; the second comes from a cost model for one target.

## Where this comes back

!!! next "You will use this again in"

    - [O5. Constants and dead code](o5-constants-and-dead-code.md): *dead code that can still fail*, *folding floats exactly as the program rounds them*
    - [O6. Redundancy: CSE, GVN, PRE and LICM](o6-redundancy.md): *moving a check that can fail*
    - [O8. Loops: structure, induction variables and bounds checks](o8-loops.md): *preconditions*, *removing a check with a proof*
    - [O9. Memory: alias analysis and MemorySSA](o9-alias-analysis.md): *`noalias` from `&mut`*
    - [O10. Pass managers and pipelines](o10-pass-pipelines.md): *remarks per pass*, *legality and profitability*
    - [O11. Undefined behavior, poison and correct optimization](o11-undefined-behavior.md): *refinement*, *poison*, *`nsw`*
    - [O12. Testing an optimizer](o12-testing-optimizers.md): *differential testing*, *the contract test*
    - [P10. Vectorization](p10-vectorization.md): *ordered reductions*, *loop hints*
    - [P11. Floating point under optimization](p11-floating-point.md): *signed zero*, *reassociation*, *contraction*
    - [P15. Choosing parameters: models or search](p15-choosing-parameters.md): *cost model*, *estimate versus measurement*
    - [C7. Peephole optimization](../backend/c7-peephole.md): *rewrite rules with preconditions*
    - [M3. Passes and pattern rewriting](../mlir/m3-passes-and-rewriting.md): *rewrites that must keep meaning*

## Sources and further reading

For depth, read the first section of the Alive2 paper for refinement in the presence of undefined behavior, Leroy's article for what it means to prove a whole compiler correct, and the "Languages and Compilers" section of Goldberg's survey for floating point under optimization. [O11](o11-undefined-behavior.md) returns to poison and refinement in LLVM in detail.

[^asif]: cppreference.com, "The as-if rule", sections "Explanation" and "Notes". <https://en.cppreference.com/cpp/language/as_if>
[^compcert]: Xavier Leroy, "Formal Verification of a Realistic Compiler", *Communications of the ACM* 52(7), 2009, sections 1 and 2.1. <https://doi.org/10.1145/1538788.1538814> (author's copy: <https://xavierleroy.org/publi/compcert-CACM.pdf>)
[^alive2]: Nuno P. Lopes, Juneyoung Lee, Chung-Kil Hur, Zhengyang Liu and John Regehr, "Alive2: Bounded Translation Validation for LLVM", *Proceedings of the 42nd ACM SIGPLAN International Conference on Programming Language Design and Implementation (PLDI)*, 2021, abstract and section 1. <https://doi.org/10.1145/3453483.3454030> (free copy: <https://users.cs.utah.edu/~regehr/alive2-pldi21.pdf>)
[^alive]: Nuno P. Lopes, David Menendez, Santosh Nagarakatte and John Regehr, "Provably Correct Peephole Optimizations with Alive", *Proceedings of the 36th ACM SIGPLAN Conference on Programming Language Design and Implementation (PLDI)*, 2015, abstract, section 3 and Figure 8. <https://doi.org/10.1145/2737924.2737965> (free copy: <https://users.cs.utah.edu/~regehr/papers/pldi15.pdf>)
[^csmith]: Xuejun Yang, Yang Chen, Eric Eide and John Regehr, "Finding and Understanding Bugs in C Compilers", *Proceedings of the 32nd ACM SIGPLAN Conference on Programming Language Design and Implementation (PLDI)*, 2011, abstract and sections 2.1 and 3.1. <https://doi.org/10.1145/1993498.1993532> (free copy: <https://users.cs.utah.edu/~regehr/papers/pldi11-preprint.pdf>)
[^goldberg]: David Goldberg, "What Every Computer Scientist Should Know About Floating-Point Arithmetic", *ACM Computing Surveys* 23(1), 1991, section "Languages and Compilers", subsections "Ambiguity" and "Optimizers". <https://doi.org/10.1145/103162.103163> (reprint: <https://docs.oracle.com/cd/E19957-01/806-3568/ncg_goldberg.html>)
[^clang-um]: Clang Project, "Clang Compiler User's Manual", section "Options to Emit Optimization Reports" with its "Current limitations", and the entries for `-fsave-optimization-record` and `-ffp-contract`. <https://clang.llvm.org/docs/UsersManual.html#options-to-emit-optimization-reports>
[^langref]: LLVM Project, "LLVM Language Reference Manual", sections "'add' Instruction", "Poison Values", "Floating-Point Environment", "Floating-Point Semantics", "Behavior of Floating-Point NaN values", "Fast-Math Flags", "Parameter Attributes" (`noalias`), "Function Attributes" (`mustprogress`) and "'llvm.vector.reduce.fadd.*' Intrinsic". <https://llvm.org/docs/LangRef.html>
[^lv-legality]: LLVM Project, `LoopVectorizationLegality.h` and `LoopVectorizationLegality.cpp`, release/18.x branch: `LoopVectorizeHints::allowReordering`, the comment on its declaration, and the `hints-allow-reordering` option. <https://github.com/llvm/llvm-project/blob/release/18.x/llvm/lib/Transforms/Vectorize/LoopVectorizationLegality.cpp>
[^clang-diag]: Clang Project, `DiagnosticFrontendKinds.td`, release/18.x branch: the remark `remark_fe_backend_optimization_remark_analysis_fpcommute`. <https://github.com/llvm/llvm-project/blob/release/18.x/clang/include/clang/Basic/DiagnosticFrontendKinds.td>
[^llvm-remarks]: LLVM Project, "Remarks", sections "Introduction to the LLVM remark diagnostics", "Enabling optimization remarks", "YAML remarks" and "opt-viewer". <https://llvm.org/docs/Remarks.html>
[^tti]: LLVM Project, `TargetTransformInfoImpl.h`, release/18.x branch: the defaults `getRegisterBitWidth` and `enableOrderedReductions`, used when a target does not override them or no target is named. <https://github.com/llvm/llvm-project/blob/release/18.x/llvm/include/llvm/Analysis/TargetTransformInfoImpl.h>
[^ordered]: LLVM Project, `LoopVectorize.cpp`, release/18.x branch: where `AllowOrderedReductions` is taken from the target before the `CantReorderFPOps` remark; and `AArch64TargetTransformInfo.h`, where `enableOrderedReductions` returns true. <https://github.com/llvm/llvm-project/blob/release/18.x/llvm/lib/Transforms/Vectorize/LoopVectorize.cpp> and <https://github.com/llvm/llvm-project/blob/release/18.x/llvm/lib/Target/AArch64/AArch64TargetTransformInfo.h>
[^gcc-dev]: GCC Project, "Developer Options", entries `-fopt-info` and `-fsave-optimization-record`. <https://gcc.gnu.org/onlinedocs/gcc/Developer-Options.html>

# O5. Constants and dead code

<p class="page-intro">A compiler that knows the operands of an operation can compute its result before the program runs, and a compiler that knows a result will never be used can stop computing it. This chapter builds both cleanups, from folding one operation to sparse conditional constant propagation and aggressive dead-code elimination, and fits them to Vortex, where a constant must come out exactly as the program would compute it, and a value nobody reads can still carry a check that has to run.</p>

<p class="vx-meta" markdown="1">Level: Intermediate · Reading time: about 45 minutes · Builds on: [O3. SSA form: construction and destruction](o3-ssa.md), [O4. Dataflow analysis](o4-dataflow.md)</p>

???+ remember "Before you start, remember"

    ??? question "In the flat lattice of constants, what is the join of 2 and 3? Of 2 and ⊥?"

        ⊤, not a constant, because the two paths disagree. And 2, because ⊥ means that no value has arrived yet.

        Introduced in [O4. Dataflow analysis](o4-dataflow.md#constants-when-merging-loses-information).

    ??? question "Why could LLVM's sccp prove that `x` is 1 in `flip`, where a pessimistic analysis could not?"

        It is optimistic: every value starts at ⊥, the back edge's contribution included, so the loop header first sees only 1, and the body confirms it. The price is that only the finished fixed point is safe.

        Introduced in [O4. Dataflow analysis](o4-dataflow.md#optimism-where-to-start).

    ??? question "What does SSA form give a sparse analysis?"

        Each value has one definition, so a fact about a value holds wherever the value exists, and a change can travel from the definition straight to its uses instead of through every block in between.

        Introduced in [O4. Dataflow analysis](o4-dataflow.md#dense-and-sparse).

    ??? question "When is a block control dependent on a branch?"

        When one edge of the branch guarantees that the block runs and another edge may skip it. Frontiers in the reversed graph compute these dependences.

        Introduced in [O2. Control-flow graphs and dominance](o2-cfg-and-dominance.md#post-dominance).

    ??? question "`let ratio = total / count;` is never read. What must a Vortex optimizer keep?"

        Both tests of the division, for a zero divisor and for -2147483648 / -1, unless it proves both impossible. The quotient itself may go.

        Introduced in [O1. The optimizer's contract](o1-optimizer-contract.md#code-that-looks-dead-can-still-fail).

!!! goals "In this chapter"

    - Fold an operation on constants exactly as Vortex evaluates it at run time, and tell the optimizer's folding apart from the language's constant evaluation.
    - Run sparse conditional constant propagation by hand, and explain why marking edges executable finds constants that propagation along every edge misses.
    - Treat each runtime check as a branch, and say what folding it to "always passes" or "always fails" permits.
    - Delete dead code by marking from roots through data and control dependences, and choose the roots for Vortex.
    - Recognize when folding and dead-code elimination empty a benchmark, and keep its work observable.

## Arithmetic before the program runs

Here is a complete program:

```vortex
// program: valid
fn main() {
    let values = [10, 20, 30, 40];
    let size = 4;
    print(values[size - 1]);
}
```

It prints 40. A v0.1 compiler emits two runtime checks for its last line: one for the subtraction `size - 1`, which could overflow, and one for the index, which must be at least 0 and below the extent 4 ([Expressions 5.5](../specification/expressions.md#checked-integer-operations), [Arrays 7.6](../specification/arrays.md#76-indexing)). It has no choice. `size` is a name, and a name is never an integer constant expression, even when it names an immutable variable initialized with a literal ([Expressions 5.12](../specification/expressions.md#512-constant-expressions)), so neither check may happen during compilation.

An optimizer is free to look further. `size` holds 4 wherever it is used, so every use can become 4: that step is **constant propagation**, replacing a use of a value with the constant it is known to hold. Now `4 - 1` has constant operands, so the optimizer can compute 3 and confirm that it fits in `i32`: that step is **constant folding**, evaluating an operation whose operands are all known.

The index is the constant 3, the bounds comparison `3 < 4` folds to true, and the branch to the error report can never be taken, so the check can go. If the optimizer also knows what the array holds, a matter for [O7](o7-inlining-and-sroa.md) and [O9](o9-alias-analysis.md), the whole line becomes `print(40)`.

Each step fed the next. Folding made a constant, propagation carried it to a comparison, and folding the comparison settled a branch and left code that can never run. Wegman and Zadeck's paper on constant propagation, the source of most of this chapter, lists exactly these payoffs: an expression evaluated during compilation needs no evaluation at run time, which pays most inside loops, and a branch that always goes one way exposes code that can be deleted.[^wz91]

Change `size` to 5 and the program still compiles. The index is now 4, the check fails, and the program must write its runtime error line and exit with status 101, after any output that came before. The optimizer can work this out as well, and it must still accept the program: an implementation may omit a runtime check only when it proves that the check cannot fail, and must not reject a program because it can prove that a runtime check will fail, though it may warn ([Conformance 1.4](../specification/conformance.md#14-static-and-dynamic-rules), [decision 39](../decisions/diagnostics.md#d39)).

Written with literals, `print(values[5 - 1])` is a different program. `5 - 1` is an integer constant expression, so the index is checked during compilation, and the program is rejected with a constant-evaluation error.

The same arithmetic happens at two moments, with two different jobs:

| | Constant evaluation | Constant folding |
| --- | --- | --- |
| Who performs it | the front end, as a rule of the language | the optimizer, by choice |
| What it covers | integer constant expressions: literals, unary `-`, parentheses and `+ - * / %` | any operation whose operands the optimizer knows, through names, phis and inlined calls |
| When a check fails | a constant-evaluation error: the program is rejected | the same runtime error, at the same point, as without optimization |
| Effect on the program | decides which programs compile | none: output, error line and exit status stay the same |

Decision 39 ties compile-time checking to the form of the operands rather than to the compiler's cleverness, so that every implementation accepts the same programs. With that fixed, an optimizer may be as clever as it likes, because nothing it proves can change which programs compile or what they do. The roadmap plans named constants, `const` declarations, as the first language addition after v0.1, so that array dimensions can use names ([Roadmap](../roadmap.md#after-v01)); a rule that lets such names into constant expressions would change the left-hand column and leave folding as it is. The rest of this chapter is about the right-hand column.

## Folding by the program's rules

A **folder** is an interpreter for one operation. It must produce exactly what the operation produces when the program runs, or the program's output changes with the optimization level, which [O1's contract](o1-optimizer-contract.md#vortexs-list) forbids.

For integers, Vortex makes the folder's job small and exact. Every checked `i32`, `u32` and `usize` operation either produces its result in its type or fails, and the table of checked operations says exactly when it fails ([Expressions 5.5](../specification/expressions.md#checked-integer-operations)). So a folder returns one of two things: a value, or "fails" with the kind of failure. `2147483647 + 1` in `i32` fails with an overflow, and so does `-2147483648 / -1`, the one division whose quotient does not fit; `7 / 0` fails with a division by zero. The types are the target's: `usize` has 64 bits on every v0.1 target ([decision 42](../decisions/numbers.md#d42)), whatever machine the compiler itself runs on.

A C compiler folding the same `int` expressions may produce anything in these cases, because the C program has no required behavior there. A Vortex folder must never hand back a wrapped value, and a folder written in C++ must find the failure without committing it. C++ leaves signed overflow and division by zero undefined, as [I8](../decisions/implementation.md#i8) notes, so a folder that computes `2147483647 + 1` in `int32_t` to see whether it overflows has a bug of its own.

A fold to "fails" also says something about the control flow around the operation. The operation is guarded by a comparison and a branch to the runtime's error report, as [I8](../decisions/implementation.md#i8) suggests, so "fails" means that the branch always goes to the report and the code after the check never runs. [O4's transfer functions](o4-dataflow.md#transfer-functions-must-follow-vortexs-rules) gave such an operation the value ⊥ for the same reason: no value flows on. The section on checks below turns this into a rule about branches.

For floating point, [decision 56](../decisions/numbers.md#d56) settles the question: each `f32` or `f64` operation produces the IEEE 754 result rounded to nearest with ties to even, with no contraction, reordering, wider format or flushing of subnormal values, and that includes values computed during compilation. A folder written in C++ meets it by computing each folded operation in the matching C++ type, `float` or `double`, one operation per expression.

The last condition protects against the compiler that builds the folder. Clang, by default, fuses a multiplication and an addition within one expression into a **fused multiply-add**, one instruction that rounds once instead of twice,[^clang-um] so a folder that evaluated `a * b + c` for three constant operands in one C++ expression could produce a bit pattern the Vortex program never would. [O1](o1-optimizer-contract.md#floating-point-identities-that-are-false) met the same trap in a test program; building the compiler itself with `-ffp-contract=off` removes it.

A folder evaluates an operation whose operands are all constants. A rule that rewrites an operation with an unknown operand, such as `x * 0` to `0`, is an **algebraic identity**, and it must hold for every value of that operand. Some identities hold for integers and fail for floats: `x * 0` is 0 for every `i32`, because the exact product is 0 and always fits, but for `f32` it is NaN when `x` is infinite or NaN, and `-0.0` when `x` is negative. O1 collected the float cases, and they bind a simplifier exactly as they bound the rewrites there.

In LLVM, algebraic simplification lives in the `instcombine` pass, whose rewrites include turning a multiplication by a constant power of two into a shift;[^llvm-passes] a front end that computes the kernel's row offset as `row * 64` gets `row << 6` back.

??? check "Which of these rewrites may a Vortex compiler apply? For `i32`: `x * 1` to `x`, `0 / x` to `0`, `x % 1` to `0`. For `f32`: `x * 1.0` to `x`, `0.0 / x` to `0.0`."

    - `x * 1` to `x`: yes. The exact product is `x`, which always fits.
    - `0 / x` to `0`: no. When `x` is 0 the division fails with a division by zero, and the rewrite would lose the error. It becomes correct under the precondition `x != 0`.
    - `x % 1` to `0`: yes. `%` fails only when its divisor is zero ([Expressions 5.5](../specification/expressions.md#checked-integer-operations)), and every integer leaves remainder 0 when divided by 1.
    - `x * 1.0` to `x`: yes. Multiplying by 1.0 is exact, so every finite value, both zeros and both infinities come back unchanged; a NaN stays a NaN, and which NaN is not observable ([O1](o1-optimizer-contract.md#vortexs-list)).
    - `0.0 / x` to `0.0`: no. For negative `x` the result is `-0.0`, which `print` writes differently, and for `x` equal to 0.0 or NaN the result is NaN.

## Propagating constants through a function

Folding sees one operation. Propagation connects operations, following each value from its definition to its uses. [O4](o4-dataflow.md#constants-when-merging-loses-information) solved constant propagation as a dataflow problem, with a fact for every variable at every block, and found its weakness: where two paths meet with different constants, the join is ⊤.

Wegman and Zadeck's paper, published in 1991 after a preliminary version at POPL in 1985, orders four algorithms by strength, each finding at least the constants of the one before, and separates two independent improvements:[^wz91]

| | Every edge carries values | Only edges proven executable carry values |
| --- | --- | --- |
| **Dense**: a fact for every variable at every node | Simple Constant (SC), Kildall's algorithm | Conditional Constant (CC), after Wegbreit |
| **Sparse**: a fact for every SSA value, sent along def-use edges | Sparse Simple Constant (SSC), after Reif and Lewis | Sparse Conditional Constant (SCC), new in the paper |

Moving down a column changes the cost, not the answer. SSC finds exactly the constants SC finds, faster by a factor proportional to the number of variables, because it sends each change along the **SSA edges**, the links from each definition to its uses, instead of copying every variable's fact through every node. Moving right changes the answer: the conditional algorithms find more constants.[^wz91] The bottom-right cell is the algorithm that LLVM and MLIR call **sparse conditional constant propagation**, or **SCCP**.[^llvm-passes] [^mlir-passes]

Constants are not the only facts worth propagating. A **copy** `y = x` gives a value a second name, and **copy propagation** replaces each use of `y` with `x`, after which the copy has no uses and dead-code elimination removes it. In SSA form the replacement is always safe, because the definition of `x` dominates every use of `y`. [O3](o3-ssa.md#leaving-ssa-form) showed its price: it can stretch one version's lifetime over another's, and leaving SSA form must then take care. SCCP leaves copies behind as well: a phi with one executable predecessor is a copy, and the exercise on `settle` below produces one.

### What "conditional" adds

Take a helper that reads an array from either end, and a caller that always passes `false`:

```vortex
// items: valid
fn element(values: &[f32; 64], from_end: bool) -> f32 {
    let mut index = 0;
    if from_end {
        index = 63;
    }
    return values[index];
}

fn first(values: &[f32; 64]) -> f32 {
    return element(&values, false);
}
```

Once an inliner ([O7](o7-inlining-and-sroa.md)) has copied `element` into `first`, `from_end` is the constant `false`. Here is the copy in SSA form, with the bounds check as a branch to a block R that reports the error. The comparison is unsigned, so a negative index, which reads as a large unsigned number, fails it too, and one test covers both bounds:

```text
A:  index0 = 0
    if false goto B else C
B:  index1 = 63
    goto C
C:  index2 = phi(A: index0, B: index1)
    ok = index2 < 64
    if ok goto D else R
D:  x = values[index2]
    return x
R:  report the bounds error and exit
```

Propagation along every edge gives `index0` the constant 0 and `index1` the constant 63, so the phi joins 0 and 63 and gets ⊤. Then `ok` is ⊤, both edges of the check stay possible, and the check stays. The branch in A tests a constant, but SSC never looks at branch conditions: like Kildall's simple constants, it assumes nothing about which way a branch goes.[^wz91] A conditional algorithm lets no value cross an edge until some branch is known to take that edge, so values created in code that never runs cannot spoil a constant.[^wz91] Figure 1 steps through it.

<figure class="vx-figure">
<div class="vx-stepper">
<div class="vx-step">
<p><strong>Step 1. Visit A.</strong> Every value starts at ⊥ and every edge unmarked; the edge from the start into A is on the flow worklist. Visiting A gives <code>index0</code> the constant 0, and the branch tests the constant <code>false</code>, so only the edge A → C goes on the flow worklist. Nothing will ever put A → B there.</p>
<svg viewBox="0 0 760 310" role="img" aria-label="SCCP step 1: block A is visited. Its branch tests the constant false, so only the edge from A to C goes on the flow worklist. Facts: index0 is 0; index1, index2, ok and x have no value yet. Executable edges: start to A, A to C.">
<defs><marker id="o5-f1-h1" viewBox="0 0 10 10" refX="10" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker></defs>
<line class="vx-flow" x1="165" y1="6" x2="165" y2="40" marker-end="url(#o5-f1-h1)"/>
<text class="vx-text-muted" x="172" y="26">start</text>
<line class="vx-line" x1="290" y1="60" x2="380" y2="60" marker-end="url(#o5-f1-h1)"/>
<text class="vx-text-muted" x="318" y="52">true</text>
<line class="vx-flow" x1="165" y1="92" x2="165" y2="130" marker-end="url(#o5-f1-h1)"/>
<text class="vx-text-muted" x="172" y="116">false</text>
<line class="vx-line" x1="470" y1="92" x2="470" y2="130" marker-end="url(#o5-f1-h1)"/>
<line class="vx-line" x1="165" y1="202" x2="165" y2="240" marker-end="url(#o5-f1-h1)"/>
<text class="vx-text-muted" x="172" y="226">true</text>
<line class="vx-line" x1="470" y1="202" x2="470" y2="240" marker-end="url(#o5-f1-h1)"/>
<text class="vx-text-muted" x="477" y="226">false</text>
<rect class="vx-box-accent" x="40" y="40" width="250" height="52" rx="4"/>
<text class="vx-text" x="52" y="62">A</text>
<text class="vx-mono" x="80" y="62">index0 = 0</text>
<text class="vx-mono" x="80" y="82">if false goto B else C</text>
<rect class="vx-box" x="380" y="40" width="180" height="52" rx="4"/>
<text class="vx-text" x="392" y="62">B</text>
<text class="vx-mono" x="420" y="62">index1 = 63</text>
<text class="vx-mono" x="420" y="82">goto C</text>
<rect class="vx-box" x="40" y="130" width="520" height="72" rx="4"/>
<text class="vx-text" x="52" y="152">C</text>
<text class="vx-mono" x="80" y="152">index2 = phi(A: index0, B: index1)</text>
<text class="vx-mono" x="80" y="172">ok = index2 &lt; 64</text>
<text class="vx-mono" x="80" y="192">if ok goto D else R</text>
<rect class="vx-box" x="40" y="240" width="250" height="52" rx="4"/>
<text class="vx-text" x="52" y="262">D</text>
<text class="vx-mono" x="80" y="262">x = values[index2]</text>
<text class="vx-mono" x="80" y="282">return x</text>
<rect class="vx-box" x="380" y="240" width="180" height="52" rx="4"/>
<text class="vx-text" x="392" y="262">R</text>
<text class="vx-mono" x="420" y="262">report error</text>
<text class="vx-mono" x="420" y="282">exit 101</text>
<text class="vx-text" x="600" y="40">facts</text>
<text class="vx-mono" x="600" y="64">index0</text><text class="vx-text-accent" x="690" y="64">0</text>
<text class="vx-mono" x="600" y="86">index1</text><text class="vx-mono" x="690" y="86">⊥</text>
<text class="vx-mono" x="600" y="108">index2</text><text class="vx-mono" x="690" y="108">⊥</text>
<text class="vx-mono" x="600" y="130">ok</text><text class="vx-mono" x="690" y="130">⊥</text>
<text class="vx-mono" x="600" y="152">x</text><text class="vx-mono" x="690" y="152">⊥</text>
<text class="vx-text" x="600" y="196">executable</text>
<text class="vx-mono" x="600" y="220">start → A</text>
<text class="vx-text-accent" x="600" y="242">A → C</text>
</svg>
</div>
<div class="vx-step">
<p><strong>Step 2. Visit C.</strong> The edge A → C is marked, the first executable edge into C, so C's phi and its other instructions are evaluated. The phi joins only operands whose edges are executable, and only A → C is, so <code>index2</code> is 0, not ⊤. The comparison gives true, and the check's branch puts only C → D on the list.</p>
<svg viewBox="0 0 760 310" role="img" aria-label="SCCP step 2: block C is visited through the edge from A. The phi sees only the operand from A, so index2 is 0, and ok is true, so only the edge from C to D goes on the flow worklist. Facts: index0 is 0, index1 has no value, index2 is 0, ok is true, x has no value. Executable edges: start to A, A to C, C to D.">
<defs><marker id="o5-f1-h2" viewBox="0 0 10 10" refX="10" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker></defs>
<line class="vx-flow" x1="165" y1="6" x2="165" y2="40" marker-end="url(#o5-f1-h2)"/>
<text class="vx-text-muted" x="172" y="26">start</text>
<line class="vx-line" x1="290" y1="60" x2="380" y2="60" marker-end="url(#o5-f1-h2)"/>
<text class="vx-text-muted" x="318" y="52">true</text>
<line class="vx-flow" x1="165" y1="92" x2="165" y2="130" marker-end="url(#o5-f1-h2)"/>
<text class="vx-text-muted" x="172" y="116">false</text>
<line class="vx-line" x1="470" y1="92" x2="470" y2="130" marker-end="url(#o5-f1-h2)"/>
<line class="vx-flow" x1="165" y1="202" x2="165" y2="240" marker-end="url(#o5-f1-h2)"/>
<text class="vx-text-muted" x="172" y="226">true</text>
<line class="vx-line" x1="470" y1="202" x2="470" y2="240" marker-end="url(#o5-f1-h2)"/>
<text class="vx-text-muted" x="477" y="226">false</text>
<rect class="vx-box-strong" x="40" y="40" width="250" height="52" rx="4"/>
<text class="vx-text" x="52" y="62">A</text>
<text class="vx-mono" x="80" y="62">index0 = 0</text>
<text class="vx-mono" x="80" y="82">if false goto B else C</text>
<rect class="vx-box" x="380" y="40" width="180" height="52" rx="4"/>
<text class="vx-text" x="392" y="62">B</text>
<text class="vx-mono" x="420" y="62">index1 = 63</text>
<text class="vx-mono" x="420" y="82">goto C</text>
<rect class="vx-box-accent" x="40" y="130" width="520" height="72" rx="4"/>
<text class="vx-text" x="52" y="152">C</text>
<text class="vx-mono" x="80" y="152">index2 = phi(A: index0, B: index1)</text>
<text class="vx-mono" x="80" y="172">ok = index2 &lt; 64</text>
<text class="vx-mono" x="80" y="192">if ok goto D else R</text>
<rect class="vx-box" x="40" y="240" width="250" height="52" rx="4"/>
<text class="vx-text" x="52" y="262">D</text>
<text class="vx-mono" x="80" y="262">x = values[index2]</text>
<text class="vx-mono" x="80" y="282">return x</text>
<rect class="vx-box" x="380" y="240" width="180" height="52" rx="4"/>
<text class="vx-text" x="392" y="262">R</text>
<text class="vx-mono" x="420" y="262">report error</text>
<text class="vx-mono" x="420" y="282">exit 101</text>
<text class="vx-text" x="600" y="40">facts</text>
<text class="vx-mono" x="600" y="64">index0</text><text class="vx-mono" x="690" y="64">0</text>
<text class="vx-mono" x="600" y="86">index1</text><text class="vx-mono" x="690" y="86">⊥</text>
<text class="vx-mono" x="600" y="108">index2</text><text class="vx-text-accent" x="690" y="108">0</text>
<text class="vx-mono" x="600" y="130">ok</text><text class="vx-text-accent" x="690" y="130">true</text>
<text class="vx-mono" x="600" y="152">x</text><text class="vx-mono" x="690" y="152">⊥</text>
<text class="vx-text" x="600" y="196">executable</text>
<text class="vx-mono" x="600" y="220">start → A</text>
<text class="vx-mono" x="600" y="242">A → C</text>
<text class="vx-text-accent" x="600" y="264">C → D</text>
</svg>
</div>
<div class="vx-step">
<p><strong>Step 3. Visit D.</strong> The load gives <code>x</code> the fact ⊤, because the analysis does not track what memory holds, and D ends in a return, which marks no edge. Both worklists are now empty: this is the fixed point.</p>
<svg viewBox="0 0 760 310" role="img" aria-label="SCCP step 3: block D is visited. The load gives x the fact top, not a constant, and the return marks no edge. Both worklists are empty. Executable edges: start to A, A to C, C to D.">
<defs><marker id="o5-f1-h3" viewBox="0 0 10 10" refX="10" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker></defs>
<line class="vx-flow" x1="165" y1="6" x2="165" y2="40" marker-end="url(#o5-f1-h3)"/>
<text class="vx-text-muted" x="172" y="26">start</text>
<line class="vx-line" x1="290" y1="60" x2="380" y2="60" marker-end="url(#o5-f1-h3)"/>
<text class="vx-text-muted" x="318" y="52">true</text>
<line class="vx-flow" x1="165" y1="92" x2="165" y2="130" marker-end="url(#o5-f1-h3)"/>
<text class="vx-text-muted" x="172" y="116">false</text>
<line class="vx-line" x1="470" y1="92" x2="470" y2="130" marker-end="url(#o5-f1-h3)"/>
<line class="vx-flow" x1="165" y1="202" x2="165" y2="240" marker-end="url(#o5-f1-h3)"/>
<text class="vx-text-muted" x="172" y="226">true</text>
<line class="vx-line" x1="470" y1="202" x2="470" y2="240" marker-end="url(#o5-f1-h3)"/>
<text class="vx-text-muted" x="477" y="226">false</text>
<rect class="vx-box-strong" x="40" y="40" width="250" height="52" rx="4"/>
<text class="vx-text" x="52" y="62">A</text>
<text class="vx-mono" x="80" y="62">index0 = 0</text>
<text class="vx-mono" x="80" y="82">if false goto B else C</text>
<rect class="vx-box" x="380" y="40" width="180" height="52" rx="4"/>
<text class="vx-text" x="392" y="62">B</text>
<text class="vx-mono" x="420" y="62">index1 = 63</text>
<text class="vx-mono" x="420" y="82">goto C</text>
<rect class="vx-box-strong" x="40" y="130" width="520" height="72" rx="4"/>
<text class="vx-text" x="52" y="152">C</text>
<text class="vx-mono" x="80" y="152">index2 = phi(A: index0, B: index1)</text>
<text class="vx-mono" x="80" y="172">ok = index2 &lt; 64</text>
<text class="vx-mono" x="80" y="192">if ok goto D else R</text>
<rect class="vx-box-accent" x="40" y="240" width="250" height="52" rx="4"/>
<text class="vx-text" x="52" y="262">D</text>
<text class="vx-mono" x="80" y="262">x = values[index2]</text>
<text class="vx-mono" x="80" y="282">return x</text>
<rect class="vx-box" x="380" y="240" width="180" height="52" rx="4"/>
<text class="vx-text" x="392" y="262">R</text>
<text class="vx-mono" x="420" y="262">report error</text>
<text class="vx-mono" x="420" y="282">exit 101</text>
<text class="vx-text" x="600" y="40">facts</text>
<text class="vx-mono" x="600" y="64">index0</text><text class="vx-mono" x="690" y="64">0</text>
<text class="vx-mono" x="600" y="86">index1</text><text class="vx-mono" x="690" y="86">⊥</text>
<text class="vx-mono" x="600" y="108">index2</text><text class="vx-mono" x="690" y="108">0</text>
<text class="vx-mono" x="600" y="130">ok</text><text class="vx-mono" x="690" y="130">true</text>
<text class="vx-mono" x="600" y="152">x</text><text class="vx-text-accent" x="690" y="152">⊤</text>
<text class="vx-text" x="600" y="196">executable</text>
<text class="vx-mono" x="600" y="220">start → A</text>
<text class="vx-mono" x="600" y="242">A → C</text>
<text class="vx-mono" x="600" y="264">C → D</text>
<text class="vx-text-muted" x="600" y="296">worklists empty</text>
</svg>
</div>
<div class="vx-step">
<p><strong>Step 4. Read the result.</strong> No executable edge enters B or R, so both are unreachable, and the edges A → B, B → C and C → R never carried a value. <code>index2</code> is 0 wherever it is used and the check always passes, so B, R, the phi and the check can all be deleted. Propagation along every edge would have joined 0 and 63 at C, given <code>index2</code> the fact ⊤, and kept everything.</p>
<svg viewBox="0 0 760 310" role="img" aria-label="SCCP step 4, the result: blocks B and R and the edges A to B, B to C and C to R fade out, because no executable edge reaches them. Final facts: index0 is 0, index1 never gets a value, index2 is 0, ok is true, x is top. Propagating along every edge instead would give index2 the fact top and keep the check.">
<defs><marker id="o5-f1-h4" viewBox="0 0 10 10" refX="10" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker></defs>
<line class="vx-flow" x1="165" y1="6" x2="165" y2="40" marker-end="url(#o5-f1-h4)"/>
<text class="vx-text-muted" x="172" y="26">start</text>
<line class="vx-flow" x1="165" y1="92" x2="165" y2="130" marker-end="url(#o5-f1-h4)"/>
<text class="vx-text-muted" x="172" y="116">false</text>
<line class="vx-flow" x1="165" y1="202" x2="165" y2="240" marker-end="url(#o5-f1-h4)"/>
<text class="vx-text-muted" x="172" y="226">true</text>
<g opacity="0.35">
<line class="vx-line" x1="290" y1="60" x2="380" y2="60" marker-end="url(#o5-f1-h4)"/>
<text class="vx-text-muted" x="318" y="52">true</text>
<line class="vx-line" x1="470" y1="92" x2="470" y2="130" marker-end="url(#o5-f1-h4)"/>
<line class="vx-line" x1="470" y1="202" x2="470" y2="240" marker-end="url(#o5-f1-h4)"/>
<text class="vx-text-muted" x="477" y="226">false</text>
<rect class="vx-box-bad" x="380" y="40" width="180" height="52" rx="4"/>
<text class="vx-text" x="392" y="62">B</text>
<text class="vx-mono" x="420" y="62">index1 = 63</text>
<text class="vx-mono" x="420" y="82">goto C</text>
<rect class="vx-box-bad" x="380" y="240" width="180" height="52" rx="4"/>
<text class="vx-text" x="392" y="262">R</text>
<text class="vx-mono" x="420" y="262">report error</text>
<text class="vx-mono" x="420" y="282">exit 101</text>
</g>
<rect class="vx-box-strong" x="40" y="40" width="250" height="52" rx="4"/>
<text class="vx-text" x="52" y="62">A</text>
<text class="vx-mono" x="80" y="62">index0 = 0</text>
<text class="vx-mono" x="80" y="82">if false goto B else C</text>
<rect class="vx-box-strong" x="40" y="130" width="520" height="72" rx="4"/>
<text class="vx-text" x="52" y="152">C</text>
<text class="vx-mono" x="80" y="152">index2 = phi(A: index0, B: index1)</text>
<text class="vx-mono" x="80" y="172">ok = index2 &lt; 64</text>
<text class="vx-mono" x="80" y="192">if ok goto D else R</text>
<rect class="vx-box-strong" x="40" y="240" width="250" height="52" rx="4"/>
<text class="vx-text" x="52" y="262">D</text>
<text class="vx-mono" x="80" y="262">x = values[index2]</text>
<text class="vx-mono" x="80" y="282">return x</text>
<text class="vx-text" x="600" y="40">facts</text>
<text class="vx-mono" x="600" y="64">index0</text><text class="vx-mono" x="690" y="64">0</text>
<text class="vx-mono" x="600" y="86">index1</text><text class="vx-mono" x="690" y="86">⊥</text>
<text class="vx-mono" x="600" y="108">index2</text><text class="vx-text-accent" x="690" y="108">0</text>
<text class="vx-mono" x="600" y="130">ok</text><text class="vx-text-accent" x="690" y="130">true</text>
<text class="vx-mono" x="600" y="152">x</text><text class="vx-mono" x="690" y="152">⊤</text>
<text class="vx-text" x="600" y="196">unreachable</text>
<text class="vx-mono" x="600" y="220">B, R</text>
<text class="vx-text-muted" x="600" y="274">every edge instead:</text>
<text class="vx-text-muted" x="600" y="292">index2 = ⊤, check kept</text>
</svg>
</div>
</div>
<figcaption>Figure 1. SCCP on the lowered <code>first</code>, one visit at a time. Left: the control-flow graph; the block being visited has the accent outline, blocks already visited a heavier one, and edges found executable, whether already marked or still waiting on the flow worklist, are drawn as moving dashes. Right: the fact for each value, where ⊥ means no value yet and ⊤ means not a constant, and the edges found executable so far; whatever changed in a step is highlighted. In the last step the blocks and edges that never became executable fade out.</figcaption>
</figure>

### The algorithm

SCCP keeps a fact from the flat lattice for every SSA value, starting at ⊥, and an **executable flag** for every edge of the control-flow graph, starting false. It works from two worklists: control-flow edges newly found executable, and SSA edges whose definition's fact has changed since they were last examined.[^wz91] Wegman and Zadeck draw the lattice the other way up, with ⊤ for "no value yet" and ⊥ for "not a constant"; this book keeps O4's orientation.

1. Put the edge into the entry block on the flow worklist.
2. Take an edge from the flow worklist. If it is already marked, drop it. Otherwise mark it, evaluate every phi in its target block, and, if it is the first marked edge into that block, evaluate the block's other instructions.
3. Take an SSA edge from the SSA worklist, and evaluate its use again if the use's block has a marked incoming edge.
4. To evaluate an instruction, compute its fact from its operands' facts, with a phi joining only the operands whose edges are marked. If the fact changes, put the instruction's SSA edges on the SSA worklist. A branch puts on the flow worklist the one edge its constant condition selects, every edge if the condition is ⊤, and no edge while it is still ⊥; a jump puts its only edge.
5. Stop when both worklists are empty.

At the end, a block with no marked incoming edge is unreachable, every value with a constant fact can be replaced by the constant, and every branch with a constant condition can become a jump. Wegman and Zadeck prove both halves of that promise: no execution of the program takes an edge SCC left unmarked or gives a value a different constant, and SCC finds every constant the dense conditional algorithm finds.[^wz91]

A fact can change at most twice, since the lattice has height 2, so each SSA edge is examined at most twice, and each block is visited once per incoming edge: the work grows with the number of flow edges plus SSA edges, which Wegman and Zadeck expect to be linear in practice. The dense conditional algorithm keeps Kildall's worst case, O(E × V²) for E edges and V variables.[^wz91]

The first example runs one solver twice on the lowered `first`, once letting every edge carry values and once only the marked ones:

--8<-- "includes/examples/optimize/o5-constants-and-dead-code/sccp.cpp.md"

The two runs differ only in how a branch chooses its edges, and that alone decides whether `index2` is 0 or ⊤ and whether B and R survive.

### Edges, not blocks

The flags belong to edges, not blocks, and Wegman and Zadeck explain why: a block can be executable while one of its incoming edges is not, and a phi must ignore the operand from that edge.[^wz91] LLVM's solver follows the advice. It records feasible edges, its phi joins only the operands whose edges are feasible, and when two constant operands disagree it records the range of integers they span, a fact the flat lattice cannot express.[^llvm-sccp]

??? check "Block A is executable, and its branch tests the constant true. What does SCCP give `x2`? What would it give with one executable flag per block instead of per edge?"

    ```text
    A:  if true goto B else C
    B:  x1 = 5
        goto C
    C:  x2 = phi(A: 0, B: x1)
    ```

    With edge flags, only A → B and B → C are marked, so the phi joins only `x1`, and `x2` is 5. With block flags, A and B are both executable and both are predecessors of C, so the phi would join 0 and 5 and give ⊤. The edge A → C is critical: A has two successors and C has two predecessors. Wegman and Zadeck add that flags on blocks would work if every such edge first received a block of its own, which is what splitting critical edges does ([O2](o2-cfg-and-dominance.md#critical-edges)).

### Optimistic twice over

SCCP is optimistic in two ways at once: every value starts at ⊥, as in O4's `flip`, and every block starts unreachable. The second is what lets the conditional algorithms find more, and Wegman and Zadeck observe that the gap between optimistic and pessimistic starts, small for the unconditional algorithms, can be large once branches are evaluated.[^wz91] The price is the one O4 described: an optimistic analysis stopped early may hand out false facts.

LLVM's `sccp` pass implements this algorithm, with the integer ranges described above. It assumes values constant and blocks dead until proven otherwise, replaces the values it proves constant, turns the branches it proves one-way into jumps, and its documentation warns that it leaves definitions dead, so a dead-code pass should run after it.[^llvm-passes] The pass marks the entry block executable, treats every argument as overdefined, LLVM's name for ⊤, and deletes the blocks it never reached.[^llvm-sccp] The second example gives it the lowered `first`:

--8<-- "includes/examples/optimize/o5-constants-and-dead-code/check_fold.ll.md"

The branch on `false`, the phi, the check and the report are gone, and the load reads element 0. MLIR's `-sccp` pass describes itself as based on the same paper;[^mlir-passes] MLIR's passes are the subject of [M3](../mlir/m3-passes-and-rewriting.md).

## Checks are branches

A compiler that places each check inline, as I8 suggests, turns every check into an ordinary conditional branch: one edge continues, the other leads to the report. That visibility is one of I8's reasons for the choice: a check hidden inside a call to a safe operation in the runtime library is out of the optimizer's sight. SCCP needs no special rule for an inline check, and the three possible facts for the check's condition are the three things a Vortex optimizer may do with a check:

- **Always passes.** Only the continuing edge is marked. The comparison and the branch go, and the report block goes too once no other check leads to it. This is the one case [Conformance 1.4](../specification/conformance.md#14-static-and-dynamic-rules) allows: a check proved unable to fail.
- **Always fails.** Only the edge to the report is marked. Everything after the check on that path can go, but the report must run, with the same kind and position, after everything printed before it. The program still compiles, and the compiler may warn ([decision 48](../decisions/diagnostics.md#d48)).
- **⊤.** Both edges stay, and so does the check.

Wegman and Zadeck's own example of a run-time check removed by propagation is a type test in Lisp. They treat each value's type tag as a variable of its own, and once propagation proves that the operands of `plus` are integers, the test that chooses integer addition disappears.[^wz91] Vortex's checks fold the same way, with integer facts in place of type facts.

In the matrix multiplication kernel, at the 64 by 64 size that O1 to O4 use, every extent is the constant 64, written into the type `[f32; 64, 64]`, so every bounds check compares an index with a constant. The indices are loop variables, though, and in the flat lattice `k` is ⊤ at its loop header, since it takes 64 values: SCCP alone removes none of the kernel's checks.

The check on `k` repeats the test `k < 64` that controls the loop, and a branch condition is a fact about the edges it guards. O4's interval analysis, [refined by branch conditions](o4-dataflow.md#intervals-bounding-a-loop-index), proves that `k` lies in [0, 63] in the body, and [O8](o8-loops.md) turns such proofs into removed checks.

LLVM shows the same boundary. Given a loop whose body tests `k < 64` a second time before a load, LLVM 18's `sccp` keeps the second test, while `ipsccp`, its **interprocedural** variant (one that analyzes across function calls),[^llvm-passes] folds it (LLVM 18.1.8 on the owner's M4 Pro, checked on 2026-09-24).

The difference is a structure called PredicateInfo, which `ipsccp` builds and `sccp` does not: it places a copy of each value that a branch tests on the branch's edges and attaches the branch's condition to the copy, so that the uses below the edge see what the test proved.[^llvm-sccp] Wegman and Zadeck describe the same device for equality tests, an extra assignment on a branch's edge that records what the test established.[^wz91]

This chapter's share of the kernel is the step after the proof. Once a check's condition is known, folding its branch and deleting what only the failing edge needed is SCCP and dead-code work. Wegman and Zadeck point out that removing paths that are never taken simplifies control flow enough to help transform a program for vector or parallel processing,[^wz91] and that is the point of the exercise for the kernel: an inner loop with no checks left is a straight-line body that [P10](p10-vectorization.md) can vectorize.

## Unreachable code and dead code

Two different things go by the name dead code. **Unreachable code** never runs, because no executable path leads to it. **Useless code** runs, but nothing uses what it computes. Wegman and Zadeck call the matching techniques unreachable code elimination and unused code elimination, and point out that each finds dead code the other misses.[^wz91] Cytron and his coauthors use "dead" for the second kind, code with no effect on any output of the program, and argue for the broadest definition under which the code can still be removed safely.[^cytron] From here on, **dead code** means useless code.

### Unreachable code

A block that no path from the entry reaches is unreachable in the sense of [O2](o2-cfg-and-dominance.md#blocks-that-no-path-reaches). SCCP finds more, because it follows only the edges a branch can take. Decision 9 supplies an example in which the language itself requires a statement that never runs:

```vortex
// items: valid
fn root(target: i32) -> i32 {
    let mut guess = 0;
    while true {
        if guess * guess >= target {
            return guess;
        }
        guess += 1;
    }
    return guess;
}
```

The last `return` is there because the rule for terminating statements reads only the form of the statements, and under it a `while` loop never ends a block, not even `while true` ([decision 9](../decisions/statements.md#d9)). It never runs. The loop's test is the constant `true`, so SCCP never marks the edge out of the loop, and the final `return` is unreachable. The language keeps its rule syntactic so that every implementation accepts the same programs, and the optimizer removes what the rule made the programmer write.

Dropping that edge also removes the loop's own exit. `root` still leaves through the `return` inside the `if`, but a `while true` loop with no `return` or `break` inside is left with no path to the function's exit, the case [O2](o2-cfg-and-dominance.md#post-dominance) warned about. Dead-code elimination has to handle it, as the next sections show.

### Deleting by use counts

Once SCCP has replaced `index2` with 0 in `first`, the phi and the comparison have no uses left. The simplest cleanup counts uses: an instruction that defines a value nobody uses, and has no other effect, is deleted, and then its operands are examined again, since they may have lost their last use. LLVM's `dce` pass works this way.[^llvm-passes] Its limit is a cycle. A phi and the addition that feeds it along the back edge use each other, so each always has a use, even when nothing else ever reads either of them.

### Marking from the roots

Cytron and his coauthors turn the question around. Every statement starts out presumed dead, and a statement is marked **live** when it has an effect that the program's output depends on, when a live statement uses a value it defines, or when it is a conditional branch that a live statement is control dependent on. When the worklist empties, everything still unmarked is deleted.[^cytron]

The statements live by the first condition are the **roots**; their list names input and output statements, assignments to reference parameters, and calls to routines that may have side effects.[^cytron] Control dependences come from frontiers of the reversed graph, as in [O2](o2-cfg-and-dominance.md#post-dominance). The same authors suggest running this elimination late, and repeating it, rather than burdening every earlier step with dead code of its own.[^cytron]

The presumption of death is what removes cycles. A statement that depends only on itself, directly or through others, is never marked unless something live requires it, as Cytron and his coauthors point out.[^cytron] LLVM's `adce`, for **aggressive dead-code elimination**, is this algorithm; its documentation describes it as assuming values dead until proven otherwise, like SCCP applied to liveness.[^llvm-passes] The two passes are mirror images. SCCP presumes blocks dead until a branch reaches them; ADCE presumes instructions dead until a root needs them.

A conditional branch that nothing live depends on is removed by turning it into a jump. Cytron and his coauthors allow any of its former targets.[^cytron] LLVM's `adce` picks the successor closest to the end of the function, so that the new jump still leads toward the exit, and it adds a refinement for phis: a live phi depends on which edge control arrived by, so each predecessor of a block with a live phi counts as live when control dependences are marked.[^llvm-adce]

The third example runs both cleanups on a toy function with a dead cycle and a branch that controls only dead code:

--8<-- "includes/examples/optimize/o5-constants-and-dead-code/mark_live.cpp.md"

Use counts delete one instruction, `msg`. Marking also deletes the counter `t`, whose phi and addition only use each other, and the test `big`, whose branch decides nothing but whether `msg` runs; the branch becomes a jump to `done`.

### Loops that may not end

A loop raises a question that marking from roots does not answer by itself: whether the program ever gets past the loop. By default LLVM's `adce` keeps the branch on every loop's back edge, and with it the loop; a hidden option, off by default, lets it remove loops that may be infinite and have no other effect. It also keeps every branch in a part of the function from which no return can be reached, such as an infinite loop.[^llvm-adce]

Deleting a whole loop falls to a separate pass, `loop-deletion`, which removes loops whose **trip counts**, the number of times the body runs, are computable and finite, that have no side effects, and that do not feed the function's result.[^llvm-passes]

For Vortex the caution is required. [O1](o1-optimizer-contract.md#vortexs-list) counted the exit status as observable: a program that loops forever has none, and deleting the loop would give it status 0. Vortex has no rule that loops finish, so the branch of a `while` loop is a root unless a proof shows that the loop ends. A `for` loop is different. Its endpoints are evaluated once, its variable cannot be assigned, and its count is fixed when the loop starts ([decision 13](../decisions/statements.md#d13)), so a `for` loop whose body contains nothing live may go.

### Roots for Vortex

The roots decide what "dead" means, and they come from the contract, not from the algorithm. For Vortex they are:

- every call to `print`, the only way a v0.1 program writes output;
- every `return`, whose value the caller may use;
- every store through a `&mut` parameter, since the caller can read what the function wrote, and, until the memory analyses of [O9](o9-alias-analysis.md), every other store as well;
- every call to the runtime's error report, which keeps every check that might fail: the report is a root, the branch that guards it is live by control dependence, and the comparison that feeds the branch is live because the branch uses it;
- every call to another function, until your contract says otherwise: `fn spin() { spin(); }` must stop with a report of kind `stack` ([decision 46](../decisions/diagnostics.md#d46)), and a compiler that deleted the inner call as having no effect would let `spin` return;
- the branch of every `while` loop that is not proved to finish.

The fourth line is the rule from [O1](o1-optimizer-contract.md#code-that-looks-dead-can-still-fail) in algorithmic form: a result nobody reads is dead, but its check is alive. Here is what it does to a loop that counts its iterations and never reads the count:

```vortex
// items: valid
fn total(values: &[f32; 8]) -> f32 {
    let mut sum: f32 = 0.0;
    let mut count = 0;
    for i in 0..8 {
        sum += values[i];
        count += 1;
    }
    return sum;
}
```

<figure class="vx-figure">
<svg viewBox="0 0 760 350" role="img" aria-label="Marking live instructions in the counting loop, under C's rules and under Vortex's rules" aria-describedby="o5-f2-desc">
<title id="o5-f2-title">Dead until a root needs it</title>
<desc id="o5-f2-desc">Two panels. Left, C's rules: return s1 is a root, drawn with the accent outline. Moving dashed arrows run from return s1 to s1 = phi(0.0, s2), from the phi to s2 = s1 + v and back, and from s2 = s1 + v to the loop test if i1 &lt; 8, labelled control. Beside them, c2 = c1 + 1 and c1 = phi(0, c2) point at each other, but no arrow from a root reaches them: they are drawn faded, with dashed outlines, and a note says that no root needs the counter, so it is deleted. Right, Vortex's rules: the same chain for the sum, and a second root, report overflow. A control arrow runs from it to the branch if ovf, and moving arrows continue to c2, ovf = c1 + 1, then to c1 = phi(0, c2) and back, so every instruction of the counter is marked live.</desc>
<defs><marker id="o5-f2-head" viewBox="0 0 10 10" refX="10" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker></defs>
<line class="vx-line" x1="380" y1="10" x2="380" y2="310" stroke-dasharray="3 5"/>
<text class="vx-text" x="20" y="24">C's rules</text>
<text class="vx-text-muted" x="20" y="42">count += 1 is a plain addition</text>
<text class="vx-text" x="400" y="24">Vortex's rules</text>
<text class="vx-text-muted" x="400" y="42">count += 1 is checked</text>
<rect class="vx-box-accent" x="20" y="60" width="150" height="30" rx="4"/>
<text class="vx-mono" x="32" y="80">return s1</text>
<rect class="vx-box" x="20" y="130" width="150" height="30" rx="4"/>
<text class="vx-mono" x="32" y="150">s1 = phi(0.0, s2)</text>
<rect class="vx-box" x="20" y="200" width="150" height="30" rx="4"/>
<text class="vx-mono" x="32" y="220">s2 = s1 + v</text>
<rect class="vx-box" x="20" y="270" width="150" height="30" rx="4"/>
<text class="vx-mono" x="32" y="290">if i1 &lt; 8</text>
<line class="vx-flow" x1="95" y1="90" x2="95" y2="130" marker-end="url(#o5-f2-head)"/>
<line class="vx-flow" x1="70" y1="160" x2="70" y2="200" marker-end="url(#o5-f2-head)"/>
<line class="vx-flow" x1="120" y1="200" x2="120" y2="160" marker-end="url(#o5-f2-head)"/>
<line class="vx-flow" x1="95" y1="230" x2="95" y2="270" marker-end="url(#o5-f2-head)"/>
<text class="vx-text-muted" x="102" y="254">control</text>
<g opacity="0.45">
<rect class="vx-box-bad" x="200" y="200" width="150" height="30" rx="4"/>
<text class="vx-mono" x="212" y="220">c2 = c1 + 1</text>
<rect class="vx-box-bad" x="200" y="270" width="150" height="30" rx="4"/>
<text class="vx-mono" x="212" y="290">c1 = phi(0, c2)</text>
<line class="vx-line" x1="250" y1="230" x2="250" y2="270" marker-end="url(#o5-f2-head)"/>
<line class="vx-line" x1="300" y1="270" x2="300" y2="230" marker-end="url(#o5-f2-head)"/>
</g>
<text class="vx-text-muted" x="200" y="140">no root needs the</text>
<text class="vx-text-muted" x="200" y="158">counter: deleted</text>
<rect class="vx-box-accent" x="400" y="60" width="150" height="30" rx="4"/>
<text class="vx-mono" x="412" y="80">return s1</text>
<rect class="vx-box" x="400" y="130" width="150" height="30" rx="4"/>
<text class="vx-mono" x="412" y="150">s1 = phi(0.0, s2)</text>
<rect class="vx-box" x="400" y="200" width="150" height="30" rx="4"/>
<text class="vx-mono" x="412" y="220">s2 = s1 + v</text>
<rect class="vx-box" x="400" y="270" width="150" height="30" rx="4"/>
<text class="vx-mono" x="412" y="290">if i1 &lt; 8</text>
<line class="vx-flow" x1="475" y1="90" x2="475" y2="130" marker-end="url(#o5-f2-head)"/>
<line class="vx-flow" x1="450" y1="160" x2="450" y2="200" marker-end="url(#o5-f2-head)"/>
<line class="vx-flow" x1="500" y1="200" x2="500" y2="160" marker-end="url(#o5-f2-head)"/>
<line class="vx-flow" x1="475" y1="230" x2="475" y2="270" marker-end="url(#o5-f2-head)"/>
<text class="vx-text-muted" x="482" y="254">control</text>
<rect class="vx-box-accent" x="580" y="60" width="150" height="30" rx="4"/>
<text class="vx-mono" x="592" y="80">report overflow</text>
<rect class="vx-box" x="580" y="130" width="150" height="30" rx="4"/>
<text class="vx-mono" x="592" y="150">if ovf</text>
<rect class="vx-box" x="580" y="200" width="150" height="30" rx="4"/>
<text class="vx-mono" x="592" y="220">c2, ovf = c1 + 1</text>
<rect class="vx-box" x="580" y="270" width="150" height="30" rx="4"/>
<text class="vx-mono" x="592" y="290">c1 = phi(0, c2)</text>
<line class="vx-flow" x1="655" y1="90" x2="655" y2="130" marker-end="url(#o5-f2-head)"/>
<text class="vx-text-muted" x="662" y="114">control</text>
<line class="vx-flow" x1="655" y1="160" x2="655" y2="200" marker-end="url(#o5-f2-head)"/>
<line class="vx-flow" x1="630" y1="230" x2="630" y2="270" marker-end="url(#o5-f2-head)"/>
<line class="vx-flow" x1="680" y1="270" x2="680" y2="230" marker-end="url(#o5-f2-head)"/>
<text class="vx-text-muted" x="20" y="338">moving dashes: marks, from each live instruction to what it needs · dashed outline: never marked</text>
</svg>
<figcaption>Figure 2. Aggressive dead-code elimination on <code>total</code>, with s for <code>sum</code> and c for <code>count</code>; the loop's own counter <code>i</code>, the load of <code>values[i]</code> and its bounds check are left out. Arrows run from an instruction to what it needs, and the moving dashes are the marks spreading from the roots, the boxes with the accent outline. Left: nothing live needs the count, and its phi and addition, which need only each other, are never marked. Right: the addition is checked, the report is a root, the branch that guards the report is live by control dependence, and the marks reach the whole counter.</figcaption>
</figure>

The fourth example runs LLVM's `adce` on the same loop lowered both ways:

--8<-- "includes/examples/optimize/o5-constants-and-dead-code/dead_counter.ll.md"

Under C's rules the counter's phi and addition are gone; under Vortex's rules everything stays, down to the phi. Run the same file through `-passes=dce` instead and the counter survives under both rules, because its phi and its addition use each other (LLVM 18.1.8 on the owner's M4 Pro, checked on 2026-09-24). The counter can go only after a proof that `count += 1` never overflows. `count` rises by one per iteration, in step with `i`, so it never exceeds 8; that proof, about **induction variables**, values that change by a fixed step on every iteration, belongs to [O8](o8-loops.md), and once O8 removes the check, ADCE removes the counter.

??? check "Decision 48's example has the statement `width * 2;` after `let width = 128;`, and nothing reads the product. What may a Vortex optimizer do with it? What if `width` were an `i32` parameter?"

    With `width` equal to 128, SCCP folds the product to 256, which fits, so the overflow check always passes and goes; the product itself has no use, so the whole statement disappears. With a parameter, the product is still unused, but the multiplication might overflow, so its check stays: the report is a root, and the branch and the overflow test that lead to it are live. Only the product's value is dead. A proof that `width` lies between -1073741824 and 1073741823 would remove the rest.

## Your turn: a step that never changes

Here is a function whose `if` never runs, although no single line says so:

```vortex
// items: valid
fn settle(limit: i32) -> i32 {
    let mut step = 1;
    let mut total = 0;
    for i in 0..limit {
        if step != 1 {
            total += 100;
        }
        step = 2 - step;
        total += step;
    }
    return total;
}
```

In SSA form, with the `for` loop's own counter and test abbreviated and each checked operation's failure edge to the report left out of the listing:

```text
P:  step0 = 1
    total0 = 0
    goto H
H:  step1 = phi(P: step0, J: step2)
    total1 = phi(P: total0, J: total3)
    if another iteration goto T else X
T:  if step1 != 1 goto U else J
U:  total2 = total1 + 100             (checked)
    goto J
J:  total4 = phi(T: total1, U: total2)
    step2 = 2 - step1                 (checked)
    total3 = total4 + step2           (checked)
    goto H
X:  return total1
```

Run SCCP on it. Part of the answer is filled in:

| Value, edge or check | At the fixed point |
| --- | --- |
| `step1` | 1 |
| the condition `step1 != 1` | ? |
| the edge T → U | ? |
| `total4` | ? |
| `step2`, and the check on `2 - step1` | ? |
| `total3`, and the check on `total4 + step2` | ? |

Then say how many of the three checks remain, and why a pessimistic start would keep all of them.

??? check "Answers for `settle`"

    - The condition `step1 != 1` is false, since `step1` is 1.
    - The edge T → U is never marked, so U is unreachable, and its check on `total1 + 100` goes with it.
    - `total4` joins only the operand from T, `total1`, which is ⊤, because `total` grows on every trip. SCCP gives ⊤; the phi is left with one executable predecessor, and a cleanup such as LLVM's `simplifycfg`, which removes the phis of blocks with a single predecessor,[^llvm-passes] replaces it with `total1` once U is deleted.
    - `step2` is 2 − 1 = 1, and its check always passes, so it goes.
    - `total3` is ⊤, and the check on `total4 + step2` stays: SCCP knows nothing about `total` except that it is not a constant. The addition can in fact never overflow, since `total` ends up equal to the number of iterations, at most `limit`; proving that is [O8](o8-loops.md)'s work.
    - One check of three remains. A pessimistic start assumes ⊤ for the operand of `step1`'s phi on the back edge, so `step1` is ⊤, the condition is ⊤, U is reachable, and all three checks stay. This is O4's `flip` again, with a branch riding on it: optimism about values and optimism about edges pay off together.

## Dead code in benchmarks

A benchmark is a program written to be timed rather than read, and dead-code elimination is its natural enemy: a loop whose result is thrown away is dead, and an optimizer that deletes it leaves nothing to measure.

Google Benchmark, a C++ benchmarking library, provides `DoNotOptimize`, which forces the result of an expression into a register or into memory, and `ClobberMemory`, which forces pending writes to global memory. Its guide adds a warning that belongs to this chapter: `DoNotOptimize` does not stop the compiler from optimizing the expression itself, and an expression whose result is already known may be removed and replaced by the result.[^gbench] Dead-code elimination removes the work nobody uses; folding removes the work whose answer is already known. A benchmark has to defeat both.

In LLVM, `loop-deletion` removes a loop whose result is never used, when its trip count is computable and finite.[^llvm-passes] Written as LLVM IR, a loop that sums eight floats into a sum nobody reads loses its sum to `adce`, keeps an empty counting loop because `adce` keeps loop branches, and disappears entirely under `loop-deletion` (LLVM 18.1.8 on the owner's M4 Pro, checked on 2026-09-24). The effect on timing is worth measuring once, on your own machine, with the protocol of [P1](p1-measure-first.md). Write a C++ loop that sums an array, build it at `-O2` three ways, read the assembly of each, and time each:

| Build of the summing loop | Loop present in the assembly? | Time per call (ns) | Machine, compiler, date |
| --- | --- | --- | --- |
| result unused | | | |
| result passed to `DoNotOptimize` | | | |
| result printed | | | |

Vortex needs its own answer, because it has no such escape hatch. A v0.1 program reads no input: `main` takes no parameters, and `print` is the only built-in function ([Conformance 1.2](../specification/conformance.md#12-programs), [Programs and declarations 3.9](../specification/declarations.md#39-built-in-functions)). Every value a v0.1 benchmark computes is therefore fixed before it runs, and only what reaches `print`, the error line or the exit status is observable.

A Vortex benchmark must print something that depends on all of its work, such as a checksum of the result matrix; and since only the time would change, an optimizer able to run the whole computation during compilation would still be within its rights. Stage 10's program [compares its result](../compiler/guide/stage-10-matrix-multiplication.md#the-program-the-milestone-asks-for) with known values and prints `ok`, which protects the multiplication from dead-code elimination but not from folding. How to give a Vortex benchmark data the compiler cannot see is a question for [P1](p1-measure-first.md).

## For Vortex

!!! vortex "Exercise"

    **Build** constant folding, sparse conditional constant propagation and dead-code elimination over your compiler's SSA form ([O3](o3-ssa.md#for-vortex)), using the control-flow and dataflow toolkits from [O2](o2-cfg-and-dominance.md#for-vortex) and [O4](o4-dataflow.md#for-vortex).

    1. **A folder.** For each operation of your IR, a function that takes constant operands and returns what the program would compute at run time, or "fails" with the check's kind. Integers exactly as [Expressions 5.5](../specification/expressions.md#checked-integer-operations) says, with no C++ overflow inside the folder itself; `f32` and `f64` as one IEEE operation per call in the matching C++ type, with the compiler built with `-ffp-contract=off` ([decision 56](../decisions/numbers.md#d56)). No algebraic identities yet, except those you have proved for every input as in [O1](o1-optimizer-contract.md#three-ways-to-know-a-rewrite-is-correct).
    2. **SCCP**, with executable flags on edges, a flow worklist and an SSA worklist, the flat lattice, and every check treated as a branch. A check that always passes disappears; a check that always fails keeps its report call and loses what follows it on that path; neither case changes whether the program compiles ([decision 39](../decisions/diagnostics.md#d39)).
    3. **Dead-code elimination by marking**, from this chapter's roots: `print`, `return`, every store, the runtime's report calls, every call until your contract says otherwise, and the branch of every `while` loop not proved to finish. Mark through operands and through control dependences from your post-dominator tree, turn dead branches into jumps toward the exit, and remove unreachable blocks.
    4. **Remarks** in the stream from [O1](o1-optimizer-contract.md#for-vortex): a passed remark for each check removed, with its kind, its position and the constant that decided it; an analysis remark, or a warning, for each check proved to fail whenever it runs; and a summary per function of checks folded by kind, instructions deleted and blocks removed. A build with remarks must produce the same executable as a build without them.

    **Not yet:** removing checks with ranges or induction variables ([O8](o8-loops.md)); dead stores, and loads that read a value stored earlier ([O9](o9-alias-analysis.md)); inlining ([O7](o7-inlining-and-sroa.md)), so write the inlined forms of this chapter's examples by hand; value numbering ([O6](o6-redundancy.md)); propagation across calls; any algebraic identity you have not proved. Never turn a runtime failure found by folding into a compile-time error.

    **Proof that it works:**

    - [O1's contract test](o1-optimizer-contract.md#for-vortex), optimization off against optimization on, passes on your whole test suite, and running the new passes a second time changes nothing.
    - Golden tests. This chapter's first program prints 40 with no check left in its IR. With `size` set to 5 and `print("before");` added as the first statement, it compiles, prints `before`, writes the bounds error line and exits with status 101. `print(values[5 - 1])` is still a constant-evaluation error. The hand-inlined `first` has no check and one remark. `root` loses its final `return`. `settle` keeps one check of three. `total` keeps its counter and its check. `fn spin() { spin(); }` still ends with `runtime error[stack]`.
    - A folding test without golden files: for every integer operation and type, operands drawn from 0, 1, -1, 2 and the type's extremes, the program built with folding prints exactly what the program built without it prints, including which cases fail and with which kind; for `f32` and `f64`, cases whose results are subnormal, `-0.0`, infinite or NaN. Run it with your compiler built with `-fsanitize=undefined` as well, so that a folder that overflows in C++ is reported instead of passing by luck.
    - A table for three programs, filled in from your compiler's output, with the date and your compiler's version:

    | Program | Instructions before | Instructions after | Checks before, by kind | Checks after, by kind | Blocks removed | SCCP visits |
    | --- | --- | --- | --- | --- | --- | --- |
    | this chapter's first program | | | | | | |
    | `settle` | | | | | | |
    | stage 10 `multiply` | | | | | | |

## Key ideas

!!! recap "Questions you can now answer"

    - **What separates constant folding from Vortex's constant evaluation?** Constant evaluation is a language rule for integer constant expressions that decides which programs compile; folding is an invisible optimization that must never change what a program does or whether it compiles.
    - **How must a Vortex folder compute?** Exactly as the program would at run time: a checked integer operation gives its result or "fails", and each float operation is one IEEE operation in its own format, never contracted.
    - **Why does SCCP find more constants than propagation along every edge?** It evaluates branch conditions and lets values cross only edges that some branch can take, so values from code that never runs cannot spoil a constant.
    - **Why are the executable flags on edges?** A reachable block can have an incoming edge that is never taken, and its phi must ignore that edge's operand.
    - **What does SCCP do with a Vortex check?** It treats it as a branch: always passing removes it, always failing keeps the report and removes what follows, and ⊤ keeps it.
    - **Why does marking from roots delete more than counting uses?** Values that only feed each other, such as a counter nobody reads, always have uses but are never reached from a root.
    - **What keeps an unused Vortex value alive?** Its check: the report call is a root, so the branch, the comparison and the operands behind it stay until a proof removes the check.

## Where this comes back

!!! next "You will use this again in"

    - [O6. Redundancy: CSE, GVN, PRE and LICM](o6-redundancy.md): *folding during value numbering*, *optimism, as in SCCP*
    - [O7. Calls and aggregates: inlining and SROA](o7-inlining-and-sroa.md): *constant arguments after inlining*
    - [O8. Loops: structure, induction variables and bounds checks](o8-loops.md): *checks as branches*, *the counter's overflow check*, *loop deletion*
    - [O9. Memory: alias analysis and MemorySSA](o9-alias-analysis.md): *stores as roots*, *dead stores*
    - [O10. Pass managers and pipelines](o10-pass-pipelines.md): *dead-code elimination after SCCP*, *cleanups repeated late*
    - [O12. Testing an optimizer](o12-testing-optimizers.md): *folding checked against execution*, *running a pass twice*
    - [P1. Measure first](p1-measure-first.md): *benchmarks emptied by dead-code elimination*, *DoNotOptimize*
    - [P10. Vectorization](p10-vectorization.md): *a loop body with no checks left*
    - [C7. Peephole optimization](../backend/c7-peephole.md): *folding one instruction at a time*
    - [M3. Passes and pattern rewriting](../mlir/m3-passes-and-rewriting.md): *MLIR's sccp*

## Sources and further reading

Read Wegman and Zadeck first: sections 3 and 5 are short and exact, and figure 5, which relates the four algorithms, is a map of the whole subject. Then read section 7.1 of Cytron and his coauthors for dead-code elimination by marking, and LLVM's `ADCE.cpp`, which is short and shows what a production version adds: loops, parts of a function with no exit, and the choice of a target for a dead branch. Google Benchmark's guide is the practical note on keeping a benchmark's work alive.

[^wz91]: Mark N. Wegman and F. Kenneth Zadeck, "Constant Propagation with Conditional Branches", *ACM Transactions on Programming Languages and Systems* 13(2), 1991, pages 181 to 210: the abstract, the note on its first page, sections 1, 1.1, 2.2 (with figure 4), 3 (with figure 5), 3.1 to 3.4 (with footnote 4), 3.4.1, 4, 5.1, 5.3 and 6 (figure 14). <https://doi.org/10.1145/103135.103136>
[^cytron]: Ron Cytron, Jeanne Ferrante, Barry K. Rosen, Mark N. Wegman and F. Kenneth Zadeck, "Efficiently Computing Static Single Assignment Form and the Control Dependence Graph", *ACM Transactions on Programming Languages and Systems* 13(4), 1991: section 7.1, with figure 17 and footnote 11. <https://doi.org/10.1145/115372.115320> (free copy: <https://www.cs.utexas.edu/~pingali/CS380C/2010/papers/ssaCytron.pdf>)
[^llvm-passes]: LLVM Project, "LLVM's Analysis and Transform Passes", entries `adce`, `dce`, `instcombine`, `ipsccp`, `loop-deletion`, `sccp` and `simplifycfg`, read on 2026-09-24. <https://llvm.org/docs/Passes.html>
[^llvm-sccp]: LLVM Project, release/18.x branch: `SCCP.cpp` in `Transforms/Scalar`, the function `runSCCP` <https://github.com/llvm/llvm-project/blob/release/18.x/llvm/lib/Transforms/Scalar/SCCP.cpp>; `SCCPSolver.cpp`, the set `KnownFeasibleEdges` and `visitPHINode` <https://github.com/llvm/llvm-project/blob/release/18.x/llvm/lib/Transforms/Utils/SCCPSolver.cpp>; `SCCP.cpp` in `Transforms/IPO`, where the interprocedural pass calls `addPredicateInfo` for each function <https://github.com/llvm/llvm-project/blob/release/18.x/llvm/lib/Transforms/IPO/SCCP.cpp>; and the file comment of `PredicateInfo.h`, read on 2026-09-24 <https://llvm.org/doxygen/PredicateInfo_8h_source.html>
[^llvm-adce]: LLVM Project, `ADCE.cpp`, release/18.x branch: the file header, the options `adce-remove-control-flow` and `adce-remove-loops` with their comments, `initialize`, `isAlwaysLive`, `markPhiLive` and `updateDeadRegions`. <https://github.com/llvm/llvm-project/blob/release/18.x/llvm/lib/Transforms/Scalar/ADCE.cpp>
[^mlir-passes]: MLIR Project, "Passes", entry `-sccp`, read on 2026-09-24. <https://mlir.llvm.org/docs/Passes/#-sccp>
[^clang-um]: Clang Project, "Clang Compiler User's Manual", entry `-ffp-contract`, read on 2026-09-24. <https://clang.llvm.org/docs/UsersManual.html#cmdoption-ffp-contract>
[^gbench]: google/benchmark, "User Guide", section "Preventing Optimization", read on 2026-09-24. <https://github.com/google/benchmark/blob/main/docs/user_guide.md#preventing-optimization>

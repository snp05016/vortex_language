# O8. Loops: structure, induction variables and bounds checks

<p class="page-intro">A numerical kernel spends almost all of its time in loops, and a v0.1 Vortex compiler fills the innermost one with checks. This chapter shows how a compiler gives a loop a shape it can rely on, describes the loop's counters and addresses with closed formulas, counts its iterations, and uses those facts to remove bounds and overflow checks without changing anything a Vortex program can observe.</p>

<p class="vx-meta" markdown="1">Level: Intermediate · Reading time: about 55 minutes · Builds on: [O2. Control-flow graphs and dominance](o2-cfg-and-dominance.md), [O3. SSA form: construction and destruction](o3-ssa.md), [Build v0.1, stage 9](../compiler/guide/stage-9-runtime-safety.md)</p>

???+ remember "Before you start, remember"

    ??? question "When is an edge a back edge, and which blocks form its natural loop?"

        When its target dominates its source. The target is the loop's header, and the loop is the header together with every block that can reach the edge's source without passing through the header.

        Introduced in [O2. Control-flow graphs and dominance](o2-cfg-and-dominance.md#loops-from-dominance).

    ??? question "What is a loop's preheader, and why is it a good place for work moved out of the loop?"

        The single block outside the loop that enters it, when there is only one, and whose only edge goes to the header. It dominates the whole loop, so a value it computes is available in every iteration.

        Introduced in [O2. Control-flow graphs and dominance](o2-cfg-and-dominance.md#loops-from-dominance).

    ??? question "In the kernel's SSA form, what defines `k` at the header of its loop?"

        A phi, `k1 = phi(P: k0, S: k2)`: `k0 = 0` arrives from the block before the loop, and `k2 = k1 + 1` arrives along the back edge from the step block S.

        Introduced in [O3. SSA form: construction and destruction](o3-ssa.md#your-turn-the-kernel-in-ssa-form).

    ??? question "What interval did interval analysis find for `k` at the kernel's array reads, and what made the iteration stop?"

        [0, 63]. The branch `k < 64` cuts the interval on its true edge, and widening at the loop header makes the iteration stop after a few rounds instead of one round per value.

        Introduced in [O4. Dataflow analysis](o4-dataflow.md#intervals-bounding-a-loop-index).

    ??? question "When may an optimizer remove a runtime check?"

        Only after proving that the operation it guards is safe. A compiler that keeps every check is still correct.

        Introduced in [Build v0.1, stage 9](../compiler/guide/stage-9-runtime-safety.md#when-a-check-can-be-skipped).

!!! goals "In this chapter"

    - Put a loop into LLVM's canonical forms (a preheader, one back edge, dedicated exits, loop-closed SSA form and rotation) and explain what each one lets a later pass assume.
    - Describe induction variables as add recurrences, and fold the addresses a loop computes into the same form.
    - Compute a loop's backedge-taken count and trip count, and explain why compilers count back edges.
    - Remove a bounds or overflow check with a proof, and choose between folding it, versioning the loop and splitting its iterations when no proof exists, without moving a failure.
    - Read LLVM's scalar-evolution output and predict what IndVarSimplify, IRCE and the loop vectorizer will do with a checked loop.

## The checks in the kernel's inner loop

Here is the kernel from [stage 10](../compiler/guide/stage-10-matrix-multiplication.md#the-program-the-milestone-asks-for), at the size the earlier chapters use:

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

[Stage 9](../compiler/guide/stage-9-runtime-safety.md#array-bounds) asks for a check before every index that has not been proven safe, and [I8](../decisions/implementation.md#i8) suggests its form: a comparison and a branch before the read, with every failure going to one reporting function. [O2](o2-cfg-and-dominance.md#your-turn-the-kernels-inner-loop) drew the result for the `k` loop: a header H that tests `k < 64`, a block K1 that checks both indices of `a[row, k]`, a block K2 that checks both indices of `b[k, column]`, the arithmetic in K3, the step in S, and a block R that reports a failure and exits.

That is four index checks per iteration. The body of the `k` loop runs 64 × 64 × 64 = 262,144 times per call, so one call runs 1,048,576 index checks there, and 8,192 more for the 4,096 stores to `c`. None of them can fail. Every index is the variable of a `for` loop over `0..64`, and every extent is 64.

An index check has two halves, because a negative index is out of bounds too ([Arrays 7.6](../specification/arrays.md#76-indexing)). For an `i32` index and an extent below $2^{31}$, one comparison covers both: read as an unsigned number, a negative `i32` is at least $2^{31}$, far beyond the extent. The LLVM examples in this chapter check that way.

Stage 9 allows removing a check on one condition: a proof that the operation is safe. For `a[row, k]` inside the `k` loop, the proof needs three facts. It needs to know which values `k` takes (it starts at 0 and grows by 1), how many iterations run (64), and where the check sits relative to the loop's own test (after it, on every path). The next sections build those facts in turn: the loop's shape, its induction variables, and its iteration count. The rest of the chapter uses them, first in LLVM, then in Vortex.

## The shape of a loop

[O2](o2-cfg-and-dominance.md#loops-from-dominance) defined the parts of a natural loop: its header, its latches (the sources of its back edges), its exiting blocks and exit blocks, and, when there is exactly one entering block whose only edge goes to the header, its preheader. Those definitions describe every loop. A loop pass wants more: a fixed shape it can rely on without testing for it first.

LLVM calls that shape **loop simplify form**. A loop in this form has a preheader, a single back edge, and **dedicated exits**: exit blocks whose predecessors all lie inside the loop, so that the header dominates every one of them.[^llvm-loops] The LoopSimplify pass creates whatever is missing, and the pass managers add it automatically before any loop pass.[^llvm-loops] Each part gives a later pass a place to put code:

- The preheader provides a single entry edge from outside the loop, and that edge is not critical.[^llvm-passes] Code placed there runs once, before the first iteration, which is where loop-invariant code motion hoists work ([O6](o6-redundancy.md)).
- A single back edge means a single latch, so every header phi has exactly two operands: one from the preheader and one from the latch. A `while` loop with a `continue` has two latches ([O2](o2-cfg-and-dominance.md#loops-from-dominance)); LoopSimplify merges them.
- A dedicated exit runs only when control leaves the loop, so code that must run once after the loop, such as a store sunk out of it, has somewhere to go.[^llvm-passes]

A second form concerns values. A program is in **loop-closed SSA form**, LCSSA for short, when every value defined inside a loop is used only inside that loop. A value needed after the loop reaches its users through a phi with a single operand, placed in an exit block.[^llvm-loops] As a computation such a phi does nothing, and InstCombine deletes it once the loop passes are done.[^llvm-loops] It pays for itself while they run: a pass that copies or rewrites a loop finds every use outside it in the exit blocks, and updates only those phis.[^llvm-loops]

The third form concerns the loop's test. [Stage 7](../compiler/guide/stage-7-functions-and-control-flow.md#for-loops-over-integer-ranges) tests a `for` loop at the top: the header decides whether there is another value, and the body follows. LLVM's LoopRotate pass moves the test to the bottom, turning the loop into a do-while loop.[^llvm-loops] A do-while loop always runs its body once, so the change is correct only when the body is certain to run at least once; otherwise LoopRotate puts a copy of the test in front of the loop as a **guard**.[^llvm-loops] The guarded form is what makes hoisting safe. Work moved into the rotated loop's preheader runs only when the original body would have run, so a load that might fail is never executed for a loop that runs zero times.[^llvm-loops]

The first example shows all three forms at once. Its loop is entered from two places, so it has no preheader; it is tested at the top; and it uses `sum` after the loop:

--8<-- "includes/examples/optimize/o8-loops/loop_forms.ll.md"

Read the output from the top. LoopSimplify added `header.preheader`, whose phi merges the two starting values of `i`. Rotation then copied the test into that block as the guard `%more1`, gave the rotated loop its own preheader, `latch.lr.ph`, and folded header and latch into one block, `latch`, whose test now sits at its end. The loop's only exit, `header.exit_crit_edge`, is dedicated, and it carries the loop-closing phi `%split`, which `exit` merges with the 0 that the zero-trip path brings.

For the kernel, rotation costs nothing. A `for` loop evaluates its endpoints once, before the first iteration ([decision 13](../decisions/statements.md#d13)), so the guard of `for k in 0..64` is the test `0 < 64`. It is always true and folds away. Figure 1 steps through the `k` loop from the v0.1 lowering to the shape it has at the end of this chapter.

<figure class="vx-figure">
<div class="vx-stepper">
<div class="vx-step">
<p><strong>Step 1. As a v0.1 compiler lowers it.</strong> The test is at the top, in H. Each iteration runs two check blocks, K1 and K2, with two index checks each, and either can leave the loop for R. The loop has one latch, S, and three exiting blocks: H, K1 and K2.</p>
<svg viewBox="0 0 760 330" role="img" aria-label="Step 1: the k loop as a v0.1 compiler lowers it. P sets sum to 0.0 and k to 0 and leads to H, the header, which tests k less than 64. Its yes edge leads to K1, which checks the indices of a[row, k], then to K2, which checks the indices of b[k, column], then to K3, which adds the product to sum, then to S, which adds 1 to k and goes back to H. H's no edge leads to X, which stores sum into c[row, column]. K1 and K2 each have a failing edge to R, which reports the error and exits with status 101.">
<defs><marker id="o8-f1a-head" viewBox="0 0 10 10" refX="10" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker></defs>
<rect class="vx-box" x="60" y="16" width="320" height="30" rx="4"/>
<text class="vx-text" x="72" y="36">P</text>
<text class="vx-mono" x="104" y="36">sum = 0.0; k = 0</text>
<rect class="vx-box-strong" x="60" y="66" width="320" height="30" rx="4"/>
<text class="vx-text" x="72" y="86">H</text>
<text class="vx-mono" x="104" y="86">k &lt; 64 ?</text>
<rect class="vx-box" x="60" y="116" width="320" height="30" rx="4"/>
<text class="vx-text" x="72" y="136">K1</text>
<text class="vx-mono" x="104" y="136">check a[row, k]</text>
<rect class="vx-box" x="60" y="166" width="320" height="30" rx="4"/>
<text class="vx-text" x="72" y="186">K2</text>
<text class="vx-mono" x="104" y="186">check b[k, column]</text>
<rect class="vx-box" x="60" y="216" width="320" height="30" rx="4"/>
<text class="vx-text" x="72" y="236">K3</text>
<text class="vx-mono" x="104" y="236">sum += a[row, k] * b[k, column]</text>
<rect class="vx-box" x="60" y="266" width="320" height="30" rx="4"/>
<text class="vx-text" x="72" y="286">S</text>
<text class="vx-mono" x="104" y="286">k = k + 1</text>
<rect class="vx-box" x="480" y="66" width="250" height="30" rx="4"/>
<text class="vx-text" x="492" y="86">X</text>
<text class="vx-mono" x="516" y="86">c[row, column] = sum</text>
<rect class="vx-box-bad" x="480" y="166" width="250" height="30" rx="4"/>
<text class="vx-text" x="492" y="186">R</text>
<text class="vx-mono" x="516" y="186">report, exit 101</text>
<line class="vx-line" x1="220" y1="46" x2="220" y2="66" marker-end="url(#o8-f1a-head)"/>
<line class="vx-line" x1="220" y1="96" x2="220" y2="116" marker-end="url(#o8-f1a-head)"/>
<text class="vx-text-muted" x="228" y="110">yes</text>
<line class="vx-line" x1="220" y1="146" x2="220" y2="166" marker-end="url(#o8-f1a-head)"/>
<text class="vx-text-muted" x="228" y="160">passes</text>
<line class="vx-line" x1="220" y1="196" x2="220" y2="216" marker-end="url(#o8-f1a-head)"/>
<text class="vx-text-muted" x="228" y="210">passes</text>
<line class="vx-line" x1="220" y1="246" x2="220" y2="266" marker-end="url(#o8-f1a-head)"/>
<path class="vx-flow" d="M60 281 L32 281 L32 81 L60 81" marker-end="url(#o8-f1a-head)"/>
<line class="vx-line" x1="380" y1="81" x2="480" y2="81" marker-end="url(#o8-f1a-head)"/>
<text class="vx-text-muted" x="392" y="74">no</text>
<path class="vx-line" d="M380 131 L440 131 L440 181 L480 181" marker-end="url(#o8-f1a-head)"/>
<line class="vx-line" x1="380" y1="181" x2="440" y2="181"/>
<text class="vx-text-muted" x="392" y="124">fails</text>
<text class="vx-text-muted" x="392" y="174">fails</text>
<text class="vx-text-muted" x="480" y="236">header H, latch S</text>
<text class="vx-text-muted" x="480" y="256">exiting blocks H, K1 and K2</text>
<text class="vx-text-muted" x="480" y="276">four index checks per iteration</text>
</svg>
</div>
<div class="vx-step">
<p><strong>Step 2. Rotated.</strong> The test has moved to the bottom, into S, and K1 is the new header. The guard that rotation would place in front of the loop tests 0 &lt; 64 and folds away. X, the exit that continues the kernel, receives <code>sum</code> through a loop-closing phi. In every block of the loop, <code>k</code> now lies in [0, 63].</p>
<svg viewBox="0 0 760 330" role="img" aria-label="Step 2: the same loop rotated. P sets sum to 0.0 and k to 0; the guard 0 less than 64 is always true and has folded away. K1 is now the header and checks a[row, k]; K2 checks b[k, column]; K3 adds the product to sum; S adds 1 to k and tests k less than 64, going back to K1 when it holds. S's no edge leads to X, which takes sum through a loop-closing phi and stores it into c[row, column]. K1 and K2 can still fail to R. In every block of the loop, k lies between 0 and 63, and the back edge is taken 63 times.">
<defs><marker id="o8-f1b-head" viewBox="0 0 10 10" refX="10" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker></defs>
<rect class="vx-box" x="60" y="16" width="320" height="30" rx="4"/>
<text class="vx-text" x="72" y="36">P</text>
<text class="vx-mono" x="104" y="36">sum = 0.0; k = 0</text>
<text class="vx-text-muted" x="392" y="36">guard 0 &lt; 64: always true, folded away</text>
<rect class="vx-box-strong" x="60" y="66" width="320" height="30" rx="4"/>
<text class="vx-text" x="72" y="86">K1</text>
<text class="vx-mono" x="104" y="86">check a[row, k]</text>
<rect class="vx-box" x="60" y="116" width="320" height="30" rx="4"/>
<text class="vx-text" x="72" y="136">K2</text>
<text class="vx-mono" x="104" y="136">check b[k, column]</text>
<rect class="vx-box" x="60" y="166" width="320" height="30" rx="4"/>
<text class="vx-text" x="72" y="186">K3</text>
<text class="vx-mono" x="104" y="186">sum += a[row, k] * b[k, column]</text>
<rect class="vx-box" x="60" y="216" width="320" height="30" rx="4"/>
<text class="vx-text" x="72" y="236">S</text>
<text class="vx-mono" x="104" y="236">k = k + 1; k &lt; 64 ?</text>
<rect class="vx-box-bad" x="480" y="91" width="250" height="30" rx="4"/>
<text class="vx-text" x="492" y="111">R</text>
<text class="vx-mono" x="516" y="111">report, exit 101</text>
<rect class="vx-box" x="480" y="216" width="250" height="48" rx="4"/>
<text class="vx-text" x="492" y="236">X</text>
<text class="vx-mono" x="516" y="236">out = phi(sum)</text>
<text class="vx-mono" x="516" y="256">c[row, column] = out</text>
<line class="vx-line" x1="220" y1="46" x2="220" y2="66" marker-end="url(#o8-f1b-head)"/>
<line class="vx-line" x1="220" y1="96" x2="220" y2="116" marker-end="url(#o8-f1b-head)"/>
<text class="vx-text-muted" x="228" y="110">passes</text>
<line class="vx-line" x1="220" y1="146" x2="220" y2="166" marker-end="url(#o8-f1b-head)"/>
<text class="vx-text-muted" x="228" y="160">passes</text>
<line class="vx-line" x1="220" y1="196" x2="220" y2="216" marker-end="url(#o8-f1b-head)"/>
<path class="vx-flow" d="M60 231 L32 231 L32 81 L60 81" marker-end="url(#o8-f1b-head)"/>
<line class="vx-line" x1="380" y1="231" x2="480" y2="231" marker-end="url(#o8-f1b-head)"/>
<text class="vx-text-muted" x="392" y="224">no</text>
<path class="vx-line" d="M380 81 L440 81 L440 106 L480 106" marker-end="url(#o8-f1b-head)"/>
<path class="vx-line" d="M380 131 L440 131 L440 106"/>
<text class="vx-text-muted" x="392" y="74">fails</text>
<text class="vx-text-muted" x="392" y="148">fails</text>
<text class="vx-text-accent" x="60" y="298">k lies in [0, 63] in every block of the loop</text>
<text class="vx-text-muted" x="60" y="318">header K1, latch S, which now also holds the loop test; the back edge is taken 63 times</text>
</svg>
</div>
<div class="vx-step">
<p><strong>Step 3. After the range proof.</strong> Both checks are always true, so their branches and R are deleted, and the remaining blocks merge into one. The loop now has one block, one back edge and one exit, and its only branch is its own test.</p>
<svg viewBox="0 0 760 330" role="img" aria-label="Step 3: after the range proof. P sets sum to 0.0 and k to 0 and leads to one block, B, which is the header, the latch and the only exiting block: it adds a[row, k] times b[k, column] to sum, adds 1 to k and tests k less than 64, going back to itself when it holds. Its no edge leads to X, which takes sum through a loop-closing phi and stores it into c[row, column]. The check blocks K1 and K2 and the report R are gone.">
<defs><marker id="o8-f1c-head" viewBox="0 0 10 10" refX="10" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker></defs>
<rect class="vx-box" x="60" y="16" width="320" height="30" rx="4"/>
<text class="vx-text" x="72" y="36">P</text>
<text class="vx-mono" x="104" y="36">sum = 0.0; k = 0</text>
<rect class="vx-box-strong" x="60" y="66" width="320" height="84" rx="4"/>
<text class="vx-text" x="72" y="86">B</text>
<text class="vx-mono" x="104" y="86">sum += a[row, k] * b[k, column]</text>
<text class="vx-mono" x="104" y="108">k = k + 1</text>
<text class="vx-mono" x="104" y="130">k &lt; 64 ?</text>
<rect class="vx-box" x="480" y="106" width="250" height="48" rx="4"/>
<text class="vx-text" x="492" y="126">X</text>
<text class="vx-mono" x="516" y="126">out = phi(sum)</text>
<text class="vx-mono" x="516" y="146">c[row, column] = out</text>
<line class="vx-line" x1="220" y1="46" x2="220" y2="66" marker-end="url(#o8-f1c-head)"/>
<path class="vx-flow" d="M60 138 L32 138 L32 81 L60 81" marker-end="url(#o8-f1c-head)"/>
<line class="vx-line" x1="380" y1="126" x2="480" y2="126" marker-end="url(#o8-f1c-head)"/>
<text class="vx-text-muted" x="392" y="119">no</text>
<text class="vx-text-muted" x="480" y="190">K1, K2 and R: deleted</text>
<text class="vx-text-accent" x="60" y="190">one block, one back edge, one exit</text>
<text class="vx-text-muted" x="60" y="212">the only branch left is the loop's own test</text>
</svg>
</div>
</div>
<figcaption>Figure 1. The kernel's <code>k</code> loop in three shapes: as v0.1 lowers it, rotated into a do-while loop, and with its checks removed. The moving dashes are the back edge, the heavy outline marks the header, and the dashed outline is the block that reports a failed check.</figcaption>
</figure>

??? check "Why does rotation need a guard for `for i in 0..count` but not for `for i in 0..64`?"

    A do-while loop runs its body at least once, and `0..count` runs it zero times when `count` is 0 or less. The guard `0 < count` sends that case around the loop. For `0..64` the guard is `0 < 64`, a constant that is always true, so it folds away and the rotated loop is entered unconditionally.

## Induction variables

In the `k` loop, three values change in a regular way. `k` takes the values 0, 1, 2 and so on up to 63. The address of `a[row, k]` moves along a row: arrays are stored row by row ([decision 43](../decisions/arrays.md#d43)), an `f32` fills four bytes ([stage 8](../compiler/guide/stage-8-data-in-memory.md)), and element `[row, k]` of a 64 by 64 array starts 4 × (64 × row + k) bytes after the first, so each iteration adds 4. The address of `b[k, column]` moves down a column, adding 4 × 64 = 256 bytes per iteration. Figure 2 draws the two streams.

<figure class="vx-figure">
<svg viewBox="0 0 760 320" role="img" aria-label="Two grids drawn 8 by 8 stand for the 64 by 64 arrays a and b. In a, the cells of row 2 light up one after another from left to right: a[row, k] walks along the row, 4 bytes per step, the add recurrence {A + 256 row, +, 4} in the k loop. In b, the cells of column 5 light up one after another from top to bottom, in step with a: b[k, column] walks down the column, 256 bytes per step, the add recurrence {B + 4 column, +, 256} in the k loop.">
<text class="vx-text" x="40" y="24">a[row, k], with row = 2</text>
<text class="vx-text" x="420" y="24">b[k, column], with column = 5</text>
<rect class="vx-box" x="40" y="40" width="176" height="176"/>
<line class="vx-line" x1="62" y1="40" x2="62" y2="216"/>
<line class="vx-line" x1="84" y1="40" x2="84" y2="216"/>
<line class="vx-line" x1="106" y1="40" x2="106" y2="216"/>
<line class="vx-line" x1="128" y1="40" x2="128" y2="216"/>
<line class="vx-line" x1="150" y1="40" x2="150" y2="216"/>
<line class="vx-line" x1="172" y1="40" x2="172" y2="216"/>
<line class="vx-line" x1="194" y1="40" x2="194" y2="216"/>
<line class="vx-line" x1="40" y1="62" x2="216" y2="62"/>
<line class="vx-line" x1="40" y1="84" x2="216" y2="84"/>
<line class="vx-line" x1="40" y1="106" x2="216" y2="106"/>
<line class="vx-line" x1="40" y1="128" x2="216" y2="128"/>
<line class="vx-line" x1="40" y1="150" x2="216" y2="150"/>
<line class="vx-line" x1="40" y1="172" x2="216" y2="172"/>
<line class="vx-line" x1="40" y1="194" x2="216" y2="194"/>
<rect class="vx-cell-on vx-seq" style="--vx-i: 0; --vx-n: 8" x="40" y="84" width="22" height="22"/>
<rect class="vx-cell-on vx-seq" style="--vx-i: 1; --vx-n: 8" x="62" y="84" width="22" height="22"/>
<rect class="vx-cell-on vx-seq" style="--vx-i: 2; --vx-n: 8" x="84" y="84" width="22" height="22"/>
<rect class="vx-cell-on vx-seq" style="--vx-i: 3; --vx-n: 8" x="106" y="84" width="22" height="22"/>
<rect class="vx-cell-on vx-seq" style="--vx-i: 4; --vx-n: 8" x="128" y="84" width="22" height="22"/>
<rect class="vx-cell-on vx-seq" style="--vx-i: 5; --vx-n: 8" x="150" y="84" width="22" height="22"/>
<rect class="vx-cell-on vx-seq" style="--vx-i: 6; --vx-n: 8" x="172" y="84" width="22" height="22"/>
<rect class="vx-cell-on vx-seq" style="--vx-i: 7; --vx-n: 8" x="194" y="84" width="22" height="22"/>
<text class="vx-text-accent" x="226" y="100">k: 4 bytes a step</text>
<rect class="vx-box" x="420" y="40" width="176" height="176"/>
<line class="vx-line" x1="442" y1="40" x2="442" y2="216"/>
<line class="vx-line" x1="464" y1="40" x2="464" y2="216"/>
<line class="vx-line" x1="486" y1="40" x2="486" y2="216"/>
<line class="vx-line" x1="508" y1="40" x2="508" y2="216"/>
<line class="vx-line" x1="530" y1="40" x2="530" y2="216"/>
<line class="vx-line" x1="552" y1="40" x2="552" y2="216"/>
<line class="vx-line" x1="574" y1="40" x2="574" y2="216"/>
<line class="vx-line" x1="420" y1="62" x2="596" y2="62"/>
<line class="vx-line" x1="420" y1="84" x2="596" y2="84"/>
<line class="vx-line" x1="420" y1="106" x2="596" y2="106"/>
<line class="vx-line" x1="420" y1="128" x2="596" y2="128"/>
<line class="vx-line" x1="420" y1="150" x2="596" y2="150"/>
<line class="vx-line" x1="420" y1="172" x2="596" y2="172"/>
<line class="vx-line" x1="420" y1="194" x2="596" y2="194"/>
<rect class="vx-cell-on vx-seq" style="--vx-i: 0; --vx-n: 8" x="530" y="40" width="22" height="22"/>
<rect class="vx-cell-on vx-seq" style="--vx-i: 1; --vx-n: 8" x="530" y="62" width="22" height="22"/>
<rect class="vx-cell-on vx-seq" style="--vx-i: 2; --vx-n: 8" x="530" y="84" width="22" height="22"/>
<rect class="vx-cell-on vx-seq" style="--vx-i: 3; --vx-n: 8" x="530" y="106" width="22" height="22"/>
<rect class="vx-cell-on vx-seq" style="--vx-i: 4; --vx-n: 8" x="530" y="128" width="22" height="22"/>
<rect class="vx-cell-on vx-seq" style="--vx-i: 5; --vx-n: 8" x="530" y="150" width="22" height="22"/>
<rect class="vx-cell-on vx-seq" style="--vx-i: 6; --vx-n: 8" x="530" y="172" width="22" height="22"/>
<rect class="vx-cell-on vx-seq" style="--vx-i: 7; --vx-n: 8" x="530" y="194" width="22" height="22"/>
<text class="vx-text-accent" x="606" y="56">k: 256 bytes a step,</text>
<text class="vx-text-accent" x="606" y="74">one row down</text>
<text class="vx-mono" x="40" y="246">{A + 256·row,+,4}&lt;k&gt;</text>
<text class="vx-mono" x="420" y="246">{B + 4·column,+,256}&lt;k&gt;</text>
<text class="vx-text-muted" x="40" y="268">A: the address of a[0, 0]</text>
<text class="vx-text-muted" x="420" y="268">B: the address of b[0, 0]</text>
<text class="vx-text-muted" x="40" y="304">drawn 8 by 8; the arrays are 64 by 64, stored row by row</text>
</svg>
<figcaption>Figure 2. The two address streams of the <code>k</code> loop, drawn 8 by 8 instead of 64 by 64. The lit cells, which light up together, show the element each iteration reads: <code>a[row, k]</code> walks along row 2, 4 bytes at a time, and <code>b[k, column]</code> walks down column 5, 256 bytes at a time. Each stream is an add recurrence of <code>k</code>, and its start depends on a variable of an outer loop.</figcaption>
</figure>

A value that changes by the same amount in every iteration of a loop is an **induction variable**. The classic definition comes from the strength-reduction algorithm of Allen, Cocke and Kennedy, as Cooper, Simpson and Vick describe it, and it is recursive. A **region constant** of a loop is a value that does not change inside the loop. A variable is an induction variable when every definition of it inside the loop adds a region constant to an induction variable, subtracts one from it, or copies one.[^osr] `k` is a **basic induction variable**, which steps itself by a constant; the two addresses are **derived induction variables**, computed from `k` by multiplying and adding region constants.[^absar] `row` and `column` are region constants of the `k` loop.

SSA form shows an induction variable as a cycle. `k` is a phi at the header whose operand from the latch adds 1 to the phi itself ([O3](o3-ssa.md#your-turn-the-kernel-in-ssa-form)), so the phi and the add form a cycle in the graph of definitions and uses. Cooper, Simpson and Vick find induction variables by looking for exactly such cycles: strongly connected components of the SSA graph whose every operation is one of the allowed updates.[^osr]

### Add recurrences

LLVM's analysis of loop values writes an induction variable as an **add recurrence**, {start,+,step}&lt;L&gt;: the value that is `start` in the first iteration of loop L and grows by `step` in each later one.[^absar] [^scev-expr] `k` is {0,+,1}&lt;k&gt;. The step can be any value the loop does not change, and the start can itself be a recurrence of an enclosing loop, which is how nested loops appear. With A and B for the addresses of `a[0, 0]` and `b[0, 0]`:

| Value | In the `k` loop | In the whole nest |
| --- | --- | --- |
| `k` | {0,+,1}&lt;k&gt; | the same |
| address of `a[row, k]` | {A + 256·row,+,4}&lt;k&gt; | {{A,+,256}&lt;row&gt;,+,4}&lt;k&gt; |
| address of `b[k, column]` | {B + 4·column,+,256}&lt;k&gt; | {{B,+,4}&lt;column&gt;,+,256}&lt;k&gt; |

Recurrences combine by rules simple enough for an analysis to push them through arithmetic. Adding a loop-invariant value to a recurrence adds it to the start; multiplying by one multiplies both start and step; adding two recurrences of the same loop adds their starts and their steps.[^absar] [^bwz] So the address of `a[row, k]` folds in three steps: 64·row + {0,+,1} is {64·row,+,1}; four times that is {256·row,+,4}; and adding A gives the table's entry.

Multiplying two recurrences of the same loop gives something new: {0,+,1} × {0,+,1} is {0,+,1,+,2}, the squares 0, 1, 4, 9, and so on, whose step is itself the recurrence {1,+,2}.[^absar] A recurrence whose step is a recurrence is a **chain of recurrences**. Bachmann, Wang and Zima introduced chains in 1994 as a way to evaluate a function at evenly spaced points, and LLVM's scalar-evolution analysis lists their paper first among its references.[^bwz] [^scev-cpp]

Chains have two properties a compiler uses. Moving a chain to its next value takes one addition per link and no multiplication: in Bachmann, Wang and Zima's terms, the cost of the next value is the length of the chain.[^bwz] And the value in any single iteration i has a closed form, a sum of binomial coefficients:[^scev-cpp]

$$
\{c_0,+,c_1,+,c_2,+,\dots\}(i) \;=\; c_0 + c_1\binom{i}{1} + c_2\binom{i}{2} + \cdots
$$

The first property is strength reduction, and the second lets a compiler compute what a loop leaves behind without running it. The second example builds chains for four polynomials from their first values, steps each chain eight times with additions, compares every value with the polynomial, and then jumps to i = 64 with the formula:

--8<-- "includes/examples/optimize/o8-loops/recurrences.cpp.md"

The first line is the address stream of `b[k, 5]` in bytes, a chain of one link: one addition per step. The last, i³, has three links and costs three additions per step. Its value at i = 64 is 262,144, the number of times the kernel's innermost body runs.

??? check "What add recurrence describes the byte offset of `c[row, column]`, first in the `column` loop and then in the whole nest?"

    The element starts 4 × (64·row + column) bytes into the array. In the `column` loop, `row` is a region constant, so the offset is {256·row,+,4}&lt;column&gt;. In the nest, `row` is {0,+,1}&lt;row&gt;, so the start 256·row is {0,+,256}&lt;row&gt; and the offset is {{0,+,256}&lt;row&gt;,+,4}&lt;column&gt;.

## Scalar evolution in LLVM

LLVM's ScalarEvolution analysis, SCEV for short, represents integer and pointer values as expressions built from constants, arithmetic, add recurrences, and unknown leaves for values it cannot describe, such as a number loaded from memory. Its expressions never contain a cycle, even for a phi in a loop: a phi that follows a recognized pattern becomes a recurrence, and any other becomes an unknown.[^scev-cpp] It describes integers and pointers only.[^scev-cpp] The kernel's `sum`, an `f32`, is invisible to it, and nothing in this chapter changes a floating-point operation, so [decision 56](../decisions/numbers.md#d56) is never at stake here. LLVM's pass list describes the analysis as primarily useful for induction variable substitution and strength reduction, and as the source of loop trip counts.[^llvm-passes]

`opt -passes='print<scalar-evolution>'` prints what SCEV knows. For `clear_all`, a checked loop from the third example below that clears 64 elements, LLVM 18.1.8 on the owner's M4 Pro printed this for the loop counter (2026-09-24; columns shortened):

```text
%i = phi i32 [ 0, %entry ], [ %next, %body ]
-->  {0,+,1}<nuw><nsw><%header> U: [0,65) S: [0,65)   Exits: 64
Loop %header: <multiple exits> backedge-taken count is 64
  exit count for header: 64
  exit count for check: 64
```

The recurrence is {0,+,1} in the loop whose header is `%header`. The flags `<nuw>` and `<nsw>` say that the recurrence never wraps, read as an unsigned or as a signed number.[^scev-h] U and S are the value's range read those two ways, and `Exits` is its value after the loop:[^scev-cpp] 0 up to 64, since the header sees `i` = 64 once, on its way out, and 64 at the end. The counts are the subject of the next section.

### No-wrap flags

No-wrap flags are facts with a precise meaning. On an LLVM `add`, `nuw` and `nsw` mean that if the unsigned or signed result overflows, the add produces a poison value.[^langref-add] An optimizer can read them from the instructions or prove them itself, as SCEV did here: the loop's `add` carried no flag. LLVM's advice to front ends is to add the flags wherever the source language guarantees them, because reasoning about overflow is hard for an optimizer and facts from the front end help it a great deal.[^perftips]

Vortex guarantees one such fact outright. The variable of a `for` loop never overflows ([Expressions 5.5](../specification/expressions.md#checked-integer-operations)): an exclusive range stops at `end - 1`, and an inclusive one ends without computing `end + 1` ([Statements 6.8](../specification/statements.md#68-for-loops)). So a Vortex front end may mark the step of a `for` loop `nsw` when the variable is an `i32`, and `nuw` when it is a `u32` or a `usize`, with the language rule as its proof. Not both: a signed counter that runs from -3 to 3 steps from -1 to 0, which wraps when read as an unsigned number.

A flag must never be a hope. Suppose a front end computed a checked `x + 1` with `add nsw` and tested for overflow afterwards, by asking whether the sum came out smaller than `x`. On the owner's machine, LLVM 18.1.8's InstCombine (2026-09-24) rewrote the test with and without the flag as the comments say:

```llvm
define i1 @with_flag(i32 %x) {
  %s = add nsw i32 %x, 1
  %wrapped = icmp slt i32 %s, %x      ; InstCombine: ret i1 false
  ret i1 %wrapped
}

define i1 @without_flag(i32 %x) {
  %t = add i32 %x, 1
  %wrapped2 = icmp slt i32 %t, %x     ; InstCombine: icmp eq i32 %x, 2147483647
  ret i1 %wrapped2
}
```

With the flag, an overflowing sum is poison, the optimizer may assume any value for it, and the test disappears; the check Vortex promised is gone. Without the flag, the test becomes the exact condition for overflow. [O11](o11-undefined-behavior.md) explains poison and the proof each flag needs. A checked operation belongs before the arithmetic, as [I8](../decisions/implementation.md#i8) suggests, or in an overflow intrinsic such as `llvm.sadd.with.overflow`, which returns the sum together with a bit that says whether the signed addition overflowed.[^langref-sadd]

### Closed forms

Recurrences and counts together can replace a loop. Take a loop that adds 0, 1, ..., n − 1 into `sum`, written in LLVM IR with a plain wrapping `add`. On the same machine and date, SCEV described `sum` as {0,+,0,+,1} and the loop's backedge-taken count as `(0 smax %n)`, which is n when n is positive and 0 otherwise. `opt -passes='loop(indvars,loop-deletion)'` then turned the whole function into:

```llvm
define i32 @triangle(i32 %n) {
entry:
  %smax = call i32 @llvm.smax.i32(i32 %n, i32 0)
  %0 = zext nneg i32 %smax to i33
  %1 = add nsw i32 %smax, -1
  %2 = zext i32 %1 to i33
  %3 = mul i33 %0, %2
  %4 = lshr i33 %3, 1
  br label %exit

exit:                                             ; preds = %entry
  %5 = trunc i33 %4 to i32
  ret i32 %5
}
```

That is n(n − 1)/2: the chain evaluated at the backedge-taken count with the binomial formula. The product is formed in 33 bits so that it cannot overflow before the division by 2. SCEV's source notes that the evaluation stays correct under overflow only if the multiplication comes after the binomial coefficient is computed.[^scev-cpp] IndVarSimplify's header describes the rewrite: a use after the loop of a value derived from an induction variable is computed outside the loop, and a loop that exists only to compute such a value becomes dead.[^indvars] LoopDeletion then removes it.[^llvm-passes]

In Vortex the same loop, `sum += i` on `i32`, carries an overflow check, and a check is an exit: the loop can stop early with a runtime error. The rewrite would delete that failure, so it is legal only with a proof that no partial sum overflows. Here the proof is exact arithmetic. The largest partial sum is the last, n(n − 1)/2, which fits in an `i32` for n up to 65,536 and exceeds 2,147,483,647 for any larger n.

## Counting iterations

The third fact a proof needs is how many times the loop runs. LLVM defines two measures. The **trip count** is the number of times the header executes before control leaves the loop, and the **backedge-taken count** is the number of times a back edge is taken; for a run that enters the header, the second is one less than the first.[^llvm-loops] [^scev-h]

The two depend on the loop's shape. `clear_all` above is tested at the top, so its header runs 65 times, once more than its body, and SCEV reports a backedge-taken count of 64. After `loop-rotate`, the header is the first block of the body, and SCEV (same machine and date) reports the counter as `{0,+,1}<nuw><nsw><%check> U: [0,64) S: [0,64)` with exit value 63 and a backedge-taken count of 63. The range of the counter is now exactly the range the check demands.

Compilers prefer the backedge-taken count. LLVM's documentation gives one reason: before rotation, a loop whose body never runs still executes its header once, so its trip count is 1.[^llvm-loops] Width gives another. The trip count is the backedge-taken count plus one, and SCEV's header warns that the sum can overflow: an 8-bit count of 255 back edges is a trip count of 256, which is 0 in 8 bits.[^scev-h]

Vortex meets the second case head on. `for i in 0..=m`, with `m` a `u32` holding 4,294,967,295, runs its body $2^{32}$ times, and [Statements 6.8](../specification/statements.md#68-for-loops) allows it because the loop never computes `m + 1`. No `u32` holds $2^{32}$, but the backedge-taken count, 4,294,967,295, fits. The natural lowering is a rotated loop: run the body, leave if `i` equals `m`, and only then add 1. The addition runs only when `i` is below `m`, so it never overflows and may be marked `nuw`.

MLIR's `scf.for` states its count in closed form: the loop runs max(0, ⌈(UB − LB)/step⌉) times, and the operation requires LB + n·step to be representable in the counter's type, leaving the behavior undefined otherwise, so that it can be lowered to one increment and one comparison.[^mlir-scf] A Vortex loop over `a..b` meets that rule, since LB + n·step is `b`. An inclusive loop that ends at its type's largest value does not; it needs a wider counter or the bottom-tested form above. [M6](../mlir/m6-affine-and-scf.md) returns to it.

??? check "For `for i in a..=b`, what guard does the rotated loop need, what is its backedge-taken count, and why does that count always fit in 32 bits when `i` is an `i32`?"

    The guard is `a <= b`, because the body runs zero times when `a > b`. When the loop runs, the back edge is taken b − a times: the body runs for a, a + 1, ..., b, and the last iteration leaves without taking it. For `i32` endpoints, b − a is at most 2,147,483,647 − (−2,147,483,648) = 4,294,967,295, the largest 32-bit unsigned number. It fits when read as unsigned, though not as a signed `i32`.

### Loops with several exits

A loop with several exiting blocks has an **exit count** for each: the number of back edges taken before that exit is taken, if it ever is. SCEV's exact backedge-taken count is the smallest of them, and it exists only when every one can be computed and every exiting block dominates the latch.[^scev-cpp] The printout above shows why a checked loop has several: `clear_all` exits through its loop test and through its bounds check, and SCEV counted both at 64. For `clear_first`, whose bound is a parameter, it printed `(0 smax %count)` for the loop test, 64 for the check, and `(64 umin (0 smax %count))` for the loop, which stops at whichever comes first. The check's exit is countable, which matters for IRCE and for the vectorizer below.

Not every exit can be counted. An overflow check on a value loaded from memory exits when the data says so; SCEV prints `***COULDNOTCOMPUTE***` for it and calls the loop's count unpredictable. On the owner's machine the same happened (LLVM 18.1.8, 2026-09-24) for a check on an index the loop never changes, a parameter `column` checked against 64 in every iteration: such a loop leaves in its first iteration or never, and SCEV counted neither.

## Removing a check with a proof

With the three facts in hand, the proof for `a[row, k]` is short. In the rotated `k` loop, `k` is {0,+,1}&lt;k&gt; and the backedge-taken count is 63, so in every iteration `k` is one of 0, 1, ..., 63. It is at least 0 because the recurrence starts at 0, grows, and does not wrap; it is at most 63 because iteration 63 is the last. The check's condition, `k` below 64 when read as an unsigned number, holds in every iteration, and the branch to R is never taken. In the top-tested form the same conclusion follows from [edge dominance](o2-cfg-and-dominance.md#dominance-in-llvm-and-mlir): the true edge of the header's test `k < 64` dominates the check. [O4](o4-dataflow.md#intervals-bounding-a-loop-index) reached the same interval by iterating to a fixed point with widening; SCEV computes it from the recurrence and the count instead.

In LLVM this is the work of IndVarSimplify. For each comparison that uses an induction variable, it asks SCEV whether the comparison is always true or always false at the point where it is used, and replaces a comparison that is decided with the constant. When it cannot decide, it may still turn a signed comparison of two values known to be non-negative into an unsigned one.[^indvars] SimplifyCFG then deletes the branch that can no longer go two ways, and the report block with it. The third example runs both passes on two loops: `clear_all`, whose bound is 64, and `clear_first`, whose bound is a parameter.

--8<-- "includes/examples/optimize/o8-loops/check_elimination.ll.md"

In `clear_all` the check is gone, the loop has one exit, the step has gained `nuw nsw`, and the header's test has become unsigned, as described above. In `clear_first` the check stays, because `count` may be 70. The step gained both flags there too: `i` starts at 0 and stays below `count`, so `i + 1` never wraps either way.

A check that stays still teaches the optimizer something. After `values[i]` passes its check, `i` is known to lie in [0, 64) for the rest of the iteration. Bodík, Gupta and Sarkar's ABCD algorithm, designed for Java's bounds checks, uses exactly that: a passed check is one of its five sources of facts, beside array lengths, constant assignments, increments by constants and branch conditions.[^abcd] LLVM's advice to front ends makes a related point about indexes: when the language knows an index is non-negative, extend it to the machine's register width with zero extension rather than sign extension.[^perftips] After a Vortex bounds check passes, the index is known to be non-negative.

### Overflow checks in index arithmetic

Overflow checks yield to the same reasoning. Vortex v0.1 has no linear indexing of a multidimensional array ([Arrays 7.6](../specification/arrays.md#76-indexing)), so a program that wants a flat layout writes the arithmetic itself:

```vortex
// items: valid
fn flatten(grid: &[f32; 8, 8], flat: &mut [f32; 64]) {
    for row in 0..8 {
        for column in 0..8 {
            flat[row * 8 + column] = grid[row, column];
        }
    }
}
```

Each inner iteration runs five checks: two index checks for `grid[row, column]`, overflow checks on `row * 8` and on `+ column` ([Expressions 5.5](../specification/expressions.md#checked-integer-operations)), and a bounds check on the flat index. As recurrences, `row * 8` is {0,+,8}&lt;row&gt;, with values 0 to 56, and the flat index is {{0,+,8}&lt;row&gt;,+,1}&lt;column&gt;, with values 0 to 63. Neither operation can overflow, the flat index is in bounds, and all five checks can go. Once they are proven, the compiler may mark the multiplication and the addition `nsw`, with the ranges as its proof.

### Why the checks matter for the vectorizer

Each check costs a comparison and a branch that goes the same way every time. What that costs on a given machine is a measurement ([P1](p1-measure-first.md)), not a guess. What a check can cost the vectorizer does not need a measurement. LLVM 18's loop vectorizer asks SCEV for the loop's backedge-taken count through its memory-dependence analysis, and gives up with "could not determine number of loop iterations" when there is none.[^laa] A loop whose exiting block is not its latch must run at least its final iteration as scalar code,[^lv] so a countable check can stay: the vector loop covers iterations that cannot reach it, and the check runs in the scalar epilogue. The fourth example gives the vectorizer one loop with a bounds check and one with an overflow check on loaded data:

--8<-- "includes/examples/optimize/o8-loops/vectorize_checks.ll.md"

`scale_all` is vectorized with its check still in place; `bump_all` is not. The same happened, on the owner's machine and date, with a parameter `count` as the bound of `scale_all`, whose rotated loop SCEV counts as `(64 umin (-1 + %count))`, and with the vector width left to the AArch64 cost model instead of forced. The current LLVM documentation describes a newer mechanism, early-exit vectorization, for loops with one early exit;[^llvm-vec] a compiler that targets LLVM 18 should not count on it.

For the kernel, the checks do matter. In a two-level nest shaped like its `row` and `k` loops, measured on the owner's machine (LLVM 18.1.8, 2026-09-24), the inner loop's check on `row`, a value that loop never changes, had no computable exit count, so the inner loop's count was unpredictable, as for the parameter above. After IndVarSimplify and SimplifyCFG no check remained, and SCEV counted 64 back edges for each loop. Removing the checks is still not enough to vectorize the `k` loop: strict floating point fixes the order of the additions into `sum` ([O3](o3-ssa.md#your-turn-the-kernel-in-ssa-form), [P10](p10-vectorization.md)), and until the compiler states otherwise, `c` might overlap `a` or `b` ([O9](o9-alias-analysis.md)). The checks also fence in any transformation that reorders iterations: [P7](p7-loop-transformations.md) transforms only loop nests whose checks are proven unable to fail.

## When the bound is not a constant

Not every check can be proven. In this function the bound comes from the caller:

```vortex
// items: valid
fn clear_first(values: &mut [i32; 64], count: i32) {
    for i in 0..count {
        values[i] = 0;
    }
}
```

When `count` is at most 64, no check fails. When it is 70, iterations 0 to 63 clear the array, and iteration 64 stops the program with a bounds error. The compiler cannot know which call it will get, so the check must stay somewhere, but it need not run in every iteration. There are three correct ways to compile this loop and one tempting wrong one, drawn in Figure 3 for `count` = 70.

<figure class="vx-figure">
<svg viewBox="0 0 760 340" role="img" aria-label="Four rows of iterations 0 to 69 of clear_first with count 70. Row 1, keep every check: iterations 0 to 63 run with a check each, iteration 64 fails its check, and 65 to 69 never run. Row 2, check first: a test before the loop fails at once, and no iteration runs; this is wrong for Vortex. Row 3, version the loop: a test before the loop sends the call to the checked copy, which behaves exactly like row 1. Row 4, split the iteration space: iterations 0 to 63 run without checks, iteration 64 runs with its check and fails, and 65 to 69 never run; the result is the same as row 1.">
<text class="vx-text-muted" x="176" y="30" text-anchor="middle">before</text>
<text class="vx-text-muted" x="228" y="30" text-anchor="middle">0</text>
<text class="vx-text-muted" x="264" y="30" text-anchor="middle">1</text>
<text class="vx-text-muted" x="300" y="30" text-anchor="middle">2</text>
<text class="vx-text-muted" x="336" y="30" text-anchor="middle">…</text>
<text class="vx-text-muted" x="372" y="30" text-anchor="middle">62</text>
<text class="vx-text-muted" x="408" y="30" text-anchor="middle">63</text>
<text class="vx-text-muted" x="444" y="30" text-anchor="middle">64</text>
<text class="vx-text-muted" x="480" y="30" text-anchor="middle">65</text>
<text class="vx-text-muted" x="516" y="30" text-anchor="middle">…</text>
<text class="vx-text-muted" x="552" y="30" text-anchor="middle">69</text>
<text class="vx-text" x="20" y="61">Keep every check</text>
<rect class="vx-box" x="212" y="42" width="32" height="28" rx="3"/>
<circle class="vx-dot" cx="228" cy="56" r="4"/>
<rect class="vx-box" x="248" y="42" width="32" height="28" rx="3"/>
<circle class="vx-dot" cx="264" cy="56" r="4"/>
<rect class="vx-box" x="284" y="42" width="32" height="28" rx="3"/>
<circle class="vx-dot" cx="300" cy="56" r="4"/>
<text class="vx-text-muted" x="336" y="61" text-anchor="middle">…</text>
<rect class="vx-box" x="356" y="42" width="32" height="28" rx="3"/>
<circle class="vx-dot" cx="372" cy="56" r="4"/>
<rect class="vx-box" x="392" y="42" width="32" height="28" rx="3"/>
<circle class="vx-dot" cx="408" cy="56" r="4"/>
<rect class="vx-box-bad vx-pulse" x="428" y="42" width="32" height="28" rx="3"/>
<circle class="vx-dot" cx="444" cy="56" r="4"/>
<rect class="vx-line" x="464" y="42" width="32" height="28" rx="3"/>
<text class="vx-text-muted" x="516" y="61" text-anchor="middle">…</text>
<rect class="vx-line" x="536" y="42" width="32" height="28" rx="3"/>
<text class="vx-text-muted" x="584" y="54">fails at 64,</text>
<text class="vx-text-muted" x="584" y="70">after 0 to 63</text>
<text class="vx-text" x="20" y="131">Check first</text>
<rect class="vx-box-bad vx-pulse" x="156" y="112" width="40" height="28" rx="3"/>
<text class="vx-text-muted" x="176" y="131" text-anchor="middle">fail</text>
<rect class="vx-line" x="212" y="112" width="32" height="28" rx="3"/>
<rect class="vx-line" x="248" y="112" width="32" height="28" rx="3"/>
<rect class="vx-line" x="284" y="112" width="32" height="28" rx="3"/>
<text class="vx-text-muted" x="336" y="131" text-anchor="middle">…</text>
<rect class="vx-line" x="356" y="112" width="32" height="28" rx="3"/>
<rect class="vx-line" x="392" y="112" width="32" height="28" rx="3"/>
<rect class="vx-line" x="428" y="112" width="32" height="28" rx="3"/>
<rect class="vx-line" x="464" y="112" width="32" height="28" rx="3"/>
<text class="vx-text-muted" x="516" y="131" text-anchor="middle">…</text>
<rect class="vx-line" x="536" y="112" width="32" height="28" rx="3"/>
<text class="vx-text-accent" x="584" y="124">fails before 0:</text>
<text class="vx-text-accent" x="584" y="140">wrong for Vortex</text>
<text class="vx-text" x="20" y="201">Version the loop</text>
<rect class="vx-box" x="156" y="182" width="40" height="28" rx="3"/>
<text class="vx-text-muted" x="176" y="201" text-anchor="middle">test</text>
<rect class="vx-box" x="212" y="182" width="32" height="28" rx="3"/>
<circle class="vx-dot" cx="228" cy="196" r="4"/>
<rect class="vx-box" x="248" y="182" width="32" height="28" rx="3"/>
<circle class="vx-dot" cx="264" cy="196" r="4"/>
<rect class="vx-box" x="284" y="182" width="32" height="28" rx="3"/>
<circle class="vx-dot" cx="300" cy="196" r="4"/>
<text class="vx-text-muted" x="336" y="201" text-anchor="middle">…</text>
<rect class="vx-box" x="356" y="182" width="32" height="28" rx="3"/>
<circle class="vx-dot" cx="372" cy="196" r="4"/>
<rect class="vx-box" x="392" y="182" width="32" height="28" rx="3"/>
<circle class="vx-dot" cx="408" cy="196" r="4"/>
<rect class="vx-box-bad vx-pulse" x="428" y="182" width="32" height="28" rx="3"/>
<circle class="vx-dot" cx="444" cy="196" r="4"/>
<rect class="vx-line" x="464" y="182" width="32" height="28" rx="3"/>
<text class="vx-text-muted" x="516" y="201" text-anchor="middle">…</text>
<rect class="vx-line" x="536" y="182" width="32" height="28" rx="3"/>
<text class="vx-text-muted" x="584" y="194">count &gt; 64: the</text>
<text class="vx-text-muted" x="584" y="210">checked copy runs</text>
<text class="vx-text" x="20" y="271">Split (IRCE)</text>
<rect class="vx-box-accent" x="212" y="252" width="32" height="28" rx="3"/>
<rect class="vx-box-accent" x="248" y="252" width="32" height="28" rx="3"/>
<rect class="vx-box-accent" x="284" y="252" width="32" height="28" rx="3"/>
<text class="vx-text-muted" x="336" y="271" text-anchor="middle">…</text>
<rect class="vx-box-accent" x="356" y="252" width="32" height="28" rx="3"/>
<rect class="vx-box-accent" x="392" y="252" width="32" height="28" rx="3"/>
<rect class="vx-box-bad vx-pulse" x="428" y="252" width="32" height="28" rx="3"/>
<circle class="vx-dot" cx="444" cy="266" r="4"/>
<rect class="vx-line" x="464" y="252" width="32" height="28" rx="3"/>
<text class="vx-text-muted" x="516" y="271" text-anchor="middle">…</text>
<rect class="vx-line" x="536" y="252" width="32" height="28" rx="3"/>
<text class="vx-text-muted" x="584" y="264">fails at 64,</text>
<text class="vx-text-muted" x="584" y="280">checks only from 64</text>
<rect class="vx-box" x="20" y="306" width="18" height="16" rx="2"/>
<circle class="vx-dot" cx="29" cy="314" r="3"/>
<text class="vx-text-muted" x="44" y="319">runs with its check</text>
<rect class="vx-box-accent" x="190" y="306" width="18" height="16" rx="2"/>
<text class="vx-text-muted" x="214" y="319">runs with no check</text>
<rect class="vx-box-bad" x="360" y="306" width="18" height="16" rx="2"/>
<text class="vx-text-muted" x="384" y="319">its check fails</text>
<rect class="vx-line" x="510" y="306" width="18" height="16" rx="2"/>
<text class="vx-text-muted" x="534" y="319">never runs</text>
</svg>
<figcaption>Figure 3. Four ways to compile <code>clear_first</code>, shown for a call with <code>count</code> = 70, in which iteration 64 must fail. Keeping every check, versioning the loop (whose test sends this call to the checked copy) and splitting the iteration space all fail at iteration 64, after iterations 0 to 63 have run. Checking first fails before any iteration runs, which a Vortex compiler must not do.</figcaption>
</figure>

**Keep every check.** Always correct, and the baseline for the other three.

**Version the loop.** Before the loop, test whether the whole range is safe, here `count <= 64`. If it is, run a copy of the loop with no check; if not, run the original, checked loop. Any failure then happens in the original, at the same iteration and with the same message. The price is code size: two copies of the loop.

**Split the iteration space.** Run the iterations that are provably safe with no check, and the rest with it. LLVM's InductiveRangeCheckElimination pass, IRCE, does this. It looks for an **inductive range check**: a branch in a loop whose failing side is rarely taken and whose condition is provably true for a contiguous range of the induction variable's values. It then splits the loop's iterations into three ranges, before, inside and after that range, so that the middle loop needs no check.[^irce] IRCE also needs the loop's latch to end in the loop's own test, which is the rotated form,[^irce] so the fifth example rotates `clear_first` first:

--8<-- "includes/examples/optimize/o8-loops/irce.ll.md"

Read the preheader first. `%exit.mainloop.at` is max(min(`count`, 64), 0), the end of the safe range. The main loop, still named `check`, runs from 0 to that bound with no bounds check, testing only its own exit. `main.exit.selector` decides whether iterations remain, and if so the post-loop runs them, check included. There is no pre-loop, because `i` starts at 0 and a negative index is impossible. IRCE also marks the post-loop so that later passes do not unroll or vectorize it: its source treats the loops it adds as slow paths.[^irce] The pass is not in LLVM's standard pipeline: LLVM's advice to front ends for languages with range checks is to add it, and to consider extra runs of loop unswitching and LICM, since a pipeline tuned for C may leave such checks in loops.[^perftips] [O10](o10-pass-pipelines.md) builds pipelines.

**Check first: the wrong way.** The tempting option is to move the check itself in front of the loop: test `count <= 64` once, and report the failure at once if the test is false. For Vortex this is wrong. It reports before iterations 0 to 63 have run, so anything they would have printed is missing. And in a loop with other checks, one of those iterations might have failed a different check first, and that failure is the one the program must report. Java has the same requirement, and Bodík, Gupta and Sarkar give it as the reason bounds checks block code motion: precise exceptions require the program state at a failure, and the order of failures, to be preserved.[^abcd] Vortex's specification says the same in its own terms: an optimization may remove an evaluation only if every specified observable behavior and every required diagnostic stays the same ([Expressions 5.10](../specification/expressions.md#510-evaluation-order)). That rule decides this program:

```vortex
// program: runtime error
fn main() {
    let values = [10, 20, 30, 40];
    for i in 0..6 {
        print(values[i]);
    }
}
```

It must print 10, 20, 30 and 40, then stop with one `bounds` error line for index 4 and exit with status 101 ([decision 14](../decisions/program.md#d14)). The compiler may not reject it, even though it can prove the check fails; it may warn ([decision 39](../decisions/diagnostics.md#d39)). A split loop runs iterations 0 to 3 with no check and iteration 4 with its check, and prints exactly that. A check moved in front of the loop would print only the error.

One kind of check can move in front of a loop legally: one whose condition the loop never changes, provided the loop is certain to run, and nothing observable, and no other check, comes before it in the first iteration. Such a check would have failed in the first iteration anyway, with the same message and nothing printed before it. The guard of a rotated loop supplies the first condition; the second has to be read off the loop's body.

## Strength reduction and the exit test

**Strength reduction** replaces a costly operation in a loop with a cheaper one that updates the previous iteration's value. Its classic target is array addressing, and its classic trade is additions for multiplications.[^osr] A direct translation of `b[k, column]` multiplies `k` by 256 in every iteration. Strength reduction keeps a pointer that starts at B + 4·column and adds 256 instead: the chain {B + 4·column,+,256}, stepped as the second example stepped its chains.

Cooper, Simpson and Vick's algorithm, OSR, does this on SSA form. It finds the induction variables as cycles in the SSA graph, then looks for operations that multiply or add an induction variable and a region constant, and replaces each with a new induction variable. A region constant, for OSR, is a compile-time constant or a value whose definition strictly dominates every block of the loop.[^osr] They pair it with **linear function test replacement**, which rewrites the loop's exit test in terms of a new induction variable so that the old counter, if nothing else uses it, becomes dead.[^osr]

LLVM splits the work between two passes. IndVarSimplify canonicalizes: its header gives the example of rewriting `for (i = 7; i*i < 1000; ++i)` as `for (i = 0; i != 25; ++i)`, a loop with the same 25 iterations and a simpler test.[^indvars] LoopStrengthReduce runs late, among the IR passes the code generator runs first, and rewrites address computations to fit the target's scaled-index addressing modes, choosing among candidate formulas for each address.[^lsr] [^absar] A compiler that emits LLVM IR therefore gets strength reduction without writing it. A compiler with its own back end ([B1](../backend/b1-simplest-backend.md)) can build it once it has SSA form and add recurrences; which formula is cheapest depends on the addressing modes of the target, which [A2](../backend/a2-aarch64-assembly.md) describes for AArch64.

## Your turn: a three-point average

This function averages each element of `input` with its two right-hand neighbours:

```vortex
// items: valid
fn smooth(input: &[f32; 66], output: &mut [f32; 64]) {
    for i in 0..64 {
        output[i] = (input[i] + input[i + 1] + input[i + 2]) / 3.0;
    }
}
```

Each iteration runs six checks: four bounds checks and two overflow checks. Part of the table is filled in:

| Operation | Its check | Add recurrence in the `i` loop | Values over the loop | Always passes? |
| --- | --- | --- | --- | --- |
| `input[i]` | 0 ≤ i < 66 | {0,+,1} | 0 to 63 | yes |
| `i + 1` | no `i32` overflow | {1,+,1} | 1 to 64 | ? |
| `input[i + 1]` | 0 ≤ i + 1 < 66 | {1,+,1} | ? | ? |
| `i + 2` | no `i32` overflow | ? | ? | ? |
| `input[i + 2]` | 0 ≤ i + 2 < 66 | ? | ? | ? |
| `output[i]` | 0 ≤ i < 64 | ? | ? | ? |

Complete the table. Then change the type of `input` to `&[f32; 65]`: which check can fail, in which iteration, where is the failure reported, and what does splitting the iteration space produce?

??? check "Answers for the three-point average"

    - `i + 1` has values 1 to 64 and never overflows. `input[i + 1]` reads 1 to 64, below 66. `i + 2` is {2,+,1}, with values 2 to 65, and never overflows. `input[i + 2]` reads 2 to 65, below 66. `output[i]` is {0,+,1}, with values 0 to 63, below 64. All six checks always pass, and all six can be removed.
    - With 65 elements, `input[i + 2]` reads index 65 when `i` is 63, the last iteration. Within that iteration, `input[63]`, `i + 1`, `input[64]` and `i + 2` all pass first, since operands are evaluated from left to right ([Expressions 5.10](../specification/expressions.md#510-evaluation-order)); then the check on `input[i + 2]` fails, the error line points at that indexing operation, and `output[63]` is never written.
    - The bounds are constants, so every call fails there. The compiler must still accept the program, and may warn ([decision 39](../decisions/diagnostics.md#d39)). Splitting runs iterations 0 to 62 with no check, since `i + 2` is at most 64 there, and iteration 63 with the one check that can fail; the other five checks pass in every iteration and are removed everywhere.

## For Vortex

!!! vortex "Exercise"

    **Build** range-based check elimination for loops whose bounds and shapes your compiler knows, on top of the control-flow toolkit from [O2](o2-cfg-and-dominance.md#for-vortex), the SSA form from [O3](o3-ssa.md#for-vortex) and the remark stream from [O1](o1-optimizer-contract.md#for-vortex).

    1. Loop structure: for each natural loop, its header, latch, exiting blocks and exits, and a canonicalizer that adds a preheader, merges several latches into one and gives every exit block only predecessors inside the loop. If your IR is in SSA form, add loop-closing phis for values used after a loop. Verify all four properties after every pass.
    2. Induction variables as add recurrences: a header phi whose value from the latch is the phi plus a loop-invariant integer. Build derived recurrences through addition, subtraction and multiplication by loop-invariant values. Keep the coefficients in 128-bit or arbitrary-precision integers, so that building a recurrence never overflows, and give up, with an unknown value, on everything else.
    3. Backedge-taken counts for `for` loops from their endpoints: a constant when both endpoints are constant, otherwise a symbolic expression with its zero-trip case, and correct for inclusive ranges that end at their type's largest value.
    4. A range for each checked value at each check, from its recurrence, the loop's count and the branch conditions that dominate the check.
    5. Removal: delete each bounds or overflow check whose condition is proven true, together with its failure edge. Emit a passed remark per function, such as "removed 6 bounds checks in multiply: proved 0 <= row, column, k < 64", and an analysis remark for each check that stays, naming the missing fact.
    6. No-wrap facts: record that the step of every `for` loop cannot overflow (as `nsw` for `i32` and `nuw` for `u32` and `usize`, if you emit LLVM IR), and index arithmetic only where step 4 proved it.

    **Not yet:** versioning or splitting loops whose bounds are unknown (a stretch goal once removal works); strength reduction (LLVM's LoopStrengthReduce does it if you emit LLVM IR; otherwise it waits for your own back end, [B1](../backend/b1-simplest-backend.md)); recurrences that are not affine; `while` loops beyond the simplest counting form; dependence analysis ([P6](p6-dependence-analysis.md)); anything that reorders floating-point operations. Never turn a check you prove will fail into a compile-time error ([decision 39](../decisions/diagnostics.md#d39)); a warning is allowed.

    **Proof that it works:**

    - Every golden test gives byte-identical output, runtime error line and exit status with the pass on and with it off.
    - A failing-check suite, with bounds and overflow checks that fail in the first iteration, a middle one and the last one, in inner and outer loops, and with an inclusive range ending at its type's largest value. It includes this chapter's program that prints 10, 20, 30 and 40 before its bounds error.
    - A soundness mode: compile the whole test suite with every removed check put back, but with its failure turned into an internal error ("a check proven safe has failed") that exits with a status of its own. The suite must run with no such exit.
    - Checks that must stay: `for i in 0..=64` over 64 elements, `smooth` with 65 elements, `clear_first`, and a check on an index loaded from an array.
    - A table, filled in from your compiler's remarks, with the date and your compiler's version:

    | Function | Checks in the source | Checks after the pass | Checks per iteration of the innermost loop, before and after |
    | --- | --- | --- | --- |
    | stage 10 `multiply`, 64 × 64 | | | |
    | `flatten` | | | |
    | `smooth`, 66 elements | | | |
    | `smooth`, 65 elements | | | |
    | `clear_first` | | | |

    - A measurement under the protocol of [P1](p1-measure-first.md): the time of the 64 × 64 kernel with the pass off and on, on your machine, with the date. If you emit LLVM IR, add the loop vectorizer's remarks for the kernel before and after.

## Key ideas

!!! recap "Questions you can now answer"

    - **What do LLVM's canonical loop forms guarantee?** A preheader, one back edge, dedicated exits, loop-closing phis for values used after the loop and, after rotation, a test at the bottom behind a guard.
    - **What is an add recurrence?** {start,+,step}&lt;L&gt;: a value that starts at `start` in loop L and grows by `step` each iteration; the start may be a recurrence of an outer loop, and the step a recurrence of the same loop.
    - **Why count back edges rather than iterations?** A top-tested loop executes its header once even when its body never runs, and the trip count, one more than the backedge-taken count, can overflow the counter's type.
    - **What proves that a bounds check never fails?** The index's recurrence, the loop's backedge-taken count and the branches that dominate the check, which together bound the index at the check.
    - **What may a compiler do with a check it cannot prove?** Keep it, version the loop behind a test of the whole range, or split the iteration space; never report the failure early.
    - **Why do some checks stop LLVM 18's vectorizer and others do not?** It needs SCEV to count every exit: a check on the loop's own induction variable is countable; an overflow check on loaded data, or a check on a value the loop never changes, is not.
    - **Why must `nsw` come with a proof?** With `nsw`, overflow produces poison instead of a trap, and the optimizer may delete a later test for it.

## Where this comes back

!!! next "You will use this again in"

    - [O9. Memory: alias analysis and MemorySSA](o9-alias-analysis.md): *pointer recurrences*, *loads hoisted into the preheader*
    - [O10. Pass managers and pipelines](o10-pass-pipelines.md): *loop pass manager*, *canonical forms added automatically*, *IRCE outside the default pipeline*
    - [O11. Undefined behavior, poison and correct optimization](o11-undefined-behavior.md): *no-wrap flags*, *poison*, *proof obligations*
    - [O12. Testing an optimizer](o12-testing-optimizers.md): *failing-check suites*, *soundness mode*
    - [P6. Dependence analysis](p6-dependence-analysis.md): *add recurrences*, *affine subscripts*
    - [P7. Loop transformations](p7-loop-transformations.md): *trip counts*, *check-free loop nests*
    - [P10. Vectorization](p10-vectorization.md): *computable trip counts*, *scalar epilogue*
    - [P16. Capstone: the ladder, measured](p16-capstone.md): *scalar cleanup*, *removed checks*
    - [A2. Reading and writing AArch64 assembly](../backend/a2-aarch64-assembly.md): *addressing modes*, *pointer increments*
    - [M6. Loops: affine and scf](../mlir/m6-affine-and-scf.md): *explicit induction variables*, *trip-count formula*
    - [G4. Memory performance: coalescing and bank conflicts](../gpu/g4-memory-performance.md): *address strides*

## Sources and further reading

Read LLVM's Loop Terminology page first: it defines every canonical form in this chapter, with pictures. Then read Absar's slides for scalar evolution as LLVM implements it, and the first two sections of Bachmann, Wang and Zima for the chains of recurrences underneath. Cooper, Simpson and Vick explain strength reduction on SSA form, with the history of the older algorithms. Bodík, Gupta and Sarkar show bounds-check elimination in a language whose failures, like Vortex's, must happen in order. The source headers of IndVarSimplify and InductiveRangeCheckElimination are short and worth reading beside this chapter's examples.

[^llvm-loops]: LLVM Project, "LLVM Loop Terminology (and Canonical Forms)", sections "Important Notes", "Loop Simplify Form", "Loop Closed SSA (LCSSA)" and "Rotated Loops", read on 2026-09-24. <https://llvm.org/docs/LoopTerminology.html>
[^llvm-passes]: LLVM Project, "LLVM's Analysis and Transform Passes", entries `lcssa`, `loop-deletion`, `loop-simplify` and `scalar-evolution`, read on 2026-09-24. <https://llvm.org/docs/Passes.html>
[^scev-cpp]: LLVM Project, `ScalarEvolution.cpp`, release/18.x branch: the file header and its list of references, `isSCEVable`, the comments on `SCEVAddRecExpr::evaluateAtIteration`, `BackedgeTakenInfo::getExact`, and `ScalarEvolution::print`. <https://github.com/llvm/llvm-project/blob/release/18.x/llvm/lib/Analysis/ScalarEvolution.cpp>
[^scev-h]: LLVM Project, `ScalarEvolution.h`, release/18.x branch: the comment on the no-wrap flags of the `SCEV` class, and the comments on `getBackedgeTakenCount`, `getTripCountFromExitCount` and `getSmallConstantTripCount`. <https://github.com/llvm/llvm-project/blob/release/18.x/llvm/include/llvm/Analysis/ScalarEvolution.h>
[^scev-expr]: LLVM Project, `ScalarEvolutionExpressions.h`, release/18.x branch: the comment on `SCEVAddRecExpr`. <https://github.com/llvm/llvm-project/blob/release/18.x/llvm/include/llvm/Analysis/ScalarEvolutionExpressions.h>
[^absar]: Javed Absar (Arm), "Scalar Evolution - Demystified", slides, 2018: the sections on the mathematical framework (induction variables, basic recurrences, chains of recurrences), SCEV rewriting and folding, canonical loops, and loop strength reduction. <https://llvm.org/devmtg/2018-04/slides/Absar-ScalarEvolution.pdf>
[^bwz]: Olaf Bachmann, Paul S. Wang and Eugene V. Zima, "Chains of Recurrences: a Method to Expedite the Evaluation of Closed-form Functions", *Proceedings of the International Symposium on Symbolic and Algebraic Computation (ISSAC '94)*, ACM, 1994: sections 1, 2, 2.1 and 3.2. <https://doi.org/10.1145/190347.190423>
[^osr]: Keith D. Cooper, L. Taylor Simpson and Christopher A. Vick, "Operator Strength Reduction", *ACM Transactions on Programming Languages and Systems* 23(5), 2001: the abstract and sections 2, 3, 4.2 and 5. <https://doi.org/10.1145/504709.504710>
[^abcd]: Rastislav Bodík, Rajiv Gupta and Vivek Sarkar, "ABCD: Eliminating Array Bounds Checks on Demand", *Proceedings of the ACM SIGPLAN Conference on Programming Language Design and Implementation (PLDI 2000)*, 2000: the abstract and sections 1 and 2. <https://doi.org/10.1145/349299.349342>
[^indvars]: LLVM Project, `IndVarSimplify.cpp` (the file header) and `SimplifyIndVar.cpp` (`SimplifyIndvar::eliminateIVComparison`), release/18.x branch. <https://github.com/llvm/llvm-project/blob/release/18.x/llvm/lib/Transforms/Scalar/IndVarSimplify.cpp> and <https://github.com/llvm/llvm-project/blob/release/18.x/llvm/lib/Transforms/Utils/SimplifyIndVar.cpp>
[^lsr]: LLVM Project, `LoopStrengthReduce.cpp` (the file header) and `TargetPassConfig.cpp` (`TargetPassConfig::addIRPasses`), release/18.x branch. <https://github.com/llvm/llvm-project/blob/release/18.x/llvm/lib/Transforms/Scalar/LoopStrengthReduce.cpp> and <https://github.com/llvm/llvm-project/blob/release/18.x/llvm/lib/CodeGen/TargetPassConfig.cpp>
[^irce]: LLVM Project, `InductiveRangeCheckElimination.cpp` (the file header and the comment on the `InductiveRangeCheck` class) and `LoopConstrainer.cpp` (`parseLoopStructure`, and the comment on the loops it adds being slow paths), release/18.x branch. <https://github.com/llvm/llvm-project/blob/release/18.x/llvm/lib/Transforms/Scalar/InductiveRangeCheckElimination.cpp> and <https://github.com/llvm/llvm-project/blob/release/18.x/llvm/lib/Transforms/Utils/LoopConstrainer.cpp>
[^laa]: LLVM Project, `LoopAccessAnalysis.cpp`, release/18.x branch: `LoopAccessInfo::canAnalyzeLoop`. <https://github.com/llvm/llvm-project/blob/release/18.x/llvm/lib/Analysis/LoopAccessAnalysis.cpp>
[^lv]: LLVM Project, `LoopVectorize.cpp`, release/18.x branch: the comment on `requiresScalarEpilogue`. <https://github.com/llvm/llvm-project/blob/release/18.x/llvm/lib/Transforms/Vectorize/LoopVectorize.cpp>
[^llvm-vec]: LLVM Project, "Auto-Vectorization in LLVM", sections "Loops with unknown trip count" and "Early Exit Vectorization", read on 2026-09-24. <https://llvm.org/docs/Vectorizers.html>
[^perftips]: LLVM Project, "Performance Tips for Frontend Authors", sections "Zext GEP indices to machine register width", "Describing Language Specific Properties", "Restricted Operation Semantics" and "Pass Ordering", read on 2026-09-24. <https://llvm.org/docs/Frontend/PerformanceTips.html>
[^langref-add]: LLVM Project, "LLVM Language Reference Manual", section "'add' Instruction", read on 2026-09-24. <https://llvm.org/docs/LangRef.html#add-instruction>
[^langref-sadd]: LLVM Project, "LLVM Language Reference Manual", section "'llvm.sadd.with.overflow.*' Intrinsics", read on 2026-09-24. <https://llvm.org/docs/LangRef.html#llvm-sadd-with-overflow-intrinsics>
[^mlir-scf]: MLIR Project, "'scf' Dialect", operation `scf.for`, read on 2026-09-24. <https://mlir.llvm.org/docs/Dialects/SCFDialect/>

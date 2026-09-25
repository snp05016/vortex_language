# C2. Liveness

<p class="page-intro">A register allocator can give one register to two values only if they are never needed at the same time, and liveness is the analysis that says when a value is still needed. This chapter computes it one instruction at a time, turns it into the live intervals and interference graphs that C3 and C4 consume, and shows the two shortcuts that SSA form allows.</p>

<p class="vx-meta" markdown="1">Level: Intermediate · Reading time: about 40 minutes · Builds on: [C1. Instruction selection](c1-instruction-selection.md), [O4. Dataflow analysis](../optimize/o4-dataflow.md)</p>

???+ remember "Before you start, remember"

    ??? question "What are the two equations of liveness, and in which direction do facts flow?"

        Backward. A block's live-out set is the union of its successors'
        live-in sets, and its live-in set is use ∪ (live-out \ def): what the
        block reads before writing, plus whatever passes through it untouched.

        Introduced in [O4. Dataflow analysis](../optimize/o4-dataflow.md#summaries-and-equations).

    ??? question "In postorder, which blocks does a block come after?"

        After every block the depth-first search reached from it, which
        includes its successors, except a successor still on the search's
        path when the edge is crossed: the target of a retreating edge, the
        edge that closes a loop. That makes postorder the natural order for a
        backward analysis.

        Introduced in [O2. Control-flow graphs and dominance](../optimize/o2-cfg-and-dominance.md#visiting-the-blocks-in-order).

    ??? question "When is an edge a back edge, and what does it tell you?"

        When its target dominates its source. The target is the header of a
        natural loop, and the blocks that reach the source without passing
        through the header form the loop's body.

        Introduced in [O2. Control-flow graphs and dominance](../optimize/o2-cfg-and-dominance.md#loops-from-dominance).

    ??? question "In strict SSA form, where does a definition stand relative to its uses, and where does a phi read its operands?"

        Every definition dominates all its uses. A phi's operand counts as a
        use at the end of the predecessor block it comes from, not in the
        phi's own block.

        Introduced in [O3. SSA form: construction and destruction](../optimize/o3-ssa.md#how-many-phis).

    ??? question "What may a callee do to a caller-saved register?"

        Overwrite it. A caller that needs the value after the call must keep
        it somewhere else: in a callee-saved register or on the stack.

        Introduced in [A4. Calling conventions and ABIs](a4-calling-conventions.md#caller-saved-and-callee-saved-registers).

!!! goals "In this chapter"

    - Compute live-in and live-out sets for every instruction of a function with loops, by hand and in code, and say when the iteration has finished.
    - Decide what an instruction uses and defines once machine details appear: dead results, copies, fixed registers, calls and partial register writes.
    - Build a live interval from liveness rather than from where a name is mentioned, and recognize a lifetime hole.
    - Build an interference graph with the definition rule, and explain why a copy and a dead definition are its two special cases.
    - Explain how strict SSA form lets liveness skip the iterative fixed point, by two passes over a loop-nesting forest or by path exploration.

## Where a register becomes free

After instruction selection ([C1](c1-instruction-selection.md)), a function is a list of machine instructions whose operands are **virtual registers**: names for values, as many as the program needs, that do not yet have a physical register. The register allocator's job is to map those names onto a machine's few real registers. Two values may share a register only if no point of the program needs both, so the allocator's first question is, for every point: which values will still be read later?

A value is **live** at a point if some path from that point reaches a read of the value before any write to it.[^cmu-live] The points that matter are the gaps between instructions. The set of values live immediately before an instruction is its **live-in** set, and the set live immediately after it is its **live-out** set.

[O4](../optimize/o4-dataflow.md#liveness-by-hand) computed these sets for whole blocks. A block-level answer says nothing about the middle of a block, and the middle is where most registers become free: a value's register is available again right after the last instruction that reads it. Allocation needs liveness at the grain of single instructions.

Nothing new is needed to get it. Treat each instruction as a block of its own. Its **use** set is the names it reads, its **def** set the names it writes, and O4's equations apply unchanged: an instruction's live-out is the live-in of whatever runs next, and its live-in is use ∪ (live-out \ def).

In a straight-line block there are no joins and no loops, so one backward pass from the last instruction to the first gives the exact answer.[^cmu-live] `instruction_liveness.cpp` runs that pass over six three-address instructions that compute `(a + b) * (a - b) + a * a`:

--8<-- "includes/examples/backend/c2-liveness/instruction_liveness.cpp.md"

Follow `a` down the table. It is live-in from instruction 0 through instruction 3, because `t3 = a * a` still reads it after the two products in between; after instruction 3 it is dead, and its register is free for `t3`. The same happens to `b` at instruction 1: it is live-in there but not live-out, so `t1` can take `b`'s register, because an instruction reads its operands before it writes its result. The temporaries `t0` and `t1` each live from their definition to the multiply that consumes them.

The number of values live at a point is the **register pressure** there. Its largest value over a function, here 3, is a lower bound on the registers the code needs: if more values are live at one point than there are registers, some of them must be kept in memory at that point.[^ps99]

## Loops: the same equations, repeated

A backward branch breaks the single pass. When the pass reaches the branch, it needs the live-in set of the branch's target, which lies earlier in the program and has not been computed yet in this pass.[^cmu-live] The answer is to repeat: run passes until one pass changes nothing. Because the sets only grow and are bounded by the number of names, the repetition stops, which is the fixed-point argument from [O4](../optimize/o4-dataflow.md#monotone-functions-and-the-fixed-point-theorem).

`loop_liveness.cpp` sums `b` elements of an array whose address arrives in `a`. It visits the instructions from 6 down to 0, which is postorder for this graph, and prints every instruction's live-in set after each pass:

--8<-- "includes/examples/backend/c2-liveness/loop_liveness.cpp.md"

Follow the first pass by hand. `return s` reads `s`, so live-in at 6 is {s}. Instruction 5's successors are 2 and 6, but 2 has not been visited in this pass and its set is still empty, so live-out at 5 is {s} and live-in is {i, b, s}. Instructions 4 and 3 pass `i`, `b` and `s` upward, and 3 adds `t`. Instruction 2 reads `a` and `i` and defines `t`, giving {a, i, b, s}. Instruction 1 defines `i`, and instruction 0 defines `s`, leaving {a, b}: the two parameters.

The first pass got instructions 3, 4 and 5 wrong. The second pass visits 5 again, now sees {a, i, b, s} at instruction 2 across the back edge, and adds `a` to instructions 5, 4 and 3. The third pass changes nothing, so the sets are final.

<figure class="vx-figure">
<svg viewBox="0 0 760 360" role="img" aria-label="Live-in sets of the summation loop after each of three passes, with the cells that change in pass 2 highlighted" aria-describedby="c2-f1-desc">
<title id="c2-f1-title">The live-in table filling in, pass by pass</title>
<desc id="c2-f1-desc">A table with one row per instruction of the summation loop, 0 to 6, and three columns, the live-in set after pass 1, pass 2 and pass 3. An arrow on the left runs from instruction 5 back up to instruction 2: the back edge. In pass 1, instructions 3, 4 and 5 lack the name a. In pass 2 those three cells are highlighted because a has been added: it came across the back edge from instruction 2. Pass 3 repeats pass 2 exactly, which is how the algorithm knows it has finished.</desc>
<text class="vx-text-muted" x="70" y="36">instruction</text>
<text class="vx-text" x="400" y="36" text-anchor="middle">pass 1</text>
<text class="vx-text" x="540" y="36" text-anchor="middle">pass 2</text>
<text class="vx-text" x="680" y="36" text-anchor="middle">pass 3</text>
<text class="vx-mono" x="70" y="72">0  s = 0</text>
<text class="vx-mono" x="70" y="112">1  i = 0</text>
<text class="vx-mono" x="70" y="152">2  t = load a[i]</text>
<text class="vx-mono" x="70" y="192">3  s = s + t</text>
<text class="vx-mono" x="70" y="232">4  i = i + 1</text>
<text class="vx-mono" x="70" y="272">5  if i &lt; b goto 2</text>
<text class="vx-mono" x="70" y="312">6  return s</text>
<path class="vx-line" d="M62 268 L36 268 L36 148 L54 148"/>
<polygon class="vx-arrowhead" points="62,148 50,142 50,154"/>
<text class="vx-text-muted" x="30" y="214" text-anchor="middle" transform="rotate(-90 30 214)">back edge</text>
<g>
<rect class="vx-box" x="340" y="52" width="120" height="28" rx="4"/>
<rect class="vx-box" x="340" y="92" width="120" height="28" rx="4"/>
<rect class="vx-box" x="340" y="132" width="120" height="28" rx="4"/>
<rect class="vx-box" x="340" y="172" width="120" height="28" rx="4"/>
<rect class="vx-box" x="340" y="212" width="120" height="28" rx="4"/>
<rect class="vx-box" x="340" y="252" width="120" height="28" rx="4"/>
<rect class="vx-box" x="340" y="292" width="120" height="28" rx="4"/>
<text class="vx-mono" x="400" y="71" text-anchor="middle">{a, b}</text>
<text class="vx-mono" x="400" y="111" text-anchor="middle">{a, b, s}</text>
<text class="vx-mono" x="400" y="151" text-anchor="middle">{a, i, b, s}</text>
<text class="vx-mono" x="400" y="191" text-anchor="middle">{i, b, s, t}</text>
<text class="vx-mono" x="400" y="231" text-anchor="middle">{i, b, s}</text>
<text class="vx-mono" x="400" y="271" text-anchor="middle">{i, b, s}</text>
<text class="vx-mono" x="400" y="311" text-anchor="middle">{s}</text>
</g>
<g class="vx-seq" style="--vx-i: 0; --vx-n: 2">
<rect class="vx-box" x="480" y="52" width="120" height="28" rx="4"/>
<rect class="vx-box" x="480" y="92" width="120" height="28" rx="4"/>
<rect class="vx-box" x="480" y="132" width="120" height="28" rx="4"/>
<rect class="vx-box-accent" x="480" y="172" width="120" height="28" rx="4"/>
<rect class="vx-box-accent" x="480" y="212" width="120" height="28" rx="4"/>
<rect class="vx-box-accent" x="480" y="252" width="120" height="28" rx="4"/>
<rect class="vx-box" x="480" y="292" width="120" height="28" rx="4"/>
<text class="vx-mono" x="540" y="71" text-anchor="middle">{a, b}</text>
<text class="vx-mono" x="540" y="111" text-anchor="middle">{a, b, s}</text>
<text class="vx-mono" x="540" y="151" text-anchor="middle">{a, i, b, s}</text>
<text class="vx-mono" x="540" y="191" text-anchor="middle">{a, i, b, s, t}</text>
<text class="vx-mono" x="540" y="231" text-anchor="middle">{a, i, b, s}</text>
<text class="vx-mono" x="540" y="271" text-anchor="middle">{a, i, b, s}</text>
<text class="vx-mono" x="540" y="311" text-anchor="middle">{s}</text>
</g>
<g class="vx-seq" style="--vx-i: 1; --vx-n: 2">
<rect class="vx-box" x="620" y="52" width="120" height="28" rx="4"/>
<rect class="vx-box" x="620" y="92" width="120" height="28" rx="4"/>
<rect class="vx-box" x="620" y="132" width="120" height="28" rx="4"/>
<rect class="vx-box" x="620" y="172" width="120" height="28" rx="4"/>
<rect class="vx-box" x="620" y="212" width="120" height="28" rx="4"/>
<rect class="vx-box" x="620" y="252" width="120" height="28" rx="4"/>
<rect class="vx-box" x="620" y="292" width="120" height="28" rx="4"/>
<text class="vx-mono" x="680" y="71" text-anchor="middle">{a, b}</text>
<text class="vx-mono" x="680" y="111" text-anchor="middle">{a, b, s}</text>
<text class="vx-mono" x="680" y="151" text-anchor="middle">{a, i, b, s}</text>
<text class="vx-mono" x="680" y="191" text-anchor="middle">{a, i, b, s, t}</text>
<text class="vx-mono" x="680" y="231" text-anchor="middle">{a, i, b, s}</text>
<text class="vx-mono" x="680" y="271" text-anchor="middle">{a, i, b, s}</text>
<text class="vx-mono" x="680" y="311" text-anchor="middle">{s}</text>
</g>
<text class="vx-text-accent" x="540" y="344" text-anchor="middle">a arrives across the back edge</text>
<text class="vx-text-muted" x="680" y="344" text-anchor="middle">no change: done</text>
</svg>
<figcaption>Figure 1. The live-in sets of loop_liveness.cpp after each pass. The highlighted cells change in pass 2, when the array address a, read only at instruction 2, reaches the end of the loop through the back edge. Pass 3 changes nothing, and that unchanged pass is what ends the iteration.</figcaption>
</figure>

The loop teaches the chapter's most important fact about loops. The array address `a` is read at one instruction, yet it is live at every instruction of the loop, because the next iteration will read it again. The bound `b` behaves the same way. Values defined before a loop and read inside it, such as array bases, bounds and an accumulator, stay live around the whole loop, and they are what fills the registers in a kernel's inner loop.

Postorder lets each pass see an instruction's successors first, except across back edges. Facts that must travel around a loop still need another pass, and in the worst case one more pass for each level of loop nesting.[^ssabook] [O4](../optimize/o4-dataflow.md#doing-less-work-order-and-worklists) showed how a worklist avoids revisiting instructions whose successors did not change.

Liveness computed this way is **conservative**: it can call a value live when no run of the program will read it, because it assumes every branch can go either way. The CMU notes give a branch whose condition is always false and whose target still makes a value live.[^cmu-live] For register allocation that direction of error is safe: an extra live value can cost a register, never a wrong result.

??? check "In pass 1, `b` is already in instruction 3's live-in set, but `a` is not. What is different about how each one reaches instruction 3?"

    `b` is read at instruction 5, and 5 is visited before 3 in the same pass,
    so the fall-through edges 3 → 4 → 5 carry it up at once. `a` is read
    only at instruction 2, which comes before 3 in the program. Its only way
    to reach 3 is around the loop: 2 is visited last in a pass, so the back
    edge 5 → 2 can hand `a` to instruction 5 only in the next pass.

## What an instruction uses and defines

The examples so far used tidy three-address code. Real instructions after selection raise questions that the equations do not answer by themselves. Each has to be settled in the use and def sets before the analysis runs.

**A result nobody reads.** An instruction that defines a value no later instruction reads has a **dead definition**. The value is not live anywhere, yet the instruction still writes a register when it runs. The CMU notes make the point with such a definition: it still conflicts with every value live after it, or it might be put in their register and destroy them.[^cmu-live] LLVM's code generator records, for each instruction, the registers that are dead right after it for this reason,[^llvm-cg] and those are the `dead` flags [E1](e1-llvm-codegen-pipeline.md) pointed out in machine IR.

**Reading and writing one name.** `i = i + 1` both uses and defines `i`. The use wins for live-in: `i` is live before the instruction because the instruction reads it. Most x86-64 arithmetic is **destructive**: one operand is both an input and the destination ([B2](b2-x86-64.md#one-subtraction-two-instruction-shapes)), so almost every selected instruction has this shape.

**Fixed registers.** Some operands are not virtual registers at all. Arguments arrive in particular registers, a function returns its result in one, and some instructions insist on specific registers, as x86-64 division does ([B2](b2-x86-64.md#division-fixed-registers-and-a-trap)). These physical registers take part in liveness like any other name: in the CMU notes, the return register is live after the function's last instruction, and it appears as a node in the interference graph with a color already chosen.[^cmu-ra]

**Calls.** A call reads its argument registers. By the definition in [A4](a4-calling-conventions.md#caller-saved-and-callee-saved-registers), it may also overwrite every caller-saved register, so a sound model treats a call as defining all of them. Then every value live across the call conflicts with every caller-saved register, and the allocator must put it in a callee-saved register or in memory. A call that never returns, such as Vortex's runtime-error report, has no successor at all, so nothing is live after it.

**Part of a register.** On AArch64, writing a `w` register sets the upper 32 bits of the matching `x` register to zero ([A2](a2-aarch64-assembly.md#registers-by-name)), so a write to `w0` defines all of `x0`. On x86-64, writing `eax` zeroes the rest of `rax` in the same way, but writing `al` or `ax` leaves the other bits alone ([B2](b2-x86-64.md#registers-by-another-name)). Such a **partial definition** must count as a use of the old value as well as a definition, because the bits it did not write still hold that value.

??? check "A loop body calls a function that returns normally. Which of the loop's values does the call force out of caller-saved registers?"

    Exactly those live across the call: live-out of the call instruction and
    not defined by it. A value whose last use is an argument of the call is
    not among them, because it dies at the call. Values that are live-in at
    the loop header, such as array bases and bounds, are live across any
    call inside the loop.

## Live ranges and live intervals

A value's **live range** is the set of points where it is live. It can have gaps and can wrap around loops, so it is awkward to store and to compare. Poletto and Sarkar's linear scan allocator ([C3](c3-linear-scan.md)) replaces it with something simpler. Number the instructions in some order; a **live interval** for a value is a range of numbers `[i, j]` such that the value is live at no instruction before `i` and at no instruction after `j`.[^ps99] Two intervals overlap exactly when they share a number, which one comparison decides.

The definition is written in terms of liveness, and the loop shows why. `loop_liveness.cpp` prints each name's interval from liveness next to the span from its first mention to its last. For `a` the two disagree badly: the mentions say `[2, 2]`, but liveness says `[0, 5]`, because `a` is live around the loop. An allocator that used the mention span would give `a`'s register to `t` at instruction 3 and read garbage as the array address on the next iteration.

An interval is conservative: the value may be dead somewhere inside it.[^ps99] Such a gap is a **lifetime hole**. Wimmer and Mössenböck store an interval as a list of disjoint ranges and call the space between two ranges a lifetime hole.[^wimmer] `live_intervals.cpp` produces one on purpose: the name `x` is used for two unrelated values, so it is dead at instruction 2 while its single-range interval covers it.

--8<-- "includes/examples/backend/c2-liveness/live_intervals.cpp.md"

<figure class="vx-figure">
<svg viewBox="0 0 760 300" role="img" aria-label="Live intervals for the names w, x, y and z from live_intervals.cpp, over instructions 0 to 6, with a lifetime hole in x at instruction 2" aria-describedby="c2-f2-desc">
<title id="c2-f2-title">An interval can claim more than the exact live range</title>
<desc id="c2-f2-desc">A grid of seven instruction columns, 0 to 6, with one row per name. w's bar is solid from instruction 4 to 6. x has two solid segments, 0 to 1 and 3 to 4, with a dashed box at instruction 2: the lifetime hole, where x's single interval claims a register that x does not need. y's bar runs from 1 to 5, and z's from 2 to 4. A dashed outline around column 2 notes that only y and z are live there, while three intervals cover it.</desc>
<text class="vx-text-muted" x="30" y="24">instruction</text>
<text class="vx-text" x="160" y="24" text-anchor="middle">0</text>
<text class="vx-text" x="240" y="24" text-anchor="middle">1</text>
<text class="vx-text" x="320" y="24" text-anchor="middle">2</text>
<text class="vx-text" x="400" y="24" text-anchor="middle">3</text>
<text class="vx-text" x="480" y="24" text-anchor="middle">4</text>
<text class="vx-text" x="560" y="24" text-anchor="middle">5</text>
<text class="vx-text" x="640" y="24" text-anchor="middle">6</text>
<line class="vx-line" x1="120" y1="34" x2="120" y2="256"/>
<line class="vx-line" x1="200" y1="34" x2="200" y2="256"/>
<line class="vx-line" x1="280" y1="34" x2="280" y2="256"/>
<line class="vx-line" x1="360" y1="34" x2="360" y2="256"/>
<line class="vx-line" x1="440" y1="34" x2="440" y2="256"/>
<line class="vx-line" x1="520" y1="34" x2="520" y2="256"/>
<line class="vx-line" x1="600" y1="34" x2="600" y2="256"/>
<line class="vx-line" x1="680" y1="34" x2="680" y2="256"/>
<rect class="vx-box-bad" x="283" y="38" width="74" height="216" fill-opacity="0"/>
<text class="vx-mono" x="60" y="88">w</text>
<text class="vx-mono" x="60" y="138">x</text>
<text class="vx-mono" x="60" y="188">y</text>
<text class="vx-mono" x="60" y="238">z</text>
<rect class="vx-box-accent" x="444" y="70" width="232" height="26" rx="4"/>
<rect class="vx-box-accent" x="124" y="120" width="152" height="26" rx="4"/>
<rect class="vx-box-bad" x="286" y="120" width="68" height="26" rx="4"/>
<rect class="vx-box-accent" x="364" y="120" width="152" height="26" rx="4"/>
<rect class="vx-box-accent" x="204" y="170" width="392" height="26" rx="4"/>
<rect class="vx-box-accent" x="284" y="220" width="232" height="26" rx="4"/>
<text class="vx-text-muted" x="320" y="138" text-anchor="middle">hole</text>
<text class="vx-text-muted" x="320" y="280" text-anchor="middle">at instruction 2: two names live, three intervals</text>
</svg>
<figcaption>Figure 2. The intervals of live_intervals.cpp. Solid bars are where each name is live; the dashed box in x's row is a lifetime hole, a point inside x's interval where x holds nothing anyone will read.</figcaption>
</figure>

At instruction 2, only `y` and `z` are live, but three intervals cover it. An allocator that sees only single-range intervals treats that point as needing one register more than it does. Two remedies exist, and later chapters use both. Keep the hole in the representation, as Wimmer and Mössenböck do, so another value can use the register during the gap.[^wimmer] Or rename: the two uses of `x` are unrelated values, and SSA form ([O3](../optimize/o3-ssa.md)) would have given them two names and two short intervals.

Renaming does not remove every hole, because holes also come from the instruction order. Suppose a value is defined before an `if`, read in the `else` branch and nowhere else, and the compiler lays out the `then` branch between them. The value is dead throughout the `then` branch, yet an interval over that numbering covers it. Poletto and Sarkar point out that the numbering order changes how accurate intervals are.[^ps99] [C3](c3-linear-scan.md) chooses an order, and [C5](c5-spilling.md) splits intervals at holes.

## Interference

Two values **interfere** when one of them is written at a point where the other is live, which is what forbids giving them one register: the write would destroy the value still needed.[^cmu-ra] An **interference graph** has one node per value and an edge for each pair that interferes. [C4](c4-graph-coloring.md) colors it, one color per physical register.

The CMU notes build the graph with one rule per instruction.[^cmu-ra] For an instruction that defines `t`, add an edge between `t` and every other value live after the instruction. If the instruction is a copy `t = s`, leave out the edge between `t` and `s`: right after the copy both hold the same value, so one register can serve both, and the copy then disappears. Removing copies this way is **coalescing**, which [C4](c4-graph-coloring.md) covers.

`interference.cpp` applies the rule to six instructions with one copy and one dead definition, and compares it with a shortcut that sounds equivalent: two values interfere when both are live-in at the same instruction.

--8<-- "includes/examples/backend/c2-liveness/interference.cpp.md"

<figure class="vx-figure">
<svg viewBox="0 0 760 290" role="img" aria-label="Interference graph built by the definition rule for interference.cpp" aria-describedby="c2-f3-desc">
<title id="c2-f3-title">Two special cases of the interference rule</title>
<desc id="c2-f3-desc">Six nodes, one per name: p, a, c, b, d and e. Solid edges join b and c, b and d, and c and d. A dashed line between a and c is labelled "copy: no edge", because c is a copy of a. Node d is highlighted: it is never read, yet it has two edges, because its definition at instruction 3 writes a register while b and c are live. Nodes p and e have no edges.</desc>
<line class="vx-line" x1="130" y1="80" x2="290" y2="80" stroke-dasharray="6 5"/>
<text class="vx-text-muted" x="210" y="68" text-anchor="middle">copy: no edge</text>
<line class="vx-line" x1="310" y1="100" x2="310" y2="190"/>
<line class="vx-line" x1="330" y1="88" x2="452" y2="140"/>
<line class="vx-line" x1="330" y1="202" x2="452" y2="160"/>
<circle class="vx-box" cx="110" cy="80" r="22"/>
<text class="vx-mono" x="110" y="85" text-anchor="middle">a</text>
<circle class="vx-box" cx="310" cy="80" r="22"/>
<text class="vx-mono" x="310" y="85" text-anchor="middle">c</text>
<circle class="vx-box" cx="310" cy="210" r="22"/>
<text class="vx-mono" x="310" y="215" text-anchor="middle">b</text>
<circle class="vx-box-accent" cx="470" cy="150" r="22"/>
<text class="vx-mono" x="470" y="155" text-anchor="middle">d</text>
<circle class="vx-box" cx="110" cy="210" r="22"/>
<text class="vx-mono" x="110" y="215" text-anchor="middle">p</text>
<circle class="vx-box" cx="640" cy="150" r="22"/>
<text class="vx-mono" x="640" y="155" text-anchor="middle">e</text>
<text class="vx-text-accent" x="470" y="200" text-anchor="middle">never read,</text>
<text class="vx-text-accent" x="470" y="218" text-anchor="middle">still written</text>
<text class="vx-text-muted" x="380" y="262" text-anchor="middle">the live-in shortcut would draw a–c and b–c, and miss d–b and d–c</text>
</svg>
<figcaption>Figure 3. The interference graph of interference.cpp by the definition rule. The copy leaves a and c free to share a register, and the dead definition d still conflicts with the two values live across it.</figcaption>
</figure>

The shortcut is wrong in both directions. It adds the edge `a`-`c`, because both are live-in at instruction 2, which forbids the sharing that would delete the copy. Worse, it misses `d`'s edges: `d` is never live-in anywhere, so the shortcut lets it share a register with `b` or `c`, and writing `d` at instruction 3 would then destroy a value that instruction 4 reads. The first mistake costs a copy; the second produces a wrong program.

Physical registers join the same graph. A fixed register is a node whose color is decided in advance,[^cmu-ra] and a call, modeled as defining every caller-saved register, gets an edge from each of them to every value live across it. After this, the graph holds everything the allocator must respect.

??? check "Suppose the copy at instruction 1 were `c = a + 0` instead. What would change in the graph, and in the code the allocator can produce?"

    It is no longer a copy, so the exception does not apply: `c` gets an
    edge to `a`, which is live after instruction 1. `a` and `c` now need two
    registers, and the addition stays in the code. The values are equal, but
    the rule only recognizes the copy instruction; spotting that `a + 0`
    equals `a` is a job for the optimizer before this point.

## What SSA form buys

Everything so far used the iterative fixed point, and for code in no particular form that is the general method. Strict SSA form offers a shortcut, because every definition dominates all its uses. Brandner, Boissinot, Darte, Dupont de Dinechin and Rastello show two ways to use that property to compute block live-in and live-out sets without iterating to a fixed point.[^brandner]

The first needs one more structure. A **loop-nesting forest** records a function's loops, each loop's header, and which loops sit inside which; [O2](../optimize/o2-cfg-and-dominance.md#for-vortex) asked you to build one. The **two-pass method** runs one postorder pass that ignores back edges, computing partial sets. A second pass walks the loop-nesting forest and adds the header's live-in set, minus the values its phis define, to every block inside the loop.[^brandner] The partial sets can only miss values that travel around a loop, and in strict SSA a value live at a loop's header and defined outside the loop is live throughout the loop, which is exactly what the second pass adds.[^ssabook]

The second method is **path exploration**. Take one variable at a time. From each block where it is used, walk backward along predecessor edges, marking each block as live-in for the variable, and stop at the block that defines it or at a block already marked.[^ssabook] Dominance guarantees that every backward path from a use reaches the definition, so each walk ends by itself and no pass has to ask whether anything changed. The report credits the idea to Appel's textbook and says LLVM used it.[^brandner]

`path_exploration.cpp` computes the live-in sets of a small SSA loop all three ways. `x0` is defined before the loop and read only in `neg`; `i1` and `acc1` are phi results in the header.

--8<-- "includes/examples/backend/c2-liveness/path_exploration.cpp.md"

<figure class="vx-figure">
<svg viewBox="0 0 760 400" role="img" aria-label="The SSA loop from path_exploration.cpp, with the backward walk for x0 and the loop body that the two-pass method fills in" aria-describedby="c2-f4-desc">
<title id="c2-f4-title">Two ways to find where x0 is live</title>
<desc id="c2-f4-desc">Six blocks: entry at the top, then header, then pos on the left and neg on the right, then latch, then exit. A back edge runs from latch up the left side to header. A shaded region covers the loop: header, pos, neg and latch. A dashed moving line traces the walk for x0: it starts at neg, where x0 is read, goes up to header, from header back along the back edge to latch, and from latch up to pos. It stops at entry, which defines x0. The walk marks header, neg, latch and pos. Pass 1 of the two-pass method finds x0 live-in only at header and neg; pass 2 copies the header's set into pos and latch.</desc>
<rect class="vx-box" x="150" y="82" width="480" height="236" rx="10" fill-opacity="0.5"/>
<text class="vx-text-muted" x="620" y="104" text-anchor="end">loop: pass 2 copies header's set here</text>
<rect class="vx-box-strong" x="305" y="20" width="150" height="40" rx="4"/>
<text class="vx-mono" x="380" y="45" text-anchor="middle">entry: x0 = ...</text>
<rect class="vx-box-strong" x="305" y="120" width="150" height="40" rx="4"/>
<text class="vx-mono" x="380" y="145" text-anchor="middle">header: phis</text>
<rect class="vx-box-strong" x="175" y="190" width="150" height="40" rx="4"/>
<text class="vx-mono" x="250" y="215" text-anchor="middle">pos</text>
<rect class="vx-box-accent" x="435" y="190" width="150" height="40" rx="4"/>
<text class="vx-mono" x="510" y="215" text-anchor="middle">neg: use(x0)</text>
<rect class="vx-box-strong" x="305" y="262" width="150" height="40" rx="4"/>
<text class="vx-mono" x="380" y="287" text-anchor="middle">latch</text>
<rect class="vx-box-strong" x="305" y="340" width="150" height="40" rx="4"/>
<text class="vx-mono" x="380" y="365" text-anchor="middle">exit</text>
<line class="vx-line" x1="380" y1="60" x2="380" y2="112"/>
<polygon class="vx-arrowhead" points="380,120 374,108 386,108"/>
<line class="vx-line" x1="340" y1="160" x2="274" y2="184"/>
<line class="vx-line" x1="420" y1="160" x2="486" y2="184"/>
<line class="vx-line" x1="250" y1="230" x2="336" y2="258"/>
<line class="vx-line" x1="510" y1="230" x2="424" y2="258"/>
<line class="vx-line" x1="380" y1="302" x2="380" y2="332"/>
<polygon class="vx-arrowhead" points="380,340 374,328 386,328"/>
<path class="vx-line" d="M305 282 L120 282 L120 140 L297 140"/>
<polygon class="vx-arrowhead" points="305,140 293,134 293,146"/>
<text class="vx-text-muted" x="112" y="214" text-anchor="middle" transform="rotate(-90 112 214)">back edge</text>
<path class="vx-flow" d="M500 190 L440 156"/>
<path class="vx-flow" d="M330 150 L134 150 L134 272 L300 272"/>
<path class="vx-flow" d="M330 262 L262 234"/>
<text class="vx-text-accent" x="470" y="80" text-anchor="start">walk stops: entry defines x0</text>
</svg>
<figcaption>Figure 4. Finding where x0 is live in path_exploration.cpp's loop. The dashed walk starts at neg, which reads x0, and moves against the edges until it reaches the definition in entry; every block it passes is live-in. The two-pass method gets the same answer differently: its first pass, ignoring the back edge, marks only header and neg, and its second pass copies the header's set into the rest of the shaded loop.</figcaption>
</figure>

The columns show both mechanisms at work. The first two-pass column misses `x0` in `pos` and `latch`: without the back edge, no forward path leads from those blocks to `neg`. The second pass adds it, because `x0` is live-in at the loop header and defined outside the loop. Path exploration reaches the same blocks by walking from `neg` through the header and around the back edge. The fixed point needed three passes; the other two methods have no pass count at all.

These methods trade generality for speed. They need strict SSA form, and the two-pass method as described needs a reducible graph; the SSA book extends it to irreducible graphs with a small change to the traversal.[^ssabook] The report measured the methods on the SPECINT 2000 benchmarks. All the proposed algorithms beat the standard dataflow approach, by a factor of 2 on average; the loop-forest method was fastest with bitsets on optimized programs, by 43% on average over the next best.[^brandner]

The same chapter of the SSA book describes a third use of these properties, a **liveness check**: instead of computing sets, answer the query "is `v` live at point `q`?" from precomputed facts about the graph alone. Those facts stay valid when variables are added or removed.[^ssabook] A pass that rewrites code as it goes can keep asking questions without recomputing sets after every change.

Whether you can use any of this depends on when you allocate. After SSA destruction ([O3](../optimize/o3-ssa.md#leaving-ssa-form)), copies give names several definitions, and the dominance property is gone. LLVM's code generator documentation describes this split: live variable analysis uses SSA form to compute lifetimes of virtual registers sparsely, and a separate local analysis handles physical registers within each block.[^llvm-cg]

### Your turn: the kernel's `k` loop

[O3](../optimize/o3-ssa.md#your-turn-the-kernel-in-ssa-form) put the inner loop of the stage 10 kernel into SSA form, with blocks P (before the loop), H (header), K1 and K2 (the two bounds checks), K3 (the arithmetic), S (the step), X (after the loop) and R (the error report):

```text
P:   sum0 = 0.0
     k0 = 0
     goto H
H:   k1 = phi(P: k0, S: k2)
     sum1 = phi(P: sum0, S: sum2)
     if k1 < 64 goto K1 else X
K1:  check a[row, k1]; a failure goes to R
K2:  check b[k1, column]; a failure goes to R
K3:  sum2 = sum1 + a[row, k1] * b[k1, column]
S:   k2 = k1 + 1
     goto H
X:   c[row, column] = sum1
```

`a`, `b`, `c`, `row` and `column` are defined before P. Treat R's report as reading only constants. Half of the answer is here: `k1` is live-in at K1, K2, K3 and S, and nowhere else; `k0` and `sum0` are live-in nowhere, because their only uses are phi operands at the end of P. Work out the rest before opening the answer.

??? check "Where are `sum1`, `sum2`, `k2`, `c` and `row` live-in, and what is the register pressure at K3?"

    - `sum1` is defined in H and read in K3 and X, so it is live-in at K1,
      K2, K3 and X. It is not live-in at S: after K3 nothing reads it.
    - `sum2` is defined in K3, and its only use is the phi operand at the
      end of S, so it is live-in at S only.
    - `k2` is defined in S and read only by the phi at the end of S: it is
      live-in nowhere.
    - `c` is read only in X, but X is reachable from H on every iteration,
      so `c` is live-in at P, H, K1, K2, K3, S and X. `row` is read in K1,
      K3 and X, so it is live-in at the same blocks.
    - At K3's entry, `a`, `b`, `c`, `row`, `column`, `k1` and `sum1` are
      live: seven values before any temporary for the loads and the
      multiply. `sum1` and `sum2` are never live-in at the same block,
      which is why the copies O3's destruction adds for them can be
      coalesced away.

## For Vortex

!!! vortex "Exercise"

    **Add a liveness analysis to your back end and a dump that shows it.**
    It runs on your machine-level code after instruction selection and
    before any register allocation, over virtual registers, and computes
    live-in and live-out for every instruction with the iterative method.
    Settle the use and def sets first: fixed argument and return
    registers, calls (with your target's caller-saved registers, from
    [A4](a4-calling-conventions.md#caller-saved-and-callee-saved-registers)),
    dead definitions, and any partial register writes your selector emits.
    Add a second, independent implementation: path exploration on your SSA
    form if you allocate before destruction, or a deliberately naive
    version (every instruction revisited until nothing changes, no
    postorder) if you do not.

    **Test it three ways.**

    1. Compile the [stage 10](../compiler/guide/stage-10-matrix-multiplication.md#the-program-the-milestone-asks-for)
       program and dump the liveness of `multiply`: one row per
       instruction, with its live-in and live-out sets and the live
       interval of every virtual register computed from them. Check the
       inner loop by hand.

       The base addresses of `a`, `b` and `c` must be live at every
       instruction of the `k` loop, not only where they are read, and the
       call that reports a failed bounds check must have an empty live-out
       set. Write down which values are live into that call and why.
    2. Generate a few hundred small random functions with loops and
       branches, and check that the two implementations agree on every set.
    3. Keep the dump for `multiply` as a reviewed reference file, so a
       change in the analysis shows up as a difference in review.

    **Not yet.** Do not assign registers, build an interference graph or
    split intervals: [C3](c3-linear-scan.md), [C4](c4-graph-coloring.md) and
    [C5](c5-spilling.md) decide how those should look, and they depend on
    the representation this exercise settles. Do not write the two-pass
    loop-forest method until a profile shows liveness to be slow.

## Key ideas

!!! recap "You can now answer"

    - **Why does register allocation need liveness per instruction rather than per block?** A register becomes free after the last instruction that reads its value, which is usually in the middle of a block.
    - **How do you know the iterative analysis has finished?** A whole pass over the instructions changes no set; a back edge costs about one extra pass.
    - **Why must a live interval come from liveness rather than from where a name is mentioned?** A value used inside a loop is live around the whole loop, beyond its last textual use.
    - **What is a lifetime hole?** A point inside a value's interval where the value is not live, left by name reuse or by the order of the instructions.
    - **What is the rule for building an interference graph?** Each definition interferes with every other value live after it, except the source of a copy.
    - **Why does a dead definition still interfere?** It writes a register when it runs, so it must not share one with a value that is still live.
    - **What does strict SSA form let liveness skip, and how?** The iterative fixed point: two passes, the second over the loop-nesting forest, or a backward walk from each use that stops at the definition.

## Where this comes back

!!! next "You will use this again in"

    - [C3. Register allocation I: linear scan](c3-linear-scan.md): *live intervals*, *lifetime holes*, *instruction order*
    - [C4. Register allocation II: graphs and SSA](c4-graph-coloring.md): *the interference graph*, *coalescing a copy*, *precolored registers*
    - [C5. Spilling, splitting and rematerialization](c5-spilling.md): *splitting an interval*, *values live across a loop*
    - [E3. LLVM's allocator, scheduler and MC layer](e3-llvm-allocator-scheduler-mc.md): *live ranges*, *dead definitions*

## Sources and further reading

[^cmu-live]: Frank Pfenning and André Platzer, "Liveness Analysis", lecture notes 4, 15-411 Compiler Design, Carnegie Mellon University, 2013. <https://www.cs.cmu.edu/~fp/courses/15411-f13/lectures/04-liveness.pdf>
[^cmu-ra]: Frank Pfenning and André Platzer, "Register Allocation", lecture notes 3, 15-411 Compiler Design, Carnegie Mellon University, 2013, sections 2 and 9. <https://www.cs.cmu.edu/~fp/courses/15411-f13/lectures/03-regalloc.pdf>
[^ps99]: Massimiliano Poletto and Vivek Sarkar, "Linear Scan Register Allocation", ACM TOPLAS 21(5), 1999, sections 3 and 4. <https://doi.org/10.1145/330249.330250>
[^wimmer]: Christian Wimmer and Hanspeter Mössenböck, "Optimized Interval Splitting in a Linear Scan Register Allocator", VEE 2005. <https://doi.org/10.1145/1064979.1064998>
[^brandner]: Florian Brandner, Benoit Boissinot, Alain Darte, Benoît Dupont de Dinechin and Fabrice Rastello, "Computing Liveness Sets for SSA-Form Programs", INRIA research report RR-7503, 2011. <https://inria.hal.science/inria-00558509>
[^ssabook]: Benoit Boissinot and Fabrice Rastello, "Liveness", in Fabrice Rastello and Florent Bouchez Tichadou (eds.), *SSA-based Compiler Design*, Springer, 2022; sections 9.1 to 9.4 of the pre-publication draft. Draft (unofficial mirror): <https://pfalcon.github.io/ssabook/latest/>
[^llvm-cg]: LLVM Project, "The LLVM Target-Independent Code Generator", section "Live Intervals". <https://llvm.org/docs/CodeGenerator.html>

Andrew Appel's *Modern Compiler Implementation* (Cambridge University Press, 1998) gives the classical presentation of liveness in its chapter "Liveness Analysis", which the CMU notes point to. <https://www.cs.princeton.edu/~appel/modern/toc.html>

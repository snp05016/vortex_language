# O4. Dataflow analysis

<p class="page-intro">Most optimizations rely on a fact that holds at one point of a function whichever path the program takes: this value is never read again, that variable is always 1, this index is always below 64. This chapter builds the general machine that computes such facts (a lattice of facts, a transfer function for each block and an iteration that stops at a fixed point) and aims it at the Vortex kernel, whose fixed-shape arrays hand an analysis the loop bounds it needs.</p>

<p class="vx-meta" markdown="1">Level: Intermediate · Reading time: about 45 minutes · Builds on: [O2. Control-flow graphs and dominance](o2-cfg-and-dominance.md)</p>

???+ remember "Before you start, remember"

    ??? question "In reverse postorder, which predecessors of a block can come after it?"

        Only those that reach it along a retreating edge, an edge back to a block still on the depth-first search's current path.

        Introduced in [O2. Control-flow graphs and dominance](o2-cfg-and-dominance.md#visiting-the-blocks-in-order).

    ??? question "Why did the iterative dominator computation start every set full, except the entry's?"

        A loop header depends on blocks the first pass has not reached yet. A full set can only shrink, and it stops at the answer the definition gives.

        Introduced in [O2. Control-flow graphs and dominance](o2-cfg-and-dominance.md#computing-dominators).

    ??? question "What is the loop connectedness d(G) of a graph?"

        The largest number of retreating edges on any path that repeats no block. It is 1 for the stage 7 loop.

        Introduced in [O2. Control-flow graphs and dominance](o2-cfg-and-dominance.md#computing-dominators).

    ??? question "Does decision 56 cover floating-point values the compiler computes while compiling?"

        Yes. Each `f32` and `f64` operation gives the IEEE 754 result, rounded to nearest with ties to even, with no contraction, reordering or wider format, and that includes values computed during compilation.

        Introduced in [Numbers, decision 56](../decisions/numbers.md#d56).

    ??? question "When may a Vortex compiler drop a bounds check?"

        Only after proving the access safe. Keeping every check is always correct, and an index that is not a constant expression is never rejected at compile time, even when it provably fails.

        Introduced in [Build v0.1, stage 9](../compiler/guide/stage-9-runtime-safety.md#when-a-check-can-be-skipped) and [Diagnostics, decision 39](../decisions/diagnostics.md#d39).

!!! goals "In this chapter"

    - Set up a dataflow problem: its facts, its direction, a transfer function for each block, the way facts combine where paths meet, and a starting value.
    - Solve it by round-robin iteration and with a worklist, and explain with lattice theory why the iteration stops and why its answer does not depend on the order.
    - Compute live variables, reaching definitions and constants by hand on small graphs.
    - Explain what a fixed-point answer can lose against the answer over all paths, and why an optimistic start finds more.
    - Bound a loop index with intervals, branch conditions and widening, and connect the result to Vortex's bounds checks.

## Facts at every point

[O2](o2-cfg-and-dominance.md) asked which blocks every path to a block must pass through. Most optimizations ask about values instead: what holds at one point, whichever path led there or will leave from there. Here are three such questions about the stage 10 kernel's inner loop:

- Which variables will be read again? The others can give up their registers ([C3](../backend/c3-linear-scan.md)).
- Does a variable hold the same constant on every path? Then its uses can become that constant ([O5](o5-constants-and-dead-code.md)).
- Is `k` between 0 and 63 whenever control reaches `a[row, k]`? Then the bounds check there never fails ([O8](o8-loops.md)).

Exact answers are undecidable in general, for liveness as for any such property of a Turing-complete language,[^cmu] [^spa] so an analysis computes a safe approximation: possibly less precise than the truth, never wrong in the direction its client relies on.

Compilers have computed such approximations since the early 1960s.[^spa] Gary Kildall gave many of them one framework, built on lattices, in 1973, and Kam and Ullman generalized it into the **monotone framework** used today: an ordered set of possible facts, plus rules for moving facts through the code that never turn a more precise input into a less precise output.[^spa] [^ku77] This chapter builds it by hand on the stage 7 loop from O2, then in general, then on the kernel.

## Liveness by hand

A variable is **live** at a point if some execution, continuing from there, reads it before writing it again.[^spa] A variable that is not live holds a value nobody will look at, so its register can be reused. An assignment to a variable that is dead right after it computes a value nobody reads. In Vortex it may still matter, because its right side may carry a check: a dead `x = a + b` still stops the program when the sum overflows ([O5](o5-constants-and-dead-code.md)).

In code without branches, one backward walk finds the live variables: start at the end with nothing live, and step back over each instruction, removing the variable it writes and adding the variables it reads.[^cmu] Loops break the walk. Here is the stage 7 loop, lowered as in [O2](o2-cfg-and-dominance.md#basic-blocks), with each block's letter at its first instruction:

```text
 0  A:  count = 0
 1      total = 0
 2  B:  if not (count < 10) goto F
 3  C:  count = count + 1
 4      if count % 2 == 0 goto B
 5  D:  if total > 10 goto F
 6  E:  total = total + count
 7      goto B
 8  F:  print(total)
 9      return
```

Walking back from F reaches B, whose successors are C and F, and C leads through D and E back to B. The walk goes in a circle.

### Summaries and equations

Summarize each block by two sets. The **use** set holds the variables the block reads before writing them, and the **def** set holds the variables it writes. C reads `count` before writing it, so `count` is in both of C's sets; the later read in `count % 2` sees C's own write and does not count.

| Block | Instructions | use | def |
| --- | --- | --- | --- |
| A | `count = 0; total = 0` | ∅ | count, total |
| B | `if not (count < 10) goto F` | count | ∅ |
| C | `count = count + 1; if count % 2 == 0 goto B` | count | count |
| D | `if total > 10 goto F` | total | ∅ |
| E | `total = total + count; goto B` | count, total | total |
| F | `print(total); return` | total | ∅ |

Call the variables live at the start of a block b its **live-in** set, in(b), and those live at its end its **live-out** set, out(b). A variable is live at the end of a block when it is live at the start of some successor, and live at the start of a block when the block reads it first, or when it is live at the end and the block does not write it:

$$
\mathrm{out}(b) = \bigcup_{s \,\in\, \mathrm{succ}(b)} \mathrm{in}(s), \qquad
\mathrm{in}(b) = \mathrm{use}(b) \cup \big(\mathrm{out}(b) \setminus \mathrm{def}(b)\big)
$$

Substituting the first equation into the second leaves six equations, one per block, whose unknowns are the six live-in sets, and the loop makes them depend on each other in a cycle. Solve them as O2 solved the dominator equations: start every unknown at a fixed value, here the empty set, and recompute until a whole pass changes nothing. Liveness facts flow backward, from a block's successors to the block, so visit the blocks in postorder, F first:

<div class="vx-stepper" markdown="1">
<div class="vx-step" markdown="1">

**Step 1. Pass 1, in postorder: F, E, D, C, B, A**

Every live-in set starts empty. A block's live-out is the union of its successors' live-in sets as they stand when the block is visited.

| Visit | Successors' live-in, so far | live-out | live-in |
| --- | --- | --- | --- |
| F | none: F has no successors | ∅ | total |
| E | B: ∅, not visited yet | ∅ | count, total |
| D | F: total; E: count, total | count, total | count, total |
| C | B: ∅; D: count, total | count, total | count, total |
| B | C: count, total; F: total | count, total | count, total |
| A | B: count, total | count, total | ∅ |

</div>
<div class="vx-step" markdown="1">

**Step 2. Pass 2: nothing changes**

Visiting E again, its successor B now has live-in {count, total}, so E's live-out grows to {count, total}. Its live-in stays {count, total}, because E reads both. No live-in set changes, so the sets satisfy all six equations: two passes, twelve block visits.

</div>
<div class="vx-step" markdown="1">

**Step 3. Read the answer**

Both variables are live everywhere inside the loop. At the start of F only `total` is live: after the loop nothing reads `count`, so its register is free. At the start of A nothing is live: no variable is read before the function writes it.

</div>
</div>

Figure 1 shows the result on the graph.

<figure class="vx-figure">
<svg viewBox="0 0 760 470" role="img" aria-label="The stage 7 loop with each block's use and def sets and its live variables once the iteration has settled" aria-describedby="o4-f1-desc">
<title id="o4-f1-title">Liveness on the stage 7 loop</title>
<desc id="o4-f1-desc">Left: the control-flow graph of the stage 7 loop, six blocks in a column. A sets count and total to zero and leads to B, the loop header, which tests count less than 10. B's true edge leads to C, which increments count and tests whether it is even; C's even edge goes back to B and its odd edge goes to D. D tests total greater than 10; its no edge goes to E, which adds count to total and goes back to B. B's false edge and D's yes edge both go down the right side to F, which prints total. An animated dashed arrow on the far left points upward, the direction in which liveness facts travel. Right: a table with one row per block. A: use empty, def count and total, live-in empty, live-out count and total. B: use count, def empty, live-in and live-out count and total. C: use count, def count, live-in and live-out count and total. D: use total, def empty, live-in and live-out count and total. E: use count and total, def total, live-in and live-out count and total. F: use total, def empty, live-in total, live-out empty. The live-in of A and the live-in of F are highlighted.</desc>
<defs><marker id="o4-f1-head" viewBox="0 0 10 10" refX="10" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker></defs>
<text class="vx-text" x="90" y="24">Control-flow graph</text>
<text class="vx-text" x="350" y="24">use</text>
<text class="vx-text" x="455" y="24">def</text>
<text class="vx-text" x="560" y="24">live-in</text>
<text class="vx-text" x="660" y="24">live-out</text>
<rect class="vx-box" x="90" y="44" width="220" height="34" rx="4"/>
<text class="vx-text" x="102" y="66">A</text>
<text class="vx-mono" x="124" y="66">count = 0; total = 0</text>
<rect class="vx-box-strong" x="90" y="112" width="220" height="34" rx="4"/>
<text class="vx-text" x="102" y="134">B</text>
<text class="vx-mono" x="124" y="134">count &lt; 10 ?</text>
<rect class="vx-box" x="90" y="180" width="220" height="34" rx="4"/>
<text class="vx-text" x="102" y="202">C</text>
<text class="vx-mono" x="124" y="202">count += 1; even ?</text>
<rect class="vx-box" x="90" y="248" width="220" height="34" rx="4"/>
<text class="vx-text" x="102" y="270">D</text>
<text class="vx-mono" x="124" y="270">total &gt; 10 ?</text>
<rect class="vx-box" x="90" y="316" width="220" height="34" rx="4"/>
<text class="vx-text" x="102" y="338">E</text>
<text class="vx-mono" x="124" y="338">total += count</text>
<rect class="vx-box-accent" x="90" y="400" width="220" height="34" rx="4"/>
<text class="vx-text" x="102" y="422">F</text>
<text class="vx-mono" x="124" y="422">print(total)</text>
<line class="vx-line" x1="200" y1="78" x2="200" y2="112" marker-end="url(#o4-f1-head)"/>
<line class="vx-line" x1="200" y1="146" x2="200" y2="180" marker-end="url(#o4-f1-head)"/>
<text class="vx-text-muted" x="208" y="168">true</text>
<line class="vx-line" x1="200" y1="214" x2="200" y2="248" marker-end="url(#o4-f1-head)"/>
<text class="vx-text-muted" x="208" y="236">odd</text>
<line class="vx-line" x1="200" y1="282" x2="200" y2="316" marker-end="url(#o4-f1-head)"/>
<text class="vx-text-muted" x="208" y="304">no</text>
<path class="vx-line" d="M90 197 L74 197 L74 136 L90 136" marker-end="url(#o4-f1-head)"/>
<path class="vx-line" d="M90 333 L60 333 L60 122 L90 122" marker-end="url(#o4-f1-head)"/>
<path class="vx-line" d="M310 129 L332 129 L332 417 L310 417" marker-end="url(#o4-f1-head)"/>
<path class="vx-line" d="M310 265 L321 265 L321 407 L310 407" marker-end="url(#o4-f1-head)"/>
<path class="vx-flow" d="M24 440 L24 60" marker-end="url(#o4-f1-head)"/>
<text class="vx-mono" x="350" y="66">∅</text>
<text class="vx-mono" x="455" y="66">count, total</text>
<text class="vx-text-accent" x="560" y="66">∅</text>
<text class="vx-mono" x="660" y="66">count, total</text>
<text class="vx-mono" x="350" y="134">count</text>
<text class="vx-mono" x="455" y="134">∅</text>
<text class="vx-mono" x="560" y="134">count, total</text>
<text class="vx-mono" x="660" y="134">count, total</text>
<text class="vx-mono" x="350" y="202">count</text>
<text class="vx-mono" x="455" y="202">count</text>
<text class="vx-mono" x="560" y="202">count, total</text>
<text class="vx-mono" x="660" y="202">count, total</text>
<text class="vx-mono" x="350" y="270">total</text>
<text class="vx-mono" x="455" y="270">∅</text>
<text class="vx-mono" x="560" y="270">count, total</text>
<text class="vx-mono" x="660" y="270">count, total</text>
<text class="vx-mono" x="350" y="338">count, total</text>
<text class="vx-mono" x="455" y="338">total</text>
<text class="vx-mono" x="560" y="338">count, total</text>
<text class="vx-mono" x="660" y="338">count, total</text>
<text class="vx-mono" x="350" y="422">total</text>
<text class="vx-mono" x="455" y="422">∅</text>
<text class="vx-text-accent" x="560" y="422">total</text>
<text class="vx-mono" x="660" y="422">∅</text>
<text class="vx-text-muted" x="350" y="462">moving dashes: liveness facts travel up, against the edges</text>
</svg>
<figcaption>Figure 1. The stage 7 loop and its liveness once the iteration has settled. The two edges on the left go back to the header B, from C (<code>continue</code>) and from E; the two on the right go to F, from B when the test fails and from D (<code>break</code>). Nothing is live at the start of A, and only <code>total</code> at the start of F. The moving dashes show the direction the facts flow: backward, from each use toward the definitions that feed it.</figcaption>
</figure>

The first example solves the same equations three ways, including the order used above:

--8<-- "includes/examples/optimize/o4-dataflow/liveness.cpp.md"

All three reach the same sets and differ only in the work they do. The section on lattices explains why.

### Why start from empty

Starting from the empty set was a choice. Add to the function a variable `v` that no instruction reads or writes, and put `v` in the live-in and live-out sets of A, B, C, D and E. Every equation still holds: the loop blocks pass `v` to one another, no block defines it, and F, which has no successor to demand it, stays as it was. The equations have more than one solution, and this one keeps an unused variable alive through the whole loop.

The smallest solution is the useful one, because every variable in it traces back to a real read, and iterating from the empty set finds exactly that solution. Starting from "everything live" would settle on the largest solution instead, which is safe but says almost nothing.

In Vortex the entry gives a free test. Every local declaration has an initializer ([Declarations 3.5](../specification/declarations.md#35-local-variable-declarations)), so only parameters can be live at a function's entry; a local found there means a bug in the lowering or in the analysis.

Now apply the definition to the kernel's `k` loop, as [O2](o2-cfg-and-dominance.md#your-turn-the-kernels-inner-loop) lowered it.

??? check "Which variables are live at the header of the kernel's `k` loop?"

    Seven: `k` (the test reads it), `sum` (the body and the store after the loop read it), `a` and `b` (the body reads them), `c` (the store after the loop), and `row` and `column` (the reads in the body, the store and the outer loops). The bound 64 is a constant, not a variable. All seven stay live on every trip around the loop, so a compiler that does not spill needs a register for each, plus the temporaries of the bounds checks. [P12](p12-fast-gemm.md) builds a kernel that keeps many more values live at once, on purpose.

## The general recipe

Liveness is one instance of a pattern. A dataflow problem is defined by five choices:

1. **Facts**, with an order that says which of two facts claims more. For liveness, sets of variables.
2. **A direction.** A **forward** problem describes the past, and its facts flow along the edges; a **backward** problem describes the future, and its facts flow against them.[^spa]
3. **A transfer function** for each block, which turns the fact at one end of the block into the fact at the other.
4. **A way to combine facts where edges meet.** A **may** problem keeps what holds on some path and combines with union; a **must** problem keeps only what holds on every path and combines with intersection.[^spa]
5. **A start**: a boundary fact at the entry or the exits, and a starting value for every other unknown.

The four classic problems cover every combination of direction and kind:[^spa]

| Problem | Direction | Kind | Question | Used by |
| --- | --- | --- | --- | --- |
| Reaching definitions | forward | may | Which assignments may have produced the value a use reads? | linking each use to its definitions, which SSA form ([O3](o3-ssa.md)) builds in |
| Live variables | backward | may | Which variables may still be read? | register allocation ([C3](../backend/c3-linear-scan.md)), dead stores ([O5](o5-constants-and-dead-code.md)), pruned SSA form ([O3](o3-ssa.md)) |
| Available expressions | forward | must | Which expressions have been computed on every path, with operands unchanged since? | reusing values ([O6](o6-redundancy.md)) |
| Very busy expressions | backward | must | Which expressions will be computed on every path before an operand changes? | hoisting code ([O6](o6-redundancy.md)) |

O2's dominator computation belongs in the table too: its facts are sets of blocks, it runs forward and it intersects, so it is a must problem. Both it and liveness start every unknown from the fact that claims the most. For liveness that is the empty set, "nothing is live"; for a must problem it is the full set, which is why O2's sets started full.

All four problems have transfer functions of one shape. For a block b, a **gen** set of facts the block creates and a **kill** set of facts it destroys give

$$
f_b(x) = \mathrm{gen}_b \cup (x \setminus \mathrm{kill}_b)
$$

For liveness, gen is the use set and kill the def set. For available expressions, a block generates the expressions it computes, unless it later writes one of their operands, and kills every expression that reads a variable it writes.[^spa] With one bit per variable, definition or expression, union is a bitwise OR and intersection an AND, and a transfer function costs an AND with the complement of kill and an OR per machine word. The first example stores its sets this way.

Kill sets are where knowledge about memory enters. The kernel's store `c[row, column] = sum` destroys any available value loaded from `c`, but not the values loaded from `a` or `b`. A variable passed as `&mut` may appear in no other argument of the same call, and no other reference to it may be live while that one is ([decisions 25](../decisions/references.md#d25) and [41](../decisions/references.md#d41)), so `c` never names the same array as `a` or `b`. [O9](o9-alias-analysis.md) turns those rules into alias analysis.

## Your turn: reaching definitions

A **definition** is an assignment, and it **reaches** a point if some path leads from it to the point without another assignment to the same variable.[^spa] The stage 7 loop has four: d1 `count = 0` and d2 `total = 0` in A, d3 `count = count + 1` in C, and d4 `total = total + count` in E. The problem runs forward, combines with union and starts from empty sets:[^cs6120]

$$
\mathrm{in}(b) = \bigcup_{p \,\in\, \mathrm{pred}(b)} \mathrm{out}(p), \qquad
\mathrm{out}(b) = \mathrm{gen}_b \cup (\mathrm{in}(b) \setminus \mathrm{kill}_b)
$$

A block kills every other definition of the variables it assigns. Visiting the blocks in reverse postorder, the third pass is the first that changes nothing. Part of the answer is filled in:

| Block | gen | kill | Definitions reaching its start |
| --- | --- | --- | --- |
| A | d1, d2 | d3, d4 | none |
| B | | | d1, d2, d3, d4 |
| C | d3 | d1 | ? |
| D | | | ? |
| E | d4 | d2 | ? |
| F | | | ? |

Complete the last column, then say which definitions the read of `total` in F may see.

??? check "Answers for reaching definitions"

    - C: d1, d2, d3, d4, since C's only predecessor is B and B changes nothing.
    - D: d2, d3, d4. Every path to D passes through C, which kills d1.
    - E: d2, d3, d4, the same as D, which changes nothing.
    - F: d1, d2, d3, d4, the union of what leaves B and what leaves D.
    - The read of `total` in F may see d2 or d4. The path A, B, F never runs, since `count` starts below 10, but the analysis does not evaluate the test and must keep it. Two reaching definitions at one use are why SSA form puts a phi for `total` at B ([O2](o2-cfg-and-dominance.md#the-dominance-frontier)); [O3](o3-ssa.md) builds the form so that every use has exactly one.

## Why the iteration stops: lattices

Two questions are still open: why iterating always stops, and why three strategies found the same sets. The answers come from the structure of the facts.

### Facts ordered by what they claim

A **partial order** is a relation ⊑ that is reflexive, transitive and antisymmetric. For analysis facts, x ⊑ y means that y is a safe approximation of x: y claims no more than x.[^spa] For liveness a larger set claims less, and overstating is the safe direction: calling a dead variable live wastes a register, while calling a live one dead breaks the program.[^spa]

The **join** x ⊔ y is the least upper bound of x and y: the most precise fact that is safe for both. The **meet** x ⊓ y is the greatest lower bound, the least precise fact that claims at least as much as both. A **lattice** is a partial order in which every two elements have a join and a meet, and the lattices here also have a least element ⊥, bottom, and a greatest, ⊤, top. The **height** of a lattice is the length of its longest chain from ⊥ to ⊤.[^spa]

The subsets of a set A, ordered by ⊆, form the **powerset lattice**: ⊥ is ∅, ⊤ is A, join is union, and the height is the size of A. Ordered by ⊇ instead, the same subsets serve a must problem, with the full set as ⊥ and intersection as join.[^spa] Liveness on the stage 7 loop uses the powerset of {count, total}, of height 2, once per block, so the whole analysis works on tuples of six sets compared position by position. The height of such a tuple lattice is the sum of its parts' heights,[^spa] 6 × 2 = 12.

### Monotone functions and the fixed-point theorem

A function f is **monotone** if x ⊑ y implies f(x) ⊑ f(y): a more precise input never gives a less precise output.[^spa] The liveness functions are monotone, since adding variables to out(b) can only add variables to in(b).

A **fixed point** of f is a value x with f(x) = x. Gather the six liveness equations into one function F on tuples of sets, and the solutions of the equations are exactly the fixed points of F. Kleene's fixed-point theorem, as Møller and Schwartzbach state it, says that in a lattice of finite height every monotone function has a unique **least fixed point**, the limit of ⊥, F(⊥), F(F(⊥)), and so on.[^spa]

The proof is short. The sequence rises, because ⊥ ⊑ F(⊥) and monotonicity carries each step to the next; a rising sequence in a lattice of finite height must stop, and where it stops is a fixed point. Every other fixed point x lies above it, because ⊥ ⊑ x gives F(⊥) ⊑ F(x) = x, and so on up.[^spa]

This answers both questions. Each pass that changes anything moves some fact up, which can happen only as often as the height allows: at most twelve changing passes for the stage 7 loop, in any order. The answer is the least solution, the one the empty start was meant to find. And recomputing the unknowns one at a time, in any order, until none changes reaches the same least fixed point:[^spa] the order sets the cost, never the result.

Figure 2 shows three lattices this chapter uses, each with a different shape.

<figure class="vx-figure">
<svg viewBox="0 0 760 360" role="img" aria-label="Three lattices: sets of two variables, the flat lattice of constants, and a chain of intervals" aria-describedby="o4-f2-desc">
<title id="o4-f2-title">Three lattices of analysis facts</title>
<desc id="o4-f2-desc">Left, liveness: the four subsets of count and total, drawn with the empty set at the bottom, the sets holding only count and only total in the middle, and the set holding both at the top. Its height is 2. Middle, constants: the flat lattice, with bottom at the bottom, the integers 1, 2 and 3 in a row with dots on either side for all other integers, and top at the top. A dot moves from bottom up to 2 and pauses, then up to top: the value of x where the two paths of the function pick meet, first 2 from one path, then top after joining 3 from the other. Its height is also 2, but it is infinitely wide. Right, intervals: part of a vertical chain, from the interval 0 to 0 through 0 to 1 and 0 to 2, then dashed lines past 0 to 64 up to 0 to infinity. Two animated dashed curves show widening: one jumps from 0 to 1 up to 0 to 64, the next constant in the program; the other jumps from 0 to 0 up to 0 to infinity. The chain rises forever, so the interval lattice has infinite height.</desc>
<defs><marker id="o4-f2-head" viewBox="0 0 10 10" refX="10" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker></defs>
<text class="vx-text" x="20" y="24">Liveness</text>
<text class="vx-text-muted" x="20" y="42">subsets of {count, total}, by ⊆</text>
<text class="vx-text" x="280" y="24">Constants</text>
<text class="vx-text-muted" x="280" y="42">one value per variable</text>
<text class="vx-text" x="540" y="24">Intervals</text>
<text class="vx-text-muted" x="540" y="42">one range per variable</text>
<rect class="vx-box" x="60" y="90" width="120" height="28" rx="4"/>
<text class="vx-mono" x="120" y="109" text-anchor="middle">count, total</text>
<rect class="vx-box" x="25" y="176" width="90" height="28" rx="4"/>
<text class="vx-mono" x="70" y="195" text-anchor="middle">count</text>
<rect class="vx-box" x="125" y="176" width="90" height="28" rx="4"/>
<text class="vx-mono" x="170" y="195" text-anchor="middle">total</text>
<rect class="vx-box" x="95" y="276" width="50" height="28" rx="4"/>
<text class="vx-mono" x="120" y="295" text-anchor="middle">∅</text>
<line class="vx-line" x1="112" y1="276" x2="74" y2="204"/>
<line class="vx-line" x1="128" y1="276" x2="166" y2="204"/>
<line class="vx-line" x1="74" y1="176" x2="104" y2="118"/>
<line class="vx-line" x1="166" y1="176" x2="136" y2="118"/>
<text class="vx-text-muted" x="20" y="330">join: union</text>
<text class="vx-text-muted" x="20" y="348">height 2</text>
<circle class="vx-box" cx="390" cy="104" r="16"/>
<text class="vx-mono" x="390" y="109" text-anchor="middle">⊤</text>
<circle class="vx-box" cx="330" cy="190" r="16"/>
<text class="vx-mono" x="330" y="195" text-anchor="middle">1</text>
<circle class="vx-box" cx="390" cy="190" r="16"/>
<text class="vx-mono" x="390" y="195" text-anchor="middle">2</text>
<circle class="vx-box" cx="450" cy="190" r="16"/>
<text class="vx-mono" x="450" y="195" text-anchor="middle">3</text>
<text class="vx-mono" x="290" y="195" text-anchor="middle">…</text>
<text class="vx-mono" x="490" y="195" text-anchor="middle">…</text>
<circle class="vx-box" cx="390" cy="290" r="16"/>
<text class="vx-mono" x="390" y="295" text-anchor="middle">⊥</text>
<line class="vx-line" x1="382" y1="276" x2="338" y2="204"/>
<line class="vx-line" x1="390" y1="274" x2="390" y2="206"/>
<line class="vx-line" x1="398" y1="276" x2="442" y2="204"/>
<line class="vx-line" x1="338" y1="176" x2="382" y2="118"/>
<line class="vx-line" x1="390" y1="174" x2="390" y2="120"/>
<line class="vx-line" x1="442" y1="176" x2="398" y2="118"/>
<circle class="vx-dot" r="6"><animateMotion dur="6s" repeatCount="indefinite" path="M390 290 L390 190 L390 104" keyPoints="0;0.538;0.538;1;1" keyTimes="0;0.2;0.5;0.7;1" calcMode="linear"/></circle>
<text class="vx-text-muted" x="280" y="330">join of 2 and 3: ⊤</text>
<text class="vx-text-muted" x="280" y="348">height 2, infinitely wide</text>
<rect class="vx-box" x="580" y="64" width="100" height="26" rx="4"/>
<text class="vx-mono" x="630" y="82" text-anchor="middle">[0, +∞]</text>
<rect class="vx-box" x="580" y="128" width="100" height="26" rx="4"/>
<text class="vx-mono" x="630" y="146" text-anchor="middle">[0, 64]</text>
<rect class="vx-box" x="580" y="192" width="100" height="26" rx="4"/>
<text class="vx-mono" x="630" y="210" text-anchor="middle">[0, 2]</text>
<rect class="vx-box" x="580" y="240" width="100" height="26" rx="4"/>
<text class="vx-mono" x="630" y="258" text-anchor="middle">[0, 1]</text>
<rect class="vx-box" x="580" y="288" width="100" height="26" rx="4"/>
<text class="vx-mono" x="630" y="306" text-anchor="middle">[0, 0]</text>
<line class="vx-line" x1="630" y1="288" x2="630" y2="266"/>
<line class="vx-line" x1="630" y1="240" x2="630" y2="218"/>
<line class="vx-line" x1="630" y1="192" x2="630" y2="154" stroke-dasharray="3 4"/>
<line class="vx-line" x1="630" y1="128" x2="630" y2="90" stroke-dasharray="3 4"/>
<path class="vx-flow" d="M680 253 C712 232 712 166 684 143" marker-end="url(#o4-f2-head)"/>
<path class="vx-flow" d="M680 301 C748 262 748 104 684 79" marker-end="url(#o4-f2-head)"/>
<text class="vx-text-muted" x="540" y="330">dashes: widening jumps</text>
<text class="vx-text-muted" x="540" y="348">infinite height</text>
</svg>
<figcaption>Figure 2. Three lattices, each drawn with ⊥ at the bottom and the least precise fact at the top. Left: the facts of liveness on the stage 7 loop, of height 2. Middle: the flat lattice of constants, also of height 2 but infinitely wide; the moving dot is the value of <code>x</code> where the two paths of <code>pick</code>, below, meet: first 2, then ⊤. Right: part of the interval lattice, where [0, 0] ⊑ [0, 1] ⊑ [0, 2] ⊑ … rises forever, so iteration alone may never stop; the moving dashes are the jumps that widening makes, to a constant of the program or to infinity.</figcaption>
</figure>

Some texts draw these lattices upside down. Kam and Ullman, Cooper, Harvey and Kennedy, some textbooks and the Cornell course cited below put the least precise fact at the bottom, combine facts with a meet and speak of the maximal fixed point; Møller and Schwartzbach point out that the results are the same.[^spa] [^ku77] [^chk] [^cs6120] This book keeps Møller and Schwartzbach's orientation.

??? check "A transfer function maps ∅ to {count} and {count} to ∅. What goes wrong if an analysis uses it?"

    The function is not monotone: ∅ ⊑ {count}, but the outputs are ordered the other way. Iteration from ∅ produces {count}, then ∅, then {count}, and never stops. The fixed-point theorem needs monotone functions for exactly this reason: they are what makes the sequence rise.

## Doing less work: order and worklists

The theorem leaves the cost open. A **round-robin** solver visits every block in a fixed order until a pass changes nothing,[^spa] so its cost is passes times blocks, and the order sets the number of passes. In reverse postorder a block comes after all its predecessors except along retreating edges ([O2](o2-cfg-and-dominance.md#visiting-the-blocks-in-order)), which suits a forward problem. By the same argument, in postorder a block comes after all its successors except along retreating edges, which suits a backward one. The first example shows the difference: liveness took 2 passes, 12 visits, in postorder, and 3 passes, 18 visits, in reverse postorder.

For some forward problems there is a proven bound. Cooper, Harvey and Kennedy explain that the dominator equations are simple enough to form what Kam and Ullman call a rapid framework, and that for such a framework iteration in reverse postorder stops within d(G) + 3 passes.[^chk] [^ku76] They also cite a study of Fortran programs suggesting that d(G) is typically at most 3.[^chk] For those problems the number of passes follows the loop structure, not the size of the function.

A **worklist** solver skips the visits that cannot change anything.[^spa] [^cs6120] It keeps the blocks whose inputs may have changed and recomputes only those:

```text
fact[b] = the starting value, for every block b
worklist = every block, in the order that suits the direction
while the worklist is not empty:
    take the block b at the front
    new = b's transfer function, applied to the combination of its inputs
    if new differs from fact[b]:
        fact[b] = new
        add each block that reads fact[b], unless it is already waiting
```

For a forward problem the blocks that read fact[b] are b's successors; for a backward one, its predecessors.[^spa] Every block is computed at least once and returns to the list only when something it reads has changed: 8 visits on the stage 7 loop instead of 12. When every block has a bounded number of neighbours, the work is at most proportional to the number of blocks, times the height of one block's lattice, times the cost of one transfer function.[^spa] Møller and Schwartzbach mention refinements such as a priority queue in place of the list;[^spa] ordering it by the visiting order that suits the direction would combine the worklist with the good order.

## Constants: when merging loses information

**Constant propagation** asks whether a variable holds the same constant on every path to a point; Møller and Schwartzbach count it among the first uses of static analysis, citing Kildall's 1973 paper.[^spa] [^kildall] Its facts come from the **flat lattice**: ⊥, no value seen yet, then every integer side by side, then ⊤, not a constant (Figure 2, middle). Joining a constant with itself gives that constant; joining two different constants gives ⊤. The lattice is infinitely wide but of height 2, so a fact changes at most twice.

A fact at a point gives every variable such a value, and an assignment's transfer function evaluates its right side on them. For `x + y` the result is ⊥ when either operand is ⊥, otherwise ⊤ when either is ⊤, and otherwise the sum of the two constants.[^spa]

Here the analysis falls short:

```vortex
// items: valid
fn pick(swap: bool) -> i32 {
    let mut x = 2;
    let mut y = 3;
    if swap {
        x = 3;
        y = 2;
    }
    return x + y;
}
```

Call its blocks A (the two `let`s and the test), B (the two assignments) and C (the `return`). Along A, C the function returns 2 + 3, and along A, B, C it returns 3 + 2: always 5. The analysis joins the facts entering C, where x is 2 or 3, so ⊤, and y likewise, so `x + y` is ⊤.

### Two answers: over all paths, and at the fixed point

The **all-paths solution** applies the transfer functions along each path from the entry and joins the results only at the end. With f<sub>p</sub> the composition of the transfer functions along path p, and ι the fact at the entry:

$$
\mathrm{MOP}(v) = \bigsqcup_{p \,\in\, \mathrm{paths}(\mathrm{entry},\, v)} f_p(\iota)
$$

Its usual name, MOP, for meet over all paths, comes from the upside-down convention. The **fixed-point solution**, called MFP (maximal fixed point) in that convention, is what the iteration computes, joining at every merge point on the way.[^ku77] For `pick` they disagree:

--8<-- "includes/examples/optimize/o4-dataflow/constants.cpp.md"

The difference comes from one property. A function f is **distributive** if f(x ⊔ y) = f(x) ⊔ f(y).[^spa] Every monotone function satisfies f(x) ⊔ f(y) ⊑ f(x ⊔ y),[^spa] so joining early can only lose precision. C's transfer function is not distributive: joining first forgets that x and y changed together, and one value per variable cannot remember it.

Kam and Ullman settled the general case in 1977. Every monotone framework has a fixed-point solution, which Kildall's algorithm computes; when the framework is not distributive, some programs have a fixed-point solution that differs from the all-paths solution; and no algorithm computes the all-paths solution for every monotone framework.[^ku77] For distributive frameworks the two agree, which is how Cooper, Harvey and Kennedy know that their iteration yields exactly the dominators of the definition.[^chk]

Gen and kill functions are distributive, by one line of algebra:

$$
f(x \cup y) = \mathrm{gen} \cup \big((x \cup y) \setminus \mathrm{kill}\big) = \big(\mathrm{gen} \cup (x \setminus \mathrm{kill})\big) \cup \big(\mathrm{gen} \cup (y \setminus \mathrm{kill})\big) = f(x) \cup f(y)
$$

The same holds with intersection in place of union, so liveness and reaching definitions lose nothing at merge points. Constant propagation is not distributive,[^spa] and recovering `pick`'s 5 needs an analysis that tracks correlations between variables or tells paths apart, the subject of Møller and Schwartzbach's chapter on path sensitivity and relational analysis.[^spa]

### Transfer functions must follow Vortex's rules

A transfer function evaluates the language, so it must evaluate exactly as the program would.

- **Checked integers.** An `i32` addition whose exact result does not fit stops the program ([Expressions 5.5](../specification/expressions.md#checked-integer-operations)). If the analysis finds both operands constant and the sum overflows, no value flows on, so the result is ⊥, the choice Møller and Schwartzbach make for a division by a constant zero.[^spa] The check stays, and the program still compiles. The front end has already rejected every failing operation whose operands are integer constant expressions, such as `2147483647 + 1`; a failure that only the analysis finds is a runtime error, never a compile-time one ([decision 39](../decisions/diagnostics.md#d39)).
- **Floating point.** Decision 56 covers values computed during compilation, so folding `f32` constants means computing each operation in binary32, rounded to nearest with ties to even ([decision 56](../decisions/numbers.md#d56)). Identities that fail for floats are off limits: [O1](o1-optimizer-contract.md#floating-point-identities-that-are-false) showed that `x - x` is not 0 when `x` is infinite. For `i32`, `x - x` is 0 for every `x` and cannot overflow, so a transfer function may return 0 even when `x` is ⊤, a gain that the plain abstract subtraction, which sees only two ⊤ operands, misses.

## Optimism: where to start

The empty start for liveness chose one solution among several. Constants show what that choice is worth:

```vortex
// items: valid
fn flip(n: i32) -> i32 {
    let mut x = 1;
    let mut i = 0;
    while i < n {
        x = 2 - x;
        i += 1;
    }
    return x;
}
```

At the loop header, x is the join of 1, from the entry, and `2 - x`, from the end of the body, so it must satisfy x = 1 ⊔ (2 − x). Two values do: 1, since 2 − 1 is 1, and ⊤, since 2 − ⊤ is ⊤ and 1 ⊔ ⊤ is ⊤. The least, 1, is the truth.

An **optimistic** analysis starts every unknown at ⊥ and finds the least solution: the back edge first contributes ⊥, so x is 1; the body computes 2 − 1 = 1; the header confirms it. A **pessimistic** analysis assumes ⊤ for every fact it has not computed yet, the back edge included, and lands on ⊤, where it stays, because ⊤ is also a fixed point.

LLVM's `sccp` pass is optimistic: it "assumes values are constant unless proven otherwise", and it assumes blocks are dead until proven reachable.[^llvm-passes] It tracks each value in LLVM's value lattice, whose states include **unknown** (no value yet, meaning the instruction producing it never runs), a constant, an integer range, and **overdefined**, after which no further change is allowed.[^llvm-lattice] The `adce` pass is optimistic about liveness in the same way, treating values as dead until proven otherwise.[^llvm-passes] The third example gives `sccp` the same loop in LLVM IR, written with its test at the bottom:

--8<-- "includes/examples/optimize/o4-dataflow/optimistic.ll.md"

The phi and the subtraction are gone, and the function returns 1. Run through `-passes=instcombine` or `-passes=early-cse` instead, the same file keeps both (LLVM 18.1.8 on the owner's M4 Pro, checked on 2026-09-24).

Optimism has a price: only the finished fixed point is guaranteed safe. Each element of the rising sequence ⊥, F(⊥), and so on before the end lies below the least fixed point and claims more than the equations justify, so an optimistic analysis stopped early, for instance to bound compile time, may hand out false facts. A pessimistic analysis descends from ⊤ and stays above every fixed point on the way, so it can stop at any point and remain correct, only less precise.

### Dense and sparse

Every analysis so far is **dense**: it keeps a fact for every variable at every block, and copies it through every transfer function that does not touch it. In SSA form ([O3](o3-ssa.md)) each value has one definition, so a fact such as "this value is the constant 1" holds wherever the value exists. An analysis can then keep one fact per value and send updates from each definition straight to its uses.

Such an analysis is **sparse**, the S in SCCP; LLVM's solver keeps one lattice element per value.[^llvm-passes] [^llvm-lattice] MLIR's dataflow framework works the same way: a lattice element per value, a special uninitialized state that a join treats as "take the other thing", and a join that must be monotone.[^mlir-df] [O5](o5-constants-and-dead-code.md) builds sparse conditional constant propagation.

## Intervals: bounding a loop index

The kernel's inner loop reads `a[row, k]` and `b[k, column]`, and a v0.1 compiler checks before each read that `0 <= k` and `k < 64`, since a negative index is out of bounds too ([Arrays 7.6](../specification/arrays.md#76-indexing)). Removing those checks ([O8](o8-loops.md)) first needs a proof that k lies in [0, 63] at both reads.

Constant propagation gives ⊤, since k takes 64 different values at the reads. **Interval analysis** keeps a lower and an upper bound [lo, hi] for each integer variable, either of which may be infinite; the join of two intervals is the smallest interval containing both.[^spa] This lattice contains the chain [0, 0] ⊑ [0, 1] ⊑ [0, 2] ⊑ …, which never ends, so its height is infinite and the fixed-point theorem no longer promises termination.[^spa] Two ideas make the analysis work.

**Branch conditions refine facts.** On the edge where `k < 64` holds, k's interval is cut to its part below 64; on the other edge, to its part at or above 64. Møller and Schwartzbach model each condition as an assertion at the start of the branch it guards, and call an analysis that uses them control sensitive.[^spa]

**Widening forces termination.** At a loop header the join is replaced by a **widening**: a bound that grew since the last round jumps at once to a coarser value. Møller and Schwartzbach credit the technique to Patrick and Radhia Cousot's 1977 paper, and show that widening at loop headers alone guarantees that the iteration stops.[^spa] [^cc77] The jump can go to infinity, or to the next value in a fixed set, typically the program's own constants; a few rounds of ordinary iteration afterwards, called **narrowing**, can win back precision.[^spa]

Here is the `k` loop, lowered as in [O2](o2-cfg-and-dominance.md#your-turn-the-kernels-inner-loop), with k = 0 on the edge into the header:

| Round | k at the header, no widening | Widening to the program's constants | Widening to infinity |
| --- | --- | --- | --- |
| 1 | [0, 0] | [0, 0] | [0, 0] |
| 2 | [0, 1] | [0, 1] | [0, +∞] |
| 3 | [0, 2] | [0, 64] | [0, +∞], unchanged: done |
| 4 | [0, 3] | [0, 64], unchanged: done | |
| 5 to 64 | [0, 4] to [0, 63], one step per round | | |
| 65 | [0, 64] | | |
| 66 | [0, 64], unchanged: done | | |

In the body all three end with k in [0, 63]; at the exit they give [64, 64], [64, 64] and [64, +∞]. Without widening the header grows one step per round, as if the analysis ran the loop, and it stops only because the refinement caps the body at 63.

Widening to the program's constants stops the growth in round 3: the join gives [0, 2], whose upper bound grew past the old [0, 1], so widening moves it to 64, the next constant of the program above 2. Vortex's fixed shapes put that constant in the program: the loop bound and the extent in `[f32; 64, 64]` are both 64. Widening to infinity is coarser at the header and the exit, until one round of narrowing recomputes the header from the body's [0, 63] and restores [0, 64].

Either way k lies in [0, 63] at both reads, so both halves of both checks always pass, and the same reasoning gives `row` and `column` the interval [0, 63] in their loops. This chapter only proves it; removing the checks is [O8](o8-loops.md)'s work. The fourth example runs the analysis on a counting loop with bounds 100 and 1000:

--8<-- "includes/examples/optimize/o4-dataflow/intervals.cpp.md"

Without widening the number of rounds grows with the bound, 102 and then 1002; with widening it stays at 3 or 4. Intervals also bound arithmetic: in `flip`, the checked step `i += 1` runs only when i < n, so i + 1 cannot exceed the largest `i32`. A `for` loop needs no such proof, because stepping its variable never fails an overflow check ([Statements 6.8](../specification/statements.md#68-for-loops)).

??? check "In `let mut j = 10; while j > 0 { j -= 1; }`, with widening to the constants 0, 1 and 10, what intervals does `j` get, and can `j -= 1` overflow?"

    At the header [0, 10]. Round 1 gives [10, 10]. Round 2 joins in 9 from the body; the lower bound fell, so widening moves it to 1, the next constant below 9. Round 3 brings in 0 and moves the bound to 0, and round 4 changes nothing. In the body, where `j > 0` holds, j is in [1, 10]; at the exit, [0, 0]. So `j -= 1` produces at least 0 and never overflows. Widening to infinity gives [−∞, 10] at the header and [−∞, 0] at the exit, yet the body is still [1, 10], because the branch condition bounds it; one round of narrowing then restores [0, 10] and [0, 0].

## For Vortex

!!! vortex "Exercise"

    **Build** a small dataflow toolkit over your compiler's lowered IR, on top of the control-flow toolkit from [O2](o2-cfg-and-dominance.md#for-vortex).

    1. A solver that takes a direction, a lattice (its ⊥, its join and an equality test), a boundary fact and a transfer function per block, and computes the least fixed point, by round robin in a given order or with a worklist, counting block visits either way.
    2. Liveness as its first client: live-in and live-out sets for every block, printed for any function on request.
    3. Constant propagation as its second client, over the flat lattice, for the integer and boolean types, and for `f32` and `f64` only if your folding follows decision 56 bit for bit. An integer operation that would fail its check produces ⊥. Report each variable proven constant as an analysis remark in the remark stream you built for [O1](o1-optimizer-contract.md#for-vortex), and change no code.
    4. Optionally, interval analysis for integer variables, with branch refinement and widening at loop headers to the function's integer constants, reporting as a remark each bounds check it proves will pass.

    **Not yet:** deleting code, folding constants into the IR or removing checks ([O5](o5-constants-and-dead-code.md), [O8](o8-loops.md)); sparse analyses over SSA form ([O5](o5-constants-and-dead-code.md)); facts about memory beyond what decisions 25 and 41 give you ([O9](o9-alias-analysis.md)); analysis across calls. Never turn a runtime failure found by analysis into a compile-time error; a warning is allowed ([decision 39](../decisions/diagnostics.md#d39)).

    **Proof that it works:**

    - A fixed-point check after every solve: evaluate every equation once more and assert that nothing changes.
    - Order independence: round robin in reverse postorder, round robin in postorder and the worklist give identical facts on every function in your test suite.
    - Golden tests: the stage 7 loop gives this chapter's liveness table and reaching definitions; `pick` gives ⊤ for `x + y`, recorded as a known imprecision; `flip` gives the constant 1 for `x` at the loop header; the kernel's `k` header has the seven live variables of the check in the liveness section.
    - An entry test: in every function of the suite, only parameters are live at the entry block.
    - A monotonicity test: for each transfer function, generate pairs of facts x ⊑ y and assert that f(x) ⊑ f(y).
    - A soundness test for constants and intervals: run each test program with a hook that records the values variables hold at run time at each block's start, and assert that every recorded value lies within the fact the analysis computed there.
    - A measurement for two functions, filled in from your compiler's output, with the date and your compiler's version:

    | Function | Blocks | Variables | Visits, round robin in reverse postorder | Visits, round robin in postorder | Visits, worklist | Most variables live at once in the innermost loop |
    | --- | --- | --- | --- | --- | --- | --- |
    | stage 7 `main` | | | | | | |
    | stage 10 `multiply` | | | | | | |

## Key ideas

!!! recap "Questions you can now answer"

    - **What defines a dataflow problem?** Its facts and their order, a direction, a transfer function per block, a join where paths meet, and a starting value.
    - **Why does iterating the equations stop?** The transfer functions are monotone and the lattice has finite height, so the facts only rise, and only finitely often.
    - **Why does the order of visits not change the answer?** Every order that keeps recomputing until nothing changes reaches the same least fixed point; the order changes only the number of visits.
    - **Which order suits which problem?** Reverse postorder for forward problems, postorder for backward ones, or a worklist seeded in that order.
    - **When does the fixed point equal the answer over all paths?** When the transfer functions are distributive, as gen and kill functions are; constant propagation's are not.
    - **Why start optimistically?** Starting from ⊥ finds the least fixed point, such as `x = 1` in `flip`, which a pessimistic start misses; the price is that only the finished result is guaranteed safe.
    - **How can an analysis bound a loop index?** With intervals, refined by branch conditions and widened at loop headers, ideally to constants such as Vortex's array extents.

## Where this comes back

!!! next "You will use this again in"

    - [O5. Constants and dead code](o5-constants-and-dead-code.md): *flat lattice*, *optimistic start*, *sparse analysis*, *transfer functions that respect checks*
    - [O6. Redundancy: CSE, GVN, PRE and LICM](o6-redundancy.md): *available expressions*, *very busy expressions*, *kill sets*
    - [O8. Loops: structure, induction variables and bounds checks](o8-loops.md): *interval analysis*, *branch refinement*, *widening*
    - [O9. Memory: alias analysis and MemorySSA](o9-alias-analysis.md): *what a store kills*
    - [O12. Testing an optimizer](o12-testing-optimizers.md): *fixed-point check*, *monotonicity test*, *soundness against execution*
    - [C2. Liveness](../backend/c2-liveness.md): *live-in and live-out sets*, *postorder for backward problems*
    - [C3. Register allocation I: linear scan](../backend/c3-linear-scan.md): *variables live at once*
    - [P12. Anatomy of a fast GEMM](p12-fast-gemm.md): *live values in the inner loop*

## Sources and further reading

Read Møller and Schwartzbach first: chapters 4 and 5 build the lattice framework and its classic analyses from the ground up, chapter 6 adds intervals and widening, and the book is free. Then read Kam and Ullman's 1977 abstract for the limits of the framework, and Cooper, Harvey and Kennedy for a complete, fast solver of one problem. Pfenning and Platzer's lecture notes present liveness from a back-end writer's point of view.

[^spa]: Anders Møller and Michael I. Schwartzbach, *Static Program Analysis*, Aarhus University, lecture notes, read on 2026-09-24: sections 1.1 and 1.2; the introduction to chapter 4 and sections 4.2 to 4.4 (including exercises 4.12, 4.20 and 4.21); the introduction to chapter 5 and sections 5.2 to 5.8 and 5.10 (including the footnote on lattices drawn upside down in 5.4); the introduction to chapter 6 and sections 6.1 and 6.2; the introduction to chapter 7 and section 7.1; and section 9.5. <https://cs.au.dk/~amoeller/spa/>
[^kildall]: Gary A. Kildall, "A Unified Approach to Global Program Optimization", *Conference Record of the ACM Symposium on Principles of Programming Languages (POPL)*, 1973, pages 194 to 206. <https://doi.org/10.1145/512927.512945>
[^ku76]: John B. Kam and Jeffrey D. Ullman, "Global Data Flow Analysis and Iterative Algorithms", *Journal of the ACM* 23(1), 1976, pages 158 to 171. <https://doi.org/10.1145/321921.321938>
[^ku77]: John B. Kam and Jeffrey D. Ullman, "Monotone Data Flow Analysis Frameworks", *Acta Informatica* 7, 1977, pages 305 to 317: the abstract. <https://doi.org/10.1007/BF00290339>
[^cc77]: Patrick Cousot and Radhia Cousot, "Abstract Interpretation: A Unified Lattice Model for Static Analysis of Programs by Construction or Approximation of Fixpoints", *Conference Record of the Fourth ACM Symposium on Principles of Programming Languages (POPL)*, 1977, pages 238 to 252. <https://doi.org/10.1145/512950.512973>
[^chk]: Keith D. Cooper, Timothy J. Harvey and Ken Kennedy, "A Simple, Fast Dominance Algorithm", Rice University: section "Properties of the Iterative Framework" and its footnote 3. <https://www.cs.tufts.edu/comp/150FP/archive/keith-cooper/dom14.pdf>
[^cmu]: Frank Pfenning and André Platzer, "Lecture Notes on Liveness Analysis", 15-411 Compiler Design, lecture 4, Carnegie Mellon University, 2013: sections 1 and 2. <https://www.cs.cmu.edu/~fp/courses/15411-f13/lectures/04-liveness.pdf>
[^cs6120]: Cornell University, CS 6120 (fall 2023), "Lesson 4: Data Flow": reaching definitions in the data flow framework, the worklist algorithm, the theory interlude and the list of analyses. <https://www.cs.cornell.edu/courses/cs6120/2023fa/lesson/4/>
[^llvm-passes]: LLVM Project, "LLVM's Analysis and Transform Passes", entries `sccp` and `adce`. <https://llvm.org/docs/Passes.html>
[^llvm-lattice]: LLVM Project, `ValueLattice.h`, release/18.x branch: the comments on `ValueLatticeElement` and its states <https://github.com/llvm/llvm-project/blob/release/18.x/llvm/include/llvm/Analysis/ValueLattice.h>; and `SCCPSolver.cpp`, release/18.x branch: the map `ValueState`, which holds one `ValueLatticeElement` per value <https://github.com/llvm/llvm-project/blob/release/18.x/llvm/lib/Transforms/Utils/SCCPSolver.cpp>
[^mlir-df]: MLIR Project, "Writing DataFlow Analyses in MLIR", section "Lattices". <https://mlir.llvm.org/docs/Tutorials/DataFlowAnalysis/>

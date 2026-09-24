# O6. Redundancy: CSE, GVN, PRE and LICM

<p class="page-intro">Programs repeat work: the same address computed for two array reads, the same product on two paths, an expression that gives the same value on every trip around a loop. This chapter builds the analyses that find such repeats and the transformations that remove them, and aims them at the Vortex kernel, whose inner loop spends much of its integer work on address arithmetic and bounds checks that repeat.</p>

<p class="vx-meta" markdown="1">Level: Intermediate · Reading time: about 50 minutes · Builds on: [O3. SSA form: construction and destruction](o3-ssa.md), [O4. Dataflow analysis](o4-dataflow.md)</p>

???+ remember "Before you start, remember"

    ??? question "In strict SSA form, where is a value's definition relative to its uses?"

        It dominates every use. A phi's use of an operand counts as happening at the end of the matching predecessor.

        Introduced in [O3. SSA form: construction and destruction](o3-ssa.md#how-many-phis).

    ??? question "Which two of the four classic dataflow problems ask what holds on every path?"

        Available expressions, which runs forward, and very busy expressions, which runs backward. Both are must problems and combine facts with intersection.

        Introduced in [O4. Dataflow analysis](o4-dataflow.md#the-general-recipe).

    ??? question "What is a loop's preheader, and what is the catch in hoisting work into it?"

        The one block outside the loop whose only edge goes to the header. It dominates the loop and runs once each time the loop is entered. The catch is a loop that runs zero times: hoisted work then runs when the original never did.

        Introduced in [O2. Control-flow graphs and dominance](o2-cfg-and-dominance.md#loops-from-dominance).

    ??? question "Why must a critical edge be split before code is placed on it?"

        It runs from a block with several successors to a block with several predecessors, so code at the end of its source or at the start of its target would also run on other edges.

        Introduced in [O2. Control-flow graphs and dominance](o2-cfg-and-dominance.md#critical-edges).

    ??? question "Name two things a Vortex optimizer must never do to a runtime check."

        Move it above a `print`, or let it trade places with another check that might fail first. It may drop a check only after proving that the check cannot fail.

        Introduced in [O1. The optimizer's contract](o1-optimizer-contract.md#vortexs-list).

!!! goals "In this chapter"

    - Recognize full, partial and loop-invariant redundancy in a control-flow graph, and name the transformation that removes each.
    - Number the values of a block by hashing, and extend the numbering to a whole function with the dominator tree.
    - Explain why proving two loop values equal needs an optimistic analysis, and what LLVM's `gvn` and `newgvn` passes each find.
    - Place computations with lazy code motion: down-safety, earliest placement, and the delay that keeps lifetimes short.
    - Decide when a Vortex compiler may merge, hoist or insert a computation, given its runtime checks, its strict floating point and its `&mut` rule.

## Work done twice

Here is a Vortex function that adds up one row of a grid, counting the two edge cells a second time when asked:

```vortex
// items: valid
fn row_total(grid: &[i32; 8, 8], r: i32, with_edges: bool) -> i32 {
    let mut total = 0;
    if with_edges {
        total = grid[r, 0] + grid[r, 7];
    }
    for c in 0..8 {
        total += grid[r, c];
    }
    return total;
}
```

No expression in the source is written twice, but the compiled code repeats itself. Arrays are stored row after row ([decision 43](../decisions/arrays.md#d43)), so element `[r, c]` of an `[i32; 8, 8]` array sits `r * 8 + c` elements from the start, and every read computes that position after checking its indices. The constant indices 0 and 7 are checked during compilation; `r` and `c` are checked at run time ([decision 12](../decisions/arrays.md#d12)). Here is the function in SSA form ([O3](o3-ssa.md)), each check leaving for an error block when it fails, as in [O2](o2-cfg-and-dominance.md#your-turn-the-kernels-inner-loop):

```text
entry: if with_edges goto edge else pre
edge:  check 0 <= r < 8            ; grid[r, 0]
       t1 = r * 8
       e0 = load grid[t1 + 0]
       check 0 <= r < 8            ; grid[r, 7]
       t2 = r * 8
       e7 = load grid[t2 + 7]
       total1 = e0 + e7            ; checked i32 addition
       goto pre
pre:   total2 = phi(entry: 0, edge: total1)
       goto head
head:  c1 = phi(pre: 0, body: c2)
       total3 = phi(pre: total2, body: total4)
       if c1 < 8 goto body else done
body:  check 0 <= r < 8            ; grid[r, c]
       check 0 <= c1 < 8
       t3 = r * 8
       v = load grid[t3 + c1]
       total4 = total3 + v         ; checked i32 addition
       c2 = c1 + 1
       goto head
done:  return total3
```

The multiplication `r * 8` appears three times, and so does the check on `r`, and each repeat is of a different kind:

- In `edge`, `t2 = r * 8` computes the value that `t1` computed three lines earlier, and the second check on `r` repeats the first. Every path to them passes through the first copies, with `r` unchanged. A computation is **fully redundant** at a point when every path from the function's entry to that point has already computed the same value.
- In `body`, `t3 = r * 8` and the check on `r` give the same result on all eight trips around the loop, because `r` never changes inside it. Such a computation is **loop-invariant**.
- Measured against `edge`, `t3` is also **partially redundant**: redundant on some paths to it and not on others. When `with_edges` is true, `r * 8` was computed before the loop began; when it is false, it was not.

Figure 1 shows the three shapes in isolation.

<figure class="vx-figure">
<svg viewBox="0 0 760 380" role="img" aria-label="Three kinds of repeated work, each drawn as a small control-flow graph: full redundancy, partial redundancy and loop invariance" aria-describedby="o6-f1-desc">
<title id="o6-f1-title">Three kinds of repeated work</title>
<desc id="o6-f1-desc">Three small control-flow graphs side by side. Left, full redundancy: a block computing t1 = r * 8 leads through two parallel blocks to a join computing t2 = r * 8, which is drawn dashed as removable. All four edges below t1 carry moving dashes, because r * 8 is already computed on every path to the join. Middle, partial redundancy: a test of with_edges leads left to a block computing t1 = r * 8 and right to an empty block, and both lead to a join computing t3 = r * 8, drawn dashed. Only the edge from the t1 block to the join carries moving dashes; the edge from the empty block is plain and labelled no r * 8. Right, loop invariance: a preheader leads to a header testing c1 below 8, whose true edge goes to a body computing t3 = r * 8, drawn dashed, and whose false edge goes around to an exit block. The back edge from the body to the header carries moving dashes, labelled previous trip. A legend explains the solid outlined box, the dashed box and the moving dashes.</desc>
<defs><marker id="o6-f1-head" viewBox="0 0 10 10" refX="10" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker></defs>
<text class="vx-text" x="20" y="24">Full</text>
<text class="vx-text-muted" x="20" y="42">every path computed it</text>
<rect class="vx-box-accent" x="65" y="62" width="130" height="32" rx="4"/>
<text class="vx-mono" x="78" y="83">t1 = r * 8</text>
<rect class="vx-box" x="30" y="132" width="80" height="28" rx="4"/>
<text class="vx-mono" x="70" y="151" text-anchor="middle">…</text>
<rect class="vx-box" x="150" y="132" width="80" height="28" rx="4"/>
<text class="vx-mono" x="190" y="151" text-anchor="middle">…</text>
<rect class="vx-box-bad" x="65" y="200" width="130" height="32" rx="4"/>
<text class="vx-mono" x="78" y="221">t2 = r * 8</text>
<path class="vx-flow" d="M112 94 L78 131" marker-end="url(#o6-f1-head)"/>
<path class="vx-flow" d="M148 94 L182 131" marker-end="url(#o6-f1-head)"/>
<path class="vx-flow" d="M72 160 L106 199" marker-end="url(#o6-f1-head)"/>
<path class="vx-flow" d="M188 160 L154 199" marker-end="url(#o6-f1-head)"/>
<text class="vx-text-muted" x="20" y="322">t2 is fully redundant:</text>
<text class="vx-text-muted" x="20" y="338">reuse t1 and delete t2</text>
<text class="vx-text" x="270" y="24">Partial</text>
<text class="vx-text-muted" x="270" y="42">some paths computed it</text>
<rect class="vx-box" x="315" y="62" width="130" height="32" rx="4"/>
<text class="vx-mono" x="328" y="83">with_edges ?</text>
<rect class="vx-box-accent" x="272" y="132" width="108" height="28" rx="4"/>
<text class="vx-mono" x="283" y="151">t1 = r * 8</text>
<rect class="vx-box" x="400" y="132" width="80" height="28" rx="4"/>
<text class="vx-mono" x="440" y="151" text-anchor="middle">…</text>
<rect class="vx-box-bad" x="315" y="200" width="130" height="32" rx="4"/>
<text class="vx-mono" x="328" y="221">t3 = r * 8</text>
<path class="vx-line" d="M362 94 L332 131" marker-end="url(#o6-f1-head)"/>
<path class="vx-line" d="M398 94 L432 131" marker-end="url(#o6-f1-head)"/>
<path class="vx-flow" d="M326 160 L358 199" marker-end="url(#o6-f1-head)"/>
<path class="vx-line" d="M440 160 L404 199" marker-end="url(#o6-f1-head)"/>
<text class="vx-text-muted" x="446" y="186">no r * 8</text>
<text class="vx-text-muted" x="270" y="322">t3 is partially redundant: insert</text>
<text class="vx-text-muted" x="270" y="338">r * 8 on the right, then reuse</text>
<text class="vx-text" x="530" y="24">Loop-invariant</text>
<text class="vx-text-muted" x="530" y="42">same value on every trip</text>
<rect class="vx-box" x="575" y="62" width="130" height="32" rx="4"/>
<text class="vx-text" x="588" y="83">preheader</text>
<rect class="vx-box" x="575" y="132" width="130" height="28" rx="4"/>
<text class="vx-mono" x="588" y="151">c1 &lt; 8 ?</text>
<rect class="vx-box-bad" x="575" y="200" width="130" height="32" rx="4"/>
<text class="vx-mono" x="588" y="221">t3 = r * 8</text>
<rect class="vx-box" x="575" y="262" width="130" height="28" rx="4"/>
<text class="vx-text" x="588" y="281">exit</text>
<path class="vx-line" d="M640 94 L640 131" marker-end="url(#o6-f1-head)"/>
<path class="vx-line" d="M640 160 L640 199" marker-end="url(#o6-f1-head)"/>
<text class="vx-text-muted" x="646" y="184">true</text>
<path class="vx-flow" d="M575 216 L552 216 L552 146 L574 146" marker-end="url(#o6-f1-head)"/>
<text class="vx-text-accent" x="530" y="120">previous trip</text>
<path class="vx-line" d="M705 146 L728 146 L728 276 L706 276" marker-end="url(#o6-f1-head)"/>
<text class="vx-text-muted" x="709" y="138">false</text>
<text class="vx-text-muted" x="530" y="322">t3 is loop-invariant: hoist</text>
<text class="vx-text-muted" x="530" y="338">it into the preheader</text>
<rect class="vx-box-accent" x="20" y="356" width="22" height="14" rx="3"/>
<text class="vx-text-muted" x="48" y="367">computes r * 8</text>
<rect class="vx-box-bad" x="190" y="356" width="22" height="14" rx="3"/>
<text class="vx-text-muted" x="218" y="367">repeats it</text>
<path class="vx-flow" d="M380 363 L420 363"/>
<text class="vx-text-muted" x="428" y="367">r * 8 already computed on this path</text>
</svg>
<figcaption>Figure 1. Three kinds of repeated work, with the multiplication from <code>row_total</code>. Left: every path to the dashed <code>t2</code> passes <code>t1</code> first, so <code>t2</code> can reuse it. Middle: only the left path has computed <code>r * 8</code> when it reaches the join. Right: the loop body computes the same product on every trip; the back edge brings control round from a trip that already computed it. The moving dashes mark the edges along which <code>r * 8</code> is already there.</figcaption>
</figure>

Each shape has its own transformation. **Common subexpression elimination** (CSE) removes a fully redundant computation by reusing the earlier result; the name comes from a subexpression, like `r * 8`, that several expressions have in common. **Global value numbering** (GVN) finds full redundancy by comparing values rather than names, across a whole function. **Partial redundancy elimination** (PRE) inserts the computation on the paths that lack it, which turns a partial redundancy into a full one, and then removes that. **Loop-invariant code motion** (LICM) moves invariant computations out of a loop, so that they run once each time the loop starts instead of once per trip.

The last two are closer than they look. Morel and Renvoise observed in 1979 that compilers removed redundant computations and moved invariant ones out of loops in separate steps, loop by loop, and proposed doing both at once, moving each expression straight to the entry of the outermost loop in which it is invariant. They got there by solving a more general problem, computations performed twice on one path, which they named partial redundancies.[^mr79]

For Vortex the stakes are in the kernel. Every trip around its inner loop checks four indices and computes two addresses, and most of that work repeats; the [last section before the exercise](#your-turn-the-kernels-inner-loop) takes the loop apart line by line.

## Value numbering in one block

Comparing text does not find redundancy. In

```text
x = a + b
a = 4
y = a + b
```

the two sums look alike and differ, because `a` changed between them, while in `x = a + b` followed by `y = b + a` they look different and are equal. What matters is the values.

**Value numbering** gives each distinct value a number, its **value number**, and gives two computations the same number when they apply the same operator to operands with the same numbers. Walking a block from top to bottom, it keeps two maps. One takes each name to the number of the value the name holds now. The other takes each **key**, an operator together with the numbers of its operands, to the number of the value that operation produces.

At each operation the numberer looks up the operands' numbers, forms the key and looks the key up. If the key is there, the operation recomputes a known value, and its result can be taken from a name that holds that value. If not, the value is new and gets a fresh number. An assignment moves its target name to the new value, and a name read before any assignment in the block gets a fresh number of its own. Alpern, Wegman and Zadeck describe value numbering as symbolic execution of straight-line code, in which hashing gives each different expression its own number, and credit it to Cocke and Schwartz in 1970.[^awz88]

The first example numbers a block of twelve statements:

--8<-- "includes/examples/optimize/o6-redundancy/value_numbering.cpp.md"

Line by line:

- `t2 = b + a` gets the number of `t1 = a + b`, v2, and becomes the copy `t2 = t1`.
- After `a = 4` the name `a` holds a new value, v5, so `t4 = a + b` is a new value, v6, although its text matches the first line.
- `t5 = t2 * c` matches `t3 = t1 * c`, because `t2` holds v2, like `t1`. Value numbering sees through the copy it made on the second line.
- `x = b * c` gets v7, and then `x = 1` overwrites `x`. When `y = c * b` asks for v7, no name holds it any more, so the product must be computed again. In SSA form this cannot happen, since every name is assigned once and keeps its value for good, one of the conveniences SSA form buys.
- `t6 = t4 + c` computes `(a + b) + c`, and `t8 = a + t7` computes `a + (b + c)`. They get different numbers, v9 and v11, and the next part explains why that is right.

### What goes into the key

Addition and multiplication are **commutative**: `a + b` equals `b + a`. So the numberer puts the two operand numbers of `+` and `*` in order before it forms the key, and `b + a` finds `a + b`. LLVM's `gvn` pass does the same, sorting the operand numbers of every commutative instruction.[^llvm-gvn] For Vortex it is correct for every numeric type. An `f32` sum is the exact sum rounded to nearest ([decision 56](../decisions/numbers.md#d56)), and the exact sum does not depend on the order of its operands. A checked `i32` addition fails exactly when the exact sum does not fit, which again does not depend on the order. The only thing the order could change is which NaN a float operation returns, and no Vortex program can tell NaNs apart ([O1](o1-optimizer-contract.md#vortexs-list)).

Sort the operands in the key, not in the instruction that survives. A runtime error message may name the values involved ([I7](../decisions/implementation.md#i7)), and it should name them in the order the program wrote them; [O1](o1-optimizer-contract.md#vortexs-list) met the same question for rewriting `x * 2` as `x + x`.

**Associativity**, the rule that `(a + b) + c` equals `a + (b + c)`, is another matter. It holds for real numbers, not for `f32`: [O1](o1-optimizer-contract.md#floating-point-identities-that-are-false) showed Goldberg's case, where one grouping gives 1 and the other 0. For checked integers the grouping decides whether the program stops. With `a` = 2147483647, `b` = 1 and `c` = −1, `(a + b) + c` stops at `a + b`, which does not fit in `i32`, while `a + (b + c)` returns `a`. A numberer that regrouped sums before hashing would find more matches and change what programs do, so a Vortex numberer never regroups.

LLVM has a pass, `reassociate`, whose purpose is to reorder commutative expressions so that constant propagation, global CSE, LICM and PRE find more.[^llvm-passes] Its reference manual ties such rewrites of floating-point instructions to the `reassoc` fast-math flag,[^langref] which a Vortex compiler must never set.

### What may be numbered

A key stands for the value of an operation, so only operations whose result depends on nothing but their operands may enter the table. The language decides which those are:

- **Arithmetic that cannot fail**, such as address arithmetic after its checks, comparisons, and every `f32` or `f64` operation, may always enter. Merging two identical float operations never changes a bit, because the operation that survives is the same operation on the same operands.
- **Checks** may enter too, keyed by what they test: the checked value and its limit. A second identical check, reached only after the first has passed, cannot fail. [Conformance 1.4](../specification/conformance.md#14-static-and-dynamic-rules) lets an implementation omit a runtime check only when it proves that the check cannot fail, and the first check is that proof. If the first one fails, the program stops before the second runs, so the error line is the one the original would write.
- **Loads** return the value of an earlier load of the same element only if no store in between may have written it. LLVM's EarlyCSE pass records a generation count with each remembered load and increments the count at every operation that may write memory, so a load matches only loads with no possible store in between.[^llvm-earlycse] Vortex sharpens the question. Storage reached through a `&mut` parameter is not reachable through any other parameter of the same call, and the specification says that code generation may rely on it ([References 9.8](../specification/references.md#98-aliasing)), so a store through the kernel's `c` never invalidates a load from `a` or `b`. [O9](o9-alias-analysis.md) turns that rule into alias analysis.
- **Never numbered:** `print`, whose bytes are the program's output; stores; and calls, since a Vortex function may print, fail or write through a `&mut` parameter.

??? check "A block computes `p = x * y` and later `q = y * x`, both `f32`, with nothing assigned in between. May value numbering replace `q` with `p`? And an earlier `u = (x * y) * z` with a later `w = x * (y * z)`?"

    The first, yes. Both products round the same exact value, so they have the same bits, and even a NaN result cannot be told apart in Vortex. The second, no. The two groupings round different intermediate products, so their bits can differ, and decision 56 forbids treating them as equal. A numberer that finds `w` in the table has a bug in its key.

## Across blocks: follow the dominator tree

A block is a small window. In `row_total`, `t3` repeats a product computed in another block, `edge`, and hoisting it means moving it to yet another block. SSA form makes the step to a whole function short. Every value is defined once and never changes afterwards ([O3](o3-ssa.md#one-name-one-definition)), so a computation made in block X still holds in every block that runs after X, provided X is certain to have run. Dominance says exactly when that is: X dominates Y when every path to Y passes through X ([O2](o2-cfg-and-dominance.md#dominance)).

So walk the dominator tree from the entry, carrying the table down. Entries made in a block must be visible in the blocks it dominates, and must disappear when the walk backs out of the block, since its siblings do not have them. A **scoped hash table**, which opens a new scope on entering a block and discards that scope's entries on leaving it, does this. LLVM's EarlyCSE pass is such a walk: its header comment describes a walk of the dominator tree that removes instructions whose redundancy is plain to see, and it keeps the values available so far in a scoped hash table.[^llvm-earlycse]

LLVM's `gvn` pass organizes the same fact differently. It numbers the whole function, and to reuse a value in block B it scans the instructions with that value's number for one whose block dominates B, which its comments call fast, since each test compares a few depth-first numbers.[^llvm-gvn] That is value numbering over a whole function, the sense of "global" in global value numbering: the whole function, not the whole program.

Dominance, not position in the text, is the test. In `row_total`, `edge` does not dominate `body`, because the path `entry`, `pre`, `head`, `body` avoids it. So the walk removes `t2` and the second check in `edge`, and leaves `t3` and its check in `body`. Two computations in the two arms of an `if` never match each other, since neither arm runs before the other on every path.

The dominator test also misses a case. If both arms of an `if` compute `a + b` and the join computes it again, the join's copy is fully redundant, because every path computed the sum, but no single copy dominates the join. This is the fact that [O4](o4-dataflow.md#the-general-recipe)'s available expressions problem computes, for all expressions at once. Removing the copy takes a phi that picks each arm's result, and LLVM's `gvn` inserts such a phi when every predecessor already has the value.[^llvm-gvn] It is the first step toward PRE.

## Equal values around a loop

Hashing needs the numbers of an operation's operands before the operation itself. Loops break that order. The second example has two counters that always agree:

--8<-- "includes/examples/optimize/o6-redundancy/congruence.ll.md"

Both `%i` and `%j` start at 0 and add 1 on every trip, so `%i.next - %j.next` is always 0. A numberer that visits the loop in order meets the phis first, when `%i.next` and `%j.next` have no numbers yet. It cannot know whether the phis are equal, so it must treat them as different, and then `%i.next` and `%j.next` get different keys too. LLVM's `gvn` pass gives every phi a number of its own,[^llvm-gvn] and on this file it changes nothing (LLVM 18.1.8 on the owner's M4 Pro, checked on 2026-09-24).

Alpern, Wegman and Zadeck took this on in 1988. Two variables are **equivalent** at a point when they hold the same value whenever control reaches it. Equivalence is undecidable in general, so they defined a static property, **congruence**, which implies equivalence but not the reverse: congruent values are always equal, and some equal values are not found congruent.

Their algorithm is **optimistic** in the sense of [O4](o4-dataflow.md#optimism-where-to-start). It starts by assuming that all values are equal and splits them into more and more classes until every class is consistent, meaning that its members apply the same operator to operands from the same classes. They point out that this is the problem of minimizing a finite-state machine, which Hopcroft's algorithm solves in $O(E \log E)$ time, and that the result is the maximal fixed point, the one with the most equalities.[^awz88] (O4 calls the same kind of answer the least solution, because it orders facts so that the ones claiming more sit lower.)

On the twin counters, the assumption that `%i` equals `%j` makes the two additions congruent, which agrees with the assumption, so nothing ever splits them.

Cliff Click weighed the two approaches in 1995. A hash-based GVN builds its classes bottom-up and cannot find congruences that run around a loop, which the top-down partitioning of Alpern, Wegman and Zadeck finds. In exchange, hashing can apply algebraic identities and fold constants, which their partitioning cannot, and it runs in one linear pass instead of $O(n \log n)$ time.[^click95]

LLVM's `newgvn` pass takes the optimistic road. It places every value first in a class called TOP, whose members count as equal to everything, and a value still in TOP at the end is unreachable. It is sparse, after Gargi's predicated value numbering, so it revisits only the instructions whose operands changed class, and it borrows a technique from Pai to match a phi of operations against an operation of phis.[^llvm-newgvn] The output above is `newgvn`'s: `%j` and `%j.next` are gone, and the subtraction is the constant 0.

The price of optimism is the one O4 found for SCCP. Until the iteration finishes, the classes claim equalities that are not yet justified, so only the final partition may be used.

Twin counters are not exotic in Vortex. Fusing two loops ([P7](p7-loop-transformations.md)) can leave two such counters in one loop, and so can a `while` loop that keeps its own index beside a counter. [O8](o8-loops.md) finds the same fact another way, by describing both as one induction variable.

??? check "Change the file so that `%j` starts at 1. What can an optimistic numbering conclude now?"

    That the two counters differ. Their phis receive different constants from the entry, 0 and 1, so the class splits on the first round, and the two additions split with it. The subtraction is always −1, but value numbering proves equality, not constant differences between unequal values, so `newgvn` keeps both counters and the subtraction (checked with LLVM 18.1.8). Relating `%i` and `%j` by a fixed difference is induction-variable analysis, in [O8](o8-loops.md).

## Partial redundancy

Back to the simplest partial redundancy, a diamond: one arm computes `a + b`, the other does not, and the join computes it again. The remedy is to insert a copy of the computation on the arm that lacks it. Then every path to the join has computed the sum, the join's copy is fully redundant, and a phi can replace it. The third example shows LLVM doing this:

--8<-- "includes/examples/optimize/o6-redundancy/pre.ll.md"

The edge from `%entry` to `%join` is critical: it leaves a block with two successors and enters a block with two predecessors, so no block lies on it where a new addition could go ([O2](o2-cfg-and-dominance.md#critical-edges)). GVN splits it first, creating `%entry.join_crit_edge`, places the addition `%.pre` there, and replaces `%y` with the phi `%y.pre-phi`. Every path now adds once; the path through `%left` used to add twice.

Three rules keep PRE correct and worth doing.

- **Safety.** An insertion must not add a computation to a path that did not have one. Knoop, Rüthing and Steffen point out that this is also what keeps the transformation from changing whether a program can fail, through a division by zero or an overflow.[^krs92] (Where it fails, relative to the program's output, is a further question, which Vortex adds below.) In the diamond the new addition lies on the one path that reaches the join without the sum, and the original computes the sum at the join on that path anyway.
- **Room to insert.** Code that must run on one edge only needs a block of its own, so critical edges are split first.[^krs92] LLVM's scalar PRE defers the work: when the edge it needs is critical, it records the edge, splits it, and makes the insertion on its next pass over the function.[^llvm-gvn]
- **Profit.** The point is to shorten paths, never to lengthen one. Click describes PRE's rule as hoisting a loop-invariant computation only when no path gets longer.[^click95] LLVM's scalar PRE is stricter still. It inserts in at most one predecessor, the case its comments call the basic diamond; it leaves comparisons and address computations alone, because the phi would keep a later pass from sinking them back beside their uses, forcing a comparison's result out of the processor's flags into a general register and keeping an address alive longer; and it gives up when a predecessor reaches the block through a loop's back edge.[^llvm-gvn]

A loop is a partial redundancy too. An invariant computation at the top of a loop body is redundant on every path that arrives round the back edge, since the previous trip computed the same value, and not on the path from the preheader. An insertion in the preheader makes it fully redundant, which is how Morel and Renvoise move an expression to the entry of the outermost loop in which it is invariant.[^mr79] And there safety bites: a preheader insertion is safe only if the loop body is certain to run.

## Lazy code motion

Morel and Renvoise's equations are **bidirectional**: the fact at a point depends both on its predecessors and on its successors, which makes them harder to understand and to solve than the one-way problems of O4. In 1992 Knoop, Rüthing and Steffen decomposed the placement problem into a backward analysis followed by a forward one, as cheap as the standard bit-vector problems, and added two more one-way analyses that stop code from moving further than it must.[^krs92] Their method is **lazy code motion**, and the fourth example implements it.

The paper works on a flow graph whose nodes are single statements and treats one term t, such as `a + b`, at a time; handling all terms at once is the same computation with one bit per term, as in O4. Two local facts describe a node n: **Used**(n) holds when n computes t, and **Transp**(n), for *transparent*, holds when n assigns neither operand of t, that is, when t is not in n's kill set from [O4](o4-dataflow.md#the-general-recipe). For a graph of basic blocks, a footnote replaces Used by local anticipability, which holds when the block computes t before changing either operand.[^krs92]

Every edge that enters a node with several predecessors gets an empty node of its own, so an insertion always goes at the entry of some node, and no critical edge is left.[^krs92]

**Down-safety** comes first. A node is down-safe when every path from it to the end computes t before either operand changes, so that a computation of t placed at its entry would be used on every path. It is O4's very busy expressions problem for a single term; Morel and Renvoise called it anticipability.[^krs92] It runs backward, and its greatest solution is the answer:

$$
\mathrm{DSafe}(n) =
\begin{cases}
\text{false} & \text{if } n \text{ is the end node} \\
\mathrm{Used}(n) \lor \Big( \mathrm{Transp}(n) \land \displaystyle\bigwedge_{m \in \mathrm{succ}(n)} \mathrm{DSafe}(m) \Big) & \text{otherwise}
\end{cases}
$$

Placing `h = t` only at down-safe nodes is the form of safety the paper works with: every inserted computation is matched, on every path below it, by an original one. It is stronger than adding no computation to any path, and the paper proves that for the earliest placements the two agree.[^krs92]

**Earliest** comes next. A node is earliest when, on some path from the start, no node before it could have computed the same value safely: it is the start node, or it is reached along an edge from a predecessor that changes an operand, or from a predecessor that is not down-safe and is itself earliest. It runs forward, and its least solution is the answer:

$$
\mathrm{Earliest}(n) =
\begin{cases}
\text{true} & \text{if } n \text{ is the start node} \\
\displaystyle\bigvee_{m \in \mathrm{pred}(n)} \Big( \lnot\mathrm{Transp}(m) \lor \big( \lnot\mathrm{DSafe}(m) \land \mathrm{Earliest}(m) \big) \Big) & \text{otherwise}
\end{cases}
$$

Inserting `h = t` at every node that is both down-safe and earliest, and replacing every original computation of t by `h`, is what the paper calls the **safe-earliest transformation**. It is **computationally optimal**: no safe placement computes t fewer times on any path. On the paper's own example it gives essentially what Morel and Renvoise's algorithm gives.[^krs92] But it moves computations as far up as they can go, even when that gains nothing, and every node between an insertion and its use must keep `h` alive, a cost in registers with no gain in speed.

The laziness takes two more analyses, with a local step between them. **Delay**, forward with the greatest solution, marks how far an insertion can slide down from its earliest point without passing an original computation. **Latest**, computed node by node from Delay, marks where it must stop: at a node that uses t, or one with a successor it cannot slide into.

$$
\mathrm{Delay}(n) = \big( \mathrm{DSafe}(n) \land \mathrm{Earliest}(n) \big) \lor \bigwedge_{m \in \mathrm{pred}(n)} \big( \lnot\mathrm{Used}(m) \land \mathrm{Delay}(m) \big)
$$

where the conjunction counts as false at the start node, and

$$
\mathrm{Latest}(n) = \mathrm{Delay}(n) \land \Big( \mathrm{Used}(n) \lor \lnot \bigwedge_{m \in \mathrm{succ}(n)} \mathrm{Delay}(m) \Big)
$$

**Isolated**, the second analysis, backward with the greatest solution, catches an insertion whose value would serve only the node where it is placed, which is no better than leaving that node alone:

$$
\mathrm{Isolated}(n) = \bigwedge_{m \in \mathrm{succ}(n)} \Big( \mathrm{Latest}(m) \lor \big( \lnot\mathrm{Used}(m) \land \mathrm{Isolated}(m) \big) \Big)
$$

Lazy code motion inserts `h = t` at the nodes that are latest and not isolated, and replaces t by `h` at every node that uses t, unless that node is latest and isolated. The result is computationally optimal and, among all computationally optimal placements, keeps `h` alive for the least time: it is **lifetime optimal**.[^krs92] In short, each computation goes no earlier than it must and as late as it can.

--8<-- "includes/examples/optimize/o6-redundancy/lazy_code_motion.cpp.md"

In the diamond, the only node that is both down-safe and earliest is node 2, the branch right after `a = input`, so the safe-earliest transformation, printed as the earliest placement, puts `h = a + b` there. Delay carries the insertion down both edges: on the left to node 3, which computes `a + b` and stops it, and on the right to node 5, the empty node before the join, which cannot pass it on, because the join's other predecessor, node 4, lies below the original computation at node 3. So lazy code motion inserts at nodes 3 and 5 and uses `h` at 3 and 6. Figure 2 follows the insertion down and compares how long `h` lives.

<figure class="vx-figure">
<svg viewBox="0 0 760 400" role="img" aria-label="Lazy code motion on the diamond: the insertion of h = a + b slides from node 2 down to nodes 3 and 5, and h lives for fewer nodes than with the earliest placement" aria-describedby="o6-f2-desc">
<title id="o6-f2-title">From the earliest placement to the latest</title>
<desc id="o6-f2-desc">Left: the flow graph of the diamond, eight nodes. Node 0, start, leads to node 1, a = input, then to node 2, the branch c, drawn with a strong outline and labelled earliest: h = a + b. Node 2 leads left to node 3, x = a + b, which leads to node 4, empty, and right to node 5, empty. Nodes 4 and 5 both lead to node 6, y = a + b, which leads to node 7, end. Nodes 3 and 5 are labelled latest. Two dots start at node 2, pause, then move down the two edges, one to node 3 and one to node 5, pause again, and repeat. Right: a comparison of how long h lives. On the left path, nodes 2, 3, 4 and 6, the earliest placement keeps h alive from node 2 to node 6, and the lazy placement from node 3 to node 6. On the right path, nodes 2, 5 and 6, the earliest placement keeps h alive from node 2 to node 6, and the lazy placement from node 5 to node 6. A note says that both placements compute a + b once on every path, where the original computed it twice on the left, and that the lazy one holds a register for less time.</desc>
<defs><marker id="o6-f2-head" viewBox="0 0 10 10" refX="10" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker></defs>
<rect class="vx-box" x="110" y="16" width="150" height="28" rx="4"/>
<text class="vx-text-muted" x="120" y="35">0</text>
<text class="vx-mono" x="142" y="35">start</text>
<rect class="vx-box" x="110" y="62" width="150" height="28" rx="4"/>
<text class="vx-text-muted" x="120" y="81">1</text>
<text class="vx-mono" x="142" y="81">a = input</text>
<rect class="vx-box-strong" x="110" y="108" width="150" height="28" rx="4"/>
<text class="vx-text-muted" x="120" y="127">2</text>
<text class="vx-mono" x="142" y="127">c ?</text>
<text class="vx-text-accent" x="268" y="127">earliest: h = a + b</text>
<rect class="vx-box" x="20" y="170" width="150" height="28" rx="4"/>
<text class="vx-text-muted" x="30" y="189">3</text>
<text class="vx-mono" x="52" y="189">x = a + b</text>
<text class="vx-text-accent" x="20" y="163">latest</text>
<rect class="vx-box" x="20" y="226" width="150" height="28" rx="4"/>
<text class="vx-text-muted" x="30" y="245">4</text>
<text class="vx-text-muted" x="52" y="245">(empty)</text>
<rect class="vx-box" x="200" y="198" width="150" height="28" rx="4"/>
<text class="vx-text-muted" x="210" y="217">5</text>
<text class="vx-text-muted" x="232" y="217">(empty)</text>
<text class="vx-text-accent" x="356" y="217">latest</text>
<rect class="vx-box" x="110" y="290" width="150" height="28" rx="4"/>
<text class="vx-text-muted" x="120" y="309">6</text>
<text class="vx-mono" x="142" y="309">y = a + b</text>
<rect class="vx-box" x="110" y="346" width="150" height="28" rx="4"/>
<text class="vx-text-muted" x="120" y="365">7</text>
<text class="vx-mono" x="142" y="365">end</text>
<path class="vx-line" d="M185 44 L185 61" marker-end="url(#o6-f2-head)"/>
<path class="vx-line" d="M185 90 L185 107" marker-end="url(#o6-f2-head)"/>
<path class="vx-line" d="M150 136 L100 169" marker-end="url(#o6-f2-head)"/>
<path class="vx-line" d="M220 136 L268 197" marker-end="url(#o6-f2-head)"/>
<path class="vx-line" d="M95 198 L95 225" marker-end="url(#o6-f2-head)"/>
<path class="vx-line" d="M95 254 L148 289" marker-end="url(#o6-f2-head)"/>
<path class="vx-line" d="M275 226 L222 289" marker-end="url(#o6-f2-head)"/>
<path class="vx-line" d="M185 318 L185 345" marker-end="url(#o6-f2-head)"/>
<circle class="vx-dot" r="6"><animateMotion dur="6s" repeatCount="indefinite" path="M185 136 L100 169" keyPoints="0;0;1;1" keyTimes="0;0.3;0.7;1" calcMode="linear"/></circle>
<circle class="vx-dot" r="6"><animateMotion dur="6s" repeatCount="indefinite" path="M185 136 L270 197" keyPoints="0;0;1;1" keyTimes="0;0.3;0.7;1" calcMode="linear"/></circle>
<text class="vx-text" x="450" y="34">How long h lives</text>
<text class="vx-text-muted" x="450" y="74">left path</text>
<text class="vx-mono" x="560" y="74" text-anchor="middle">2</text>
<text class="vx-mono" x="605" y="74" text-anchor="middle">3</text>
<text class="vx-mono" x="650" y="74" text-anchor="middle">4</text>
<text class="vx-mono" x="695" y="74" text-anchor="middle">6</text>
<text class="vx-text-muted" x="450" y="98">earliest</text>
<rect class="vx-box" x="555" y="88" width="145" height="12" rx="3"/>
<text class="vx-text-muted" x="450" y="120">lazy</text>
<rect class="vx-box-accent" x="600" y="110" width="100" height="12" rx="3"/>
<text class="vx-text-muted" x="450" y="164">right path</text>
<text class="vx-mono" x="560" y="164" text-anchor="middle">2</text>
<text class="vx-mono" x="605" y="164" text-anchor="middle">5</text>
<text class="vx-mono" x="650" y="164" text-anchor="middle">6</text>
<text class="vx-text-muted" x="450" y="188">earliest</text>
<rect class="vx-box" x="555" y="178" width="100" height="12" rx="3"/>
<text class="vx-text-muted" x="450" y="210">lazy</text>
<rect class="vx-box-accent" x="600" y="200" width="55" height="12" rx="3"/>
<text class="vx-text-muted" x="450" y="256">Both placements compute a + b once</text>
<text class="vx-text-muted" x="450" y="274">on every path; the original computed</text>
<text class="vx-text-muted" x="450" y="292">it twice on the left. The lazy one</text>
<text class="vx-text-muted" x="450" y="310">keeps h alive for fewer nodes, so it</text>
<text class="vx-text-muted" x="450" y="328">holds a register for less time.</text>
</svg>
<figcaption>Figure 2. Lazy code motion on the diamond of the fourth example. Node 2 is the only node that is both down-safe and earliest, so the safe-earliest transformation inserts <code>h = a + b</code> there. The moving dots are the delay: the insertion slides down each edge until node 3, which computes <code>a + b</code> itself, and node 5, the last node before the join on the right. Right: the stretch of each path over which <code>h</code> must stay alive under each placement.</figcaption>
</figure>

The two loops show safety at work. In the `while` loop, tested at the top, the path from the preheader through the header straight to the end never computes `a + b`, so the preheader is not down-safe, and the only safe place for `h` is the body itself. The safe-earliest transformation "inserts" at node 4, the computation's own node, and lazy code motion sees that such an insertion would serve only that node and changes nothing.

The same loop **rotated**, with a guard `0 < n` in front and the test moved to the bottom of the body, runs its body at least once whenever control reaches the preheader. Now the preheader, node 3, is down-safe and earliest, the insertion cannot slide into the loop without being repeated there, and `a + b` leaves the loop (Figure 3). LLVM's loop documentation gives rotation the same motivation: a guarded loop with its test at the bottom lets invariant instructions, loads especially, move into the preheader without running on the path that skips the loop.[^llvm-loops] A Vortex `for` loop with constant bounds, such as the kernel's `0..64`, has a guard whose condition is a constant, so once rotated its body is known to run.

<figure class="vx-figure">
<svg viewBox="0 0 760 360" role="img" aria-label="A loop tested at the top keeps a + b in its body, because one path never computes it; the rotated loop computes it once in the preheader" aria-describedby="o6-f3-desc">
<title id="o6-f3-title">Why rotation makes hoisting safe</title>
<desc id="o6-f3-desc">Two loops side by side. Left, tested at the top: a = input leads to a preheader, drawn dashed and holding the proposed h = a + b with a question mark, labelled not down-safe. The preheader leads to the header, i below n, whose true edge goes to the body, x = a + b and i = i + 1, which loops back to the header. The header's false edge runs around the right side to the end block and carries moving dashes, labelled false: no a + b, because that path never computes the sum. Right, rotated: a = input leads to a guard, 0 below n. Its true edge leads to the preheader, drawn with an accent outline and holding h = a + b, and on to the body, x = h, i = i + 1 and the test i below n, which loops back to itself and then leads to the end. The guard's false edge runs around the right side to the end. The two edges from the guard through the preheader into the body carry moving dashes, and a label says down-safe: every path computes it.</desc>
<defs><marker id="o6-f3-head" viewBox="0 0 10 10" refX="10" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker></defs>
<text class="vx-text" x="20" y="24">Tested at the top</text>
<text class="vx-text-muted" x="20" y="42">the loop may run zero times</text>
<rect class="vx-box" x="90" y="60" width="170" height="28" rx="4"/>
<text class="vx-mono" x="102" y="79">a = input</text>
<rect class="vx-box-bad" x="90" y="112" width="170" height="28" rx="4"/>
<text class="vx-mono" x="102" y="131">h = a + b ?</text>
<text class="vx-text-muted" x="266" y="131">not down-safe</text>
<rect class="vx-box" x="90" y="164" width="170" height="28" rx="4"/>
<text class="vx-mono" x="102" y="183">i &lt; n ?</text>
<rect class="vx-box" x="90" y="216" width="170" height="46" rx="4"/>
<text class="vx-mono" x="102" y="235">x = a + b</text>
<text class="vx-mono" x="102" y="254">i = i + 1</text>
<rect class="vx-box" x="90" y="300" width="170" height="28" rx="4"/>
<text class="vx-text" x="102" y="319">end</text>
<path class="vx-line" d="M175 88 L175 111" marker-end="url(#o6-f3-head)"/>
<path class="vx-line" d="M175 140 L175 163" marker-end="url(#o6-f3-head)"/>
<path class="vx-line" d="M175 192 L175 215" marker-end="url(#o6-f3-head)"/>
<text class="vx-text-muted" x="181" y="208">true</text>
<path class="vx-line" d="M90 240 L66 240 L66 178 L89 178" marker-end="url(#o6-f3-head)"/>
<path class="vx-flow" d="M260 178 L300 178 L300 314 L261 314" marker-end="url(#o6-f3-head)"/>
<text class="vx-text-accent" x="306" y="236">false:</text>
<text class="vx-text-accent" x="306" y="252">no a + b</text>
<text class="vx-text" x="400" y="24">Rotated</text>
<text class="vx-text-muted" x="400" y="42">the body runs at least once</text>
<rect class="vx-box" x="490" y="60" width="160" height="28" rx="4"/>
<text class="vx-mono" x="502" y="79">a = input</text>
<rect class="vx-box" x="490" y="112" width="160" height="28" rx="4"/>
<text class="vx-mono" x="502" y="131">0 &lt; n ?</text>
<rect class="vx-box-accent" x="490" y="164" width="160" height="28" rx="4"/>
<text class="vx-mono" x="502" y="183">h = a + b</text>
<rect class="vx-box" x="490" y="216" width="160" height="64" rx="4"/>
<text class="vx-mono" x="502" y="235">x = h</text>
<text class="vx-mono" x="502" y="254">i = i + 1</text>
<text class="vx-mono" x="502" y="273">i &lt; n ?</text>
<rect class="vx-box" x="490" y="318" width="160" height="28" rx="4"/>
<text class="vx-text" x="502" y="337">end</text>
<path class="vx-line" d="M570 88 L570 111" marker-end="url(#o6-f3-head)"/>
<path class="vx-flow" d="M570 140 L570 163" marker-end="url(#o6-f3-head)"/>
<text class="vx-text-muted" x="576" y="156">true</text>
<path class="vx-flow" d="M570 192 L570 215" marker-end="url(#o6-f3-head)"/>
<path class="vx-line" d="M490 262 L466 262 L466 230 L489 230" marker-end="url(#o6-f3-head)"/>
<path class="vx-line" d="M570 280 L570 317" marker-end="url(#o6-f3-head)"/>
<path class="vx-line" d="M650 126 L700 126 L700 332 L651 332" marker-end="url(#o6-f3-head)"/>
<text class="vx-text-muted" x="706" y="230">false</text>
<text class="vx-text-accent" x="400" y="174">down-safe:</text>
<text class="vx-text-accent" x="400" y="190">every path</text>
<text class="vx-text-accent" x="400" y="206">computes it</text>
</svg>
<figcaption>Figure 3. The loop of the fourth example, before and after rotation. Left: tested at the top, the loop may run zero times, and the moving dashes mark the path that leaves at once without computing <code>a + b</code>. An insertion in the preheader would add a computation to that path, so lazy code motion leaves <code>a + b</code> in the body. Right: behind a guard, the body runs at least once whenever control reaches the preheader, so <code>a + b</code> is down-safe there and moves out of the loop, and the body uses <code>h</code>.</figcaption>
</figure>

??? check "The safe-earliest transformation 'inserts' `h = a + b` at node 4 of the `while` loop, the node that already computes it. What does that mean, and why does lazy code motion report nothing?"

    Node 4 is the earliest safe point: no node above it is down-safe, because the path from the header to the end skips the body. Inserting `h = a + b` at node 4 and replacing `x = a + b` by `x = h` computes exactly what the original computes, plus a copy. Lazy code motion finds node 4 latest, since it uses t, and isolated, since `h` would serve no node but node 4 itself, so it inserts nothing and replaces nothing.

Lazy code motion works on names: it moves `a + b` where the same variables `a` and `b` are added. VanDrunen and Hosking name the two blind spots this leaves. PRE traditionally considers only expressions that are lexically the same, and GVN only operations that are fully redundant. Their hybrid algorithm combines the two, with dataflow equations for insertion points stated over values rather than names, on top of a simple hash-based GVN.[^vh04] GCC's PRE pass works on values in this way: the comment at the top of its source says that it adds insertion to GCC's value numbering to form GVN-PRE.[^gcc-pre]

## Loop-invariant code motion

LICM attacks loops directly. A computation is loop-invariant when each of its operands is defined outside the loop or by another invariant computation, so a pass can find them all by repeating that test until nothing new qualifies. LLVM's LICM pass hoists code into the preheader, sinks it to the exit blocks when that is safe, and promotes memory locations to registers.[^llvm-passes] It avoids the repetition by visiting the loop's blocks in dominator-tree order, which reaches every definition before its uses, so one walk hoists the whole body. It hoists an instruction only when three conditions hold:[^llvm-licm]

1. **Its operands are loop-invariant.**
2. **Memory allows it.** A load may move only if nothing stored in the loop can change what it reads. LLVM answers that question with alias analysis and with MemorySSA, whose documentation names LICM as one of its users.[^llvm-licm][^llvm-mssa]
3. **Running it unconditionally is safe.** Either the instruction cannot trap, so running it on a path that would not have run it is harmless, which is called **speculative execution**, or it is **guaranteed to execute** whenever the loop is entered. LLVM's test for the second, in the form LICM uses, is that nothing earlier in the instruction's block may stop control from reaching it, and that its block lies on every path from the header to an exit or to a latch.[^llvm-must] When a load with an invariant address passes neither test, LICM writes the missed remark "failed to hoist load with loop-invariant address because load is conditionally executed",[^llvm-licm] the kind of explanation [O1](o1-optimizer-contract.md#for-vortex) asked a Vortex compiler to give.

The fifth example runs LLVM's LICM on three rotated loops:

--8<-- "includes/examples/optimize/o6-redundancy/licm.ll.md"

The output shows several things at once.

- Each loop now has a preheader, `loop.preheader`, and a dedicated exit, `done.loopexit`, which `opt` created before LICM ran. That is LLVM's canonical loop form, whose single entry edge into the header exists to simplify passes such as LICM.[^llvm-passes]
- In `@scale_row`, the address arithmetic `%base = mul i64 %row, %n` moved into the preheader, and so did the load of `%factor`. The multiplication cannot trap. The load lies in the loop's only block, so it runs on every trip, and `%out` is `noalias`, which promises that memory written through `%out` is not reached through the other arguments while the function runs,[^langref] so the store to `out[j]` cannot change `factor[0]`.
- In `@scale_row_may_alias`, the same loop without `noalias`, the multiplication moved and the load stayed. Every trip's store might write `factor[0]`, so every trip must read it again.
- In `@sum_into`, LICM applied **scalar promotion**, which keeps a memory location in a register for the whole loop: one load in the preheader (`%acc.promoted`), a phi carrying the running value, and one store in the exit block. The store takes its value from `%new.lcssa`, a **loop-closing phi**: in LLVM's loop-closed SSA form, a value defined in a loop and used after it passes through a one-entry phi in an exit block.[^llvm-loops]
- LLVM's conditions for promotion are that the address is loop-invariant, that no other load or store in the loop may touch the location, and that no call in the loop may read or write it. The move must also add no store to a path that had none, and a store that is guaranteed to execute, as this loop's only block guarantees, settles that.[^llvm-licm]

The additions in `@sum_into` still happen one at a time and in the original order, so every bit of the sum is unchanged, which is why [O1](o1-optimizer-contract.md#your-turn-the-stage-10-kernel-under-the-contract) could allow the same change for a kernel that adds straight into `c[row, column]`. [P7](p7-loop-transformations.md) describes the kernel's local `sum` as the same transformation, scalar replacement, applied by hand.

`noalias` is the whole difference between the first two functions, and it is what [References 9.8](../specification/references.md#98-aliasing) gives a Vortex compiler for every `&mut` parameter. [O9](o9-alias-analysis.md) turns the rule into LLVM attributes and checks that the loops that should benefit do.

Hoisting has a cost the examples do not show: a value computed before the loop occupies a register for the whole loop. LLVM treats hoisting as a canonicalization that enables later passes and leaves it to the back end, which knows the register pressure, to rematerialize hoisted values when registers run short.[^llvm-passes] [C5](../backend/c5-spilling.md) covers rematerialization. The laziness of lazy code motion is the middle end's own answer to the same problem.

Click's **global code motion** (GCM) comes at placement from the other side. His GVN merges congruent instructions without regard to where they sit, and GCM then places every instruction afresh, using only its dependences. It schedules each instruction early, in the first block that its inputs dominate; then late, in the last block that still dominates all its uses; and between the two it picks the block in the shallowest loop nest, then the one on the fewest paths.[^click95]

In GCM, instructions that can fault, such as loads, stores, divisions and calls, keep an explicit dependence on their original block, so they may move down but never up.[^click95] GCM also hoists code used on only some paths through a loop, which Click notes may lengthen a path and pays off when the loop runs at least once.[^click95]

In a production pipeline these passes run more than once, in a fixed order. For LLVM 18.1.8, `opt -passes='default<O2>' -print-pipeline-passes` lists `early-cse` twice, `licm` four times, `gvn` once, after the first two `licm` runs, and `newgvn` not at all (checked on the owner's M4 Pro on 2026-09-24). MLIR provides the two basic transformations as generic passes: `mlir-opt` 18.1.8 lists `--cse` and `--loop-invariant-code-motion`, and [M3](../mlir/m3-passes-and-rewriting.md) uses them. [O10](o10-pass-pipelines.md) takes up the question of order.

## What Vortex allows

The algorithms above assume that a computation's only effect is its value. A Vortex computation can have two more: a runtime check can stop the program with an error line, and the position of that line among the printed output is observable ([O1](o1-optimizer-contract.md#vortexs-list)). Merging, moving and inserting interact with those effects differently:

| Computation | Merge with an identical one that dominates it | Move or insert where it runs on every path anyway | Run on a path that did not run it |
| --- | --- | --- | --- |
| Arithmetic that cannot fail: `f32` and `f64` operations, comparisons, the step of a `for` counter, address arithmetic after its checks | yes | yes | yes, when it pays |
| A check that may fail: bounds, overflow, division by zero, cast range | yes | only if it crosses no `print`, no other check that may fail and nothing that might run forever | no |
| A load through `&` or `&mut` | yes, if no store between them may write that element | yes, once its index is checked or proven in bounds | only if its index is proven in bounds |
| `print`, and calls that may print or fail | no | no | no |

Three consequences stand out.

- **Strict floating point costs nothing here.** Every transformation in this chapter merges or moves an operation without changing it, so [decision 56](../decisions/numbers.md#d56) permits them all. What it forbids, contraction, reassociation and reordering, is a different kind of transformation: it changes which operations run, or which values they combine.
- **Checks are nearly pinned.** Click's faulting instructions may move down but never up. A Vortex check that may fail may move either way, up only to where it runs on every path anyway, but in both directions only within a stretch of code that contains no `print`, no other check that may fail, and nothing that might run forever, such as a `while` loop or a call. The textbook algorithms miss the last condition: Knoop, Rüthing and Steffen define down-safety over paths that reach the end,[^krs92] while [O1](o1-optimizer-contract.md#vortexs-list) counts running forever as behavior.
- **Proofs free checks.** The way to move a check without these limits, or to delete it, is a proof that it never fails, which [O4](o4-dataflow.md#intervals-bounding-a-loop-index)'s intervals supply for the kernel and [O8](o8-loops.md) uses to delete the check outright.

## Your turn: the kernel's inner loop

Here is the `k` loop of the [stage 10](../compiler/guide/stage-10-matrix-multiplication.md#the-program-the-milestone-asks-for) kernel in the SSA form of [O3](o3-ssa.md#your-turn-the-kernel-in-ssa-form), with its checks and address arithmetic written out. Rows are stored one after another ([decision 43](../decisions/arrays.md#d43)), so `a[row, k]` is element `row * 64 + k`. Each check leaves for the error block when it fails, as in O2, so `B` stands for several blocks, listed together here:

```text
P:    sum0 = 0.0
      k0 = 0
      goto H
H:    k1 = phi(P: k0, S: k2)
      sum1 = phi(P: sum0, S: sum2)
      if k1 < 64 goto B else X
B:    (1)  check 0 <= row < 64         ; a[row, k], first index
      (2)  check 0 <= k1 < 64          ; a[row, k], second index
      (3)  t1 = row * 64
      (4)  t2 = t1 + k1
      (5)  x = load a[t2]
      (6)  check 0 <= k1 < 64          ; b[k, column], first index
      (7)  check 0 <= column < 64      ; b[k, column], second index
      (8)  t3 = k1 * 64
      (9)  t4 = t3 + column
      (10) y = load b[t4]
      (11) p = x * y
      (12) sum2 = sum1 + p
S:         k2 = k1 + 1
           goto H
X:    (13) check 0 <= row < 64         ; c[row, column]
      (14) check 0 <= column < 64
      (15) t5 = row * 64
      (16) t6 = t5 + column
      (17) store c[t6] = sum1
```

For each line, decide what it repeats, what removes it, and whether a Vortex compiler may do so without a proof that a check never fails. Three rows are filled in:

| Line | Repeats | Removed by | Allowed? |
| --- | --- | --- | --- |
| (3) `t1 = row * 64` | the same value on every trip | LICM, into P | yes: it cannot fail |
| (2) `check 0 <= k1 < 64` | nothing: `k1` changes on every trip | a range proof ([O8](o8-loops.md)) | not by this chapter's methods |
| (12) `sum2 = sum1 + p` | nothing: a new product on every trip | nothing | it stays, in order (decision 56) |
| (6) `check 0 <= k1 < 64` | ? | ? | ? |
| (1) `check 0 <= row < 64` | ? | ? | ? |
| (7) `check 0 <= column < 64` | ? | ? | ? |
| (8) `t3 = k1 * 64` | ? | ? | ? |
| (5) `x = load a[t2]` | ? | ? | ? |
| (15) `t5 = row * 64` | ? | ? | ? |

??? check "Answers for the kernel"

    - **(6)** is fully redundant with (2): the same value, the same extent, and (2) runs first on every path. Value numbering removes it, legally, since (2) passing proves that (6) cannot fail.
    - **(1)** is loop-invariant and may fail, so hoisting it needs care, and here it is allowed. The loop always runs, since `k0 = 0` is below 64, and (1) is the first thing the first trip does, with nothing printed or checked before it. If it fails, it fails at the same moment, and with the same error line, provided the hoisted check keeps its source position.
    - **(7)** is loop-invariant too, but on every trip (1), (2) and (6) run before it. Hoisted alone, above (1), it would report the wrong index when both `row` and `column` are out of range. It may move into P after (1), keeping their order, because on the first trip (2) and (6) test `k1 = 0` and cannot fail.
    - **(8)** repeats nothing, since `k1` changes. Strength reduction ([O8](o8-loops.md)) replaces the multiplication by an addition of 64 on each trip.
    - **(5)** is not invariant in this loop order, since `t2` changes with `k1`. After the loops are interchanged so that `column` is innermost ([P7](p7-loop-transformations.md)), `a[row, k]` is the same across the inner loop, and LICM loads it once per `k`. That is legal because the stores to `c` cannot change `a` (References 9.8).
    - **(15)** is partially redundant with (3). Every path that runs the body has computed `row * 64`, and the path P, H, X, on which the loop runs zero times, has not. Once LICM moves (3) into P, (3) dominates X and (15) is fully redundant, so GVN replaces `t5` with `t1`; (13) and (14) go the same way once (1) and (7) have moved. Running LICM before GVN is what makes this work, the kind of ordering question [O10](o10-pass-pipelines.md) studies.
    - The body shrinks from twelve lines per trip to eight. Better still, `row` and `column` come from `for` loops over `0..64` into extents of 64, so O4's interval analysis proves that (1), (7), (13) and (14) always pass, and [O8](o8-loops.md) deletes them instead of moving them.

## For Vortex

!!! vortex "Exercise"

    **Build** redundancy elimination on your compiler's SSA form, using the dominator tree from [O2](o2-cfg-and-dominance.md#for-vortex) and the construction from [O3](o3-ssa.md#for-vortex).

    1. **Value numbering in a block.** Key each operation by its operator, its type and the value numbers of its operands, with the two operands of `+` and `*` sorted in the key and never in the instruction. Key a bounds check by the checked value and the extent, and an overflow check by its operation. Let loads enter the table, and drop them at any store or call that may write the same array, using [References 9.8](../specification/references.md#98-aliasing) to keep loads that come through other parameters. Never enter `print`, calls or stores.
    2. **Dominator-scoped numbering.** Carry the table down the dominator tree in a scoped table, so that a computation serves every block its own block dominates.
    3. **Loop-invariant code motion** for innermost loops. Give each loop a preheader if your lowering does not, and hoist computations that cannot fail and whose operands are defined outside the loop, such as the kernel's `row * 64`. Hoist a check only when the loop certainly runs at least once and nothing that prints, may fail or might run forever runs before it on the first trip, other than checks already hoisted ahead of it in their original order. A hoisted check keeps its source position.
    4. **Remarks**, in the stream you built for [O1](o1-optimizer-contract.md#for-vortex): a passed remark for every removal, naming both source positions, and for every hoist; a missed remark for every invariant check left in a loop, with the reason, such as "the loop may run zero times".

    **Not yet:** partial redundancy elimination in general (as a stretch, once everything else passes its tests: lazy code motion for integer expressions that cannot fail); optimistic value numbering; sinking; scalar promotion, and loads hoisted past stores ([O9](o9-alias-analysis.md)); strength reduction and removing checks by range proofs ([O8](o8-loops.md)); any reassociation.

    **Proof that it works:**

    - The contract test from [O1](o1-optimizer-contract.md#for-vortex) passes on the whole suite with these passes on: standard output, the error line and the exit status match the unoptimized build byte for byte.
    - Four programs with golden outputs, one for each thing that must not change: a loop that runs zero times with an integer division by zero in its body, which must print its result and exit with status 0; a loop whose body prints and then overflows a checked multiplication on the first trip, whose output must come before the error line; a read of an `[i32; 8, 8]` array with both indices out of range, whose error must name the first index; and `(x + y) + z` against `x + (y + z)` in `f32`, with the values of Goldberg's example from O1, whose two printed results must differ.
    - A golden remark file for the stage 10 program that records the removed check and the hoisted lines of this chapter's kernel exercise.
    - A measurement for two functions, filled in from your compiler's output, with the date and your compiler's version:

    | Function | Operations per inner trip, before | Operations per inner trip, after | Checks per inner trip, before | Checks per inner trip, after | Passed remarks | Missed remarks |
    | --- | --- | --- | --- | --- | --- | --- |
    | stage 10 `multiply` | | | | | | |
    | `row_total`, this chapter | | | | | | |

## Key ideas

!!! recap "Questions you can now answer"

    - **What makes a computation redundant?** The same value was already computed on every path to it (full), on some paths (partial), or on the previous trip around its loop (invariant).
    - **What does value numbering compare?** Operators and the value numbers of their operands, with commutative operands sorted in the key; never regrouped sums, which for `f32` and checked integers are different computations.
    - **How does SSA form carry CSE across blocks?** A value never changes after its definition, so a computation in a dominating block serves every block below it, and a scoped table carried down the dominator tree finds it.
    - **Why do loops need an optimistic analysis?** Around a back edge, operands have no numbers yet; assuming equality and splitting on contradiction finds congruences that one pass of hashing misses.
    - **What makes a code placement safe?** Inserting only where the computation would run on every path anyway, so that no path gains a computation, or a failure.
    - **Why is lazy code motion lazy?** Among the placements that compute as little as possible, it picks the latest, which keeps the result's lifetime, and so its register, as short as possible.
    - **When may a Vortex compiler hoist a check out of a loop?** When the loop certainly runs and the check crosses no `print`, no other check that may fail first and nothing that might run forever; otherwise only a proof that the check never fails takes it out of the loop.

## Where this comes back

!!! next "You will use this again in"

    - [O7. Calls and aggregates: inlining and SROA](o7-inlining-and-sroa.md): *calls as barriers*, *redundancy exposed by inlining*
    - [O8. Loops: structure, induction variables and bounds checks](o8-loops.md): *preheader*, *loop rotation*, *invariant checks*, *twin induction variables*
    - [O9. Memory: alias analysis and MemorySSA](o9-alias-analysis.md): *what a store kills*, *scalar promotion*, *hoisting loads*
    - [O10. Pass managers and pipelines](o10-pass-pipelines.md): *LICM before GVN*
    - [O11. Undefined behavior, poison and correct optimization](o11-undefined-behavior.md): *speculative execution*
    - [O12. Testing an optimizer](o12-testing-optimizers.md): *zero-trip loops*, *the order of runtime errors*
    - [P7. Loop transformations](p7-loop-transformations.md): *invariant loads after interchange*
    - [P12. Anatomy of a fast GEMM](p12-fast-gemm.md): *register pressure*, *values held across a loop*
    - [C5. Spilling, splitting and rematerialization](../backend/c5-spilling.md): *rematerialization*
    - [M3. Passes and pattern rewriting](../mlir/m3-passes-and-rewriting.md): *CSE and LICM as generic passes*

## Sources and further reading

Read Knoop, Rüthing and Steffen first: the paper is eleven pages, and its equations are the ones the fourth example runs. Then read the first section of Alpern, Wegman and Zadeck for congruence, and Click's paper for hash-based GVN together with global code motion. The LLVM sources cited here are readable; the comment at the top of `NewGVN.cpp` is a short survey of value numbering in its own right.

[^mr79]: E. Morel and C. Renvoise, "Global Optimization by Suppression of Partial Redundancies", *Communications of the ACM* 22(2), 1979, pages 96 to 103: the abstract. <https://doi.org/10.1145/359060.359069>
[^krs92]: Jens Knoop, Oliver Rüthing and Bernhard Steffen, "Lazy Code Motion", *Proceedings of the ACM SIGPLAN 1992 Conference on Programming Language Design and Implementation (PLDI)*, 1992, pages 224 to 234: the abstract, sections 1 to 5, Lemma 3.1, equation systems 3.5, 3.7, 4.2 and 4.7, and footnotes 4, 7 and 8. <https://doi.org/10.1145/143095.143136>
[^awz88]: Bowen Alpern, Mark N. Wegman and F. Kenneth Zadeck, "Detecting Equality of Variables in Programs", *Conference Record of the Fifteenth ACM Symposium on Principles of Programming Languages (POPL)*, 1988, pages 1 to 11: section 1 and the reference list. <https://doi.org/10.1145/73560.73561>
[^click95]: Cliff Click, "Global Code Motion / Global Value Numbering", *Proceedings of the ACM SIGPLAN 1995 Conference on Programming Language Design and Implementation (PLDI)*, 1995, pages 246 to 257: sections 1, 1.1, 1.2, 2, 2.2 and 2.3. <https://doi.org/10.1145/207110.207154>
[^vh04]: Thomas VanDrunen and Antony L. Hosking, "Value-Based Partial Redundancy Elimination", *Compiler Construction (CC 2004)*, Lecture Notes in Computer Science 2985, Springer, 2004, pages 167 to 184: the abstract. <https://doi.org/10.1007/978-3-540-24723-4_12>
[^llvm-passes]: LLVM Project, "LLVM's Analysis and Transform Passes", entries `gvn`, `licm`, `loop-simplify` and `reassociate`. <https://llvm.org/docs/Passes.html>
[^llvm-gvn]: LLVM Project, `GVN.cpp`, release/18.x branch: the file header; `ValueTable::createExpr`, which sorts the operands of commutative instructions; `ValueTable::lookupOrAdd`, which numbers each phi on its own; `findLeader`; `performScalarPRE`; and `performPRE`. <https://github.com/llvm/llvm-project/blob/release/18.x/llvm/lib/Transforms/Scalar/GVN.cpp>
[^llvm-newgvn]: LLVM Project, `NewGVN.cpp`, release/18.x branch: the file header, the comment on `TOPClass`, and `initializeCongruenceClasses`. <https://github.com/llvm/llvm-project/blob/release/18.x/llvm/lib/Transforms/Scalar/NewGVN.cpp>
[^llvm-earlycse]: LLVM Project, `EarlyCSE.cpp`, release/18.x branch: the file header and the comments on `AvailableValues` and `AvailableLoads`. <https://github.com/llvm/llvm-project/blob/release/18.x/llvm/lib/Transforms/Scalar/EarlyCSE.cpp>
[^llvm-licm]: LLVM Project, `LICM.cpp`, release/18.x branch: the file header, the comments on and in `hoistRegion`, `isSafeToExecuteUnconditionally`, and the comment on safety in `promoteLoopAccessesToScalars`. <https://github.com/llvm/llvm-project/blob/release/18.x/llvm/lib/Transforms/Scalar/LICM.cpp>
[^llvm-must]: LLVM Project, `MustExecute.cpp`, release/18.x branch: `ICFLoopSafetyInfo::isGuaranteedToExecute`, the version LICM uses, and `SimpleLoopSafetyInfo::isGuaranteedToExecute`, whose comment states the path condition. <https://github.com/llvm/llvm-project/blob/release/18.x/llvm/lib/Analysis/MustExecute.cpp>
[^llvm-mssa]: LLVM Project, "MemorySSA", sections "Introduction" and "Invalidation and updating". <https://llvm.org/docs/MemorySSA.html>
[^llvm-loops]: LLVM Project, "LLVM Loop Terminology (and Canonical Forms)", sections "Terminology", "Loop Closed SSA (LCSSA)" and "Rotated Loops". <https://llvm.org/docs/LoopTerminology.html>
[^gcc-pre]: GCC Project, `tree-ssa-pre.cc`, releases/gcc-14 branch: the comment at the top of the file. <https://github.com/gcc-mirror/gcc/blob/releases/gcc-14/gcc/tree-ssa-pre.cc>
[^langref]: LLVM Project, "LLVM Language Reference Manual", sections "Parameter Attributes" (`noalias`) and "Fast-Math Flags" (`reassoc`). <https://llvm.org/docs/LangRef.html>

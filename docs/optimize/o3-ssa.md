# O3. SSA form: construction and destruction

<p class="page-intro">Static single assignment form gives every value one definition and marks, with a phi, each place where versions of a variable meet. This chapter builds it three ways, takes it apart again without losing a value, and follows it into the Vortex matrix multiplication kernel, whose <code>sum</code> and loop counters become the values that later chapters optimize.</p>

<p class="vx-meta" markdown="1">Level: Intermediate · Reading time: about 50 minutes · Builds on: [O2. Control-flow graphs and dominance](o2-cfg-and-dominance.md), [Build v0.1, stage 6](../compiler/guide/stage-6-first-machine-code.md#ssa-if-you-use-llvm)</p>

???+ remember "Before you start, remember"

    ??? question "What is the dominance frontier of a block X?"

        The set of blocks where X's dominance ends: Y is in DF(X) when X dominates a predecessor of Y but does not strictly dominate Y.

        Introduced in [O2. Control-flow graphs and dominance](o2-cfg-and-dominance.md#the-dominance-frontier).

    ??? question "Where does a variable assigned in several blocks need a phi?"

        At the iterated dominance frontier of those blocks, counting the entry block as one of them. In the stage 7 loop, `count` needs phis at B and F, and `total` needs one at B.

        Introduced in [O2. Control-flow graphs and dominance](o2-cfg-and-dominance.md#the-dominance-frontier).

    ??? question "What is a critical edge, and what does splitting it do?"

        An edge from a block with several successors to a block with several predecessors. Splitting puts a new, empty block on the edge, so code that must run only when control crosses it has a place to go.

        Introduced in [O2. Control-flow graphs and dominance](o2-cfg-and-dominance.md#critical-edges).

    ??? question "Where does LLVM consider a phi's use of an incoming value to happen?"

        On the edge from the matching predecessor, not in the phi's own block. Every other use must be dominated by the definition of its value.

        Introduced in [O2. Control-flow graphs and dominance](o2-cfg-and-dominance.md#dominance).

    ??? question "What shortcut does stage 6 describe for a front end that emits LLVM IR?"

        Keep each mutable variable in a stack slot created at the start of the function, and let LLVM's mem2reg pass turn the slots into SSA values.

        Introduced in [Build v0.1, stage 6](../compiler/guide/stage-6-first-machine-code.md#ssa-if-you-use-llvm).

!!! goals "In this chapter"

    - Explain what SSA form guarantees, and why the operands of a phi belong to edges rather than to its block.
    - Build minimal and pruned SSA form by hand: place phis at iterated dominance frontiers, then rename with a stack per variable during a walk of the dominator tree.
    - Compare three ways to reach SSA form: Cytron's algorithm, Braun's construction during lowering, and stack slots promoted by LLVM's mem2reg.
    - Translate out of SSA form correctly, recognizing the lost-copy and swap problems and fixing them with split edges, isolated phis and sequentialized parallel copies.
    - Choose how a Vortex compiler reaches SSA form, and check the choice on the matrix multiplication kernel.

## One name, one definition

Here is a small Vortex function:

```vortex
// items: valid
fn adjust(x: i32) -> i32 {
    let mut y = x * 2;
    if x > 5 {
        y = y - 3;
    }
    return y;
}
```

An optimizer looking at `return y` wants to know where the value of `y` came from, and the answer depends on the path: along one path it came from `y = x * 2`, along the other from `y = y - 3`. To find out, a compiler follows every path backwards from the use until it meets an assignment to `y`. The facts it collects are **def-use chains**, one link from each assignment to each use it can reach. Cytron and his coauthors point out that a variable with D assignments and U uses can need D × U of them.[^cytron]

**Static single assignment form**, or **SSA form**, removes the question. A program is in SSA form when each variable is the target of exactly one assignment in the program text.[^cytron] A variable assigned in several places is split into **versions**, one per assignment, and each use is renamed to the version that reaches it:

```text
entry:  y1 = x * 2
        if x > 5 goto then else join
then:   y2 = y1 - 3
        goto join
join:   y3 = phi(entry: y1, then: y2)
        return y3
```

Now `return y3` names its definition, and so does every other use: wherever `y1` appears, it means the value that `y1 = x * 2` computed. The one new construct is the **phi** at the top of `join`, which says that `y3` is `y1` when control arrives from `entry` and `y2` when it arrives from `then`. [Stage 6](../compiler/guide/stage-6-first-machine-code.md#ssa-if-you-use-llvm) showed the straight-line half of this idea; the phi handles the joins.

"Static" refers to the program text. An assignment inside a loop body runs once per iteration and produces a new value each time, but it is still one assignment, and the uses it reaches always see the value from the current iteration.

Rosen, Wegman and Zadeck invented SSA form, and it spread after Cytron and his coauthors showed in 1991 how to construct it efficiently.[^braun] Their paper describes the role of SSA form in an optimizing compiler as three steps: translate into SSA form, optimize, translate back out.[^cytron] By 2013, Braun and his coauthors could name the Java HotSpot VM, LLVM and libFirm as compilers whose intermediate representation is built entirely on SSA form, which they credit with making analyses easier to implement, test and debug as well as faster.[^braun] This chapter covers the two translations. The rest of the book is about the optimizations in between.

Vortex code arrives halfway there. A `let` without `mut` has an initializer and is never assigned again, and a parameter is never rebound, since assigning through a `&mut` parameter writes the memory it refers to ([Declarations 3.2](../specification/declarations.md#32-functions) and [3.5](../specification/declarations.md#35-local-variable-declarations)), so each is already a single-assignment name. Only `let mut` variables need versions, along with the hidden counter that each `for` loop steps. Vortex has no shadowing, but two declarations in sibling blocks may share a name ([3.6](../specification/declarations.md#36-scopes)), so a compiler should give each declaration its own variable, as name resolution in [stage 4](../compiler/guide/stage-4-names-and-scopes.md) already identifies it, and never merge two variables because they are spelled alike.

## Phi functions

A phi at the start of a block has one operand for each predecessor of the block. When control arrives from the j-th predecessor, the phi's value is its j-th operand, and all the phis in a block run before its ordinary statements.[^cytron] LLVM writes the pairing out, with a label beside each operand:

```llvm
%y3 = phi i32 [ %y1, %entry ], [ %y2, %then ]
```

LLVM's reference manual requires one pair per predecessor and requires the phis to come first in their block. It also treats the use of each incoming value as happening on the edge from the matching predecessor, not in the phi's own block.[^langref-phi] That rule, met in [O2](o2-cfg-and-dominance.md#dominance), is why `y2` may appear in `join` although `then` does not dominate `join`: the use is at the end of `then`, where `y2` exists. At a loop header, it lets a phi name a value that the loop body computes later in the program text (Figure 1, right).

<figure class="vx-figure">
<svg viewBox="0 0 760 330" role="img" aria-label="A phi's operands travel on edges: a join and a loop header" aria-describedby="o3-f1-desc">
<title id="o3-f1-title">A phi's operands travel on edges</title>
<desc id="o3-f1-desc">Left, a join. Block entry computes y1 = x * 2 and tests x > 5. Its true edge goes to block then, which computes y2 = y1 - 3; its false edge goes straight to block join. Join begins with y3 = phi(entry: y1, then: y2) and returns y3. The false edge is labelled carries y1 and the edge from then is labelled carries y2; both are drawn as moving dashes. Right, a loop header. Block entry sets i0 = 0 and goes to block head, which begins with i1 = phi(entry: i0, body: i2) and tests i1 < n. The true edge goes to block body, which computes i2 = i1 + 1 and goes back to head; the false edge goes to exit. The edge from entry is labelled carries i0 and the back edge from body is labelled carries i2; both are drawn as moving dashes. A note says that i2 is defined after the phi but used on the edge from body to head, at the end of body.</desc>
<defs><marker id="o3-f1-head" viewBox="0 0 10 10" refX="10" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker></defs>
<text class="vx-text" x="20" y="24">A join</text>
<rect class="vx-box" x="40" y="44" width="250" height="52" rx="4"/>
<text class="vx-text" x="52" y="66">entry</text>
<text class="vx-mono" x="110" y="66">y1 = x * 2</text>
<text class="vx-mono" x="110" y="86">x &gt; 5 ?</text>
<rect class="vx-box" x="170" y="140" width="190" height="34" rx="4"/>
<text class="vx-text" x="182" y="162">then</text>
<text class="vx-mono" x="228" y="162">y2 = y1 - 3</text>
<rect class="vx-box-accent" x="40" y="222" width="320" height="52" rx="4"/>
<text class="vx-text" x="52" y="244">join</text>
<text class="vx-mono" x="100" y="244">y3 = phi(entry: y1, then: y2)</text>
<text class="vx-mono" x="100" y="264">return y3</text>
<line class="vx-line" x1="250" y1="96" x2="265" y2="140" marker-end="url(#o3-f1-head)"/>
<text class="vx-text-muted" x="266" y="116">true</text>
<line class="vx-flow" x1="80" y1="96" x2="80" y2="222" marker-end="url(#o3-f1-head)"/>
<text class="vx-text-muted" x="86" y="118">false</text>
<text class="vx-text-accent" x="86" y="190">carries y1</text>
<line class="vx-flow" x1="265" y1="174" x2="265" y2="222" marker-end="url(#o3-f1-head)"/>
<text class="vx-text-accent" x="271" y="204">carries y2</text>
<text class="vx-text-muted" x="20" y="306">each edge into join carries one operand</text>
<text class="vx-text" x="400" y="24">A loop header</text>
<rect class="vx-box" x="430" y="44" width="170" height="34" rx="4"/>
<text class="vx-text" x="442" y="66">entry</text>
<text class="vx-mono" x="496" y="66">i0 = 0</text>
<rect class="vx-box-accent" x="430" y="122" width="310" height="52" rx="4"/>
<text class="vx-text" x="442" y="144">head</text>
<text class="vx-mono" x="490" y="144">i1 = phi(entry: i0, body: i2)</text>
<text class="vx-mono" x="490" y="164">i1 &lt; n ?</text>
<rect class="vx-box" x="430" y="222" width="170" height="34" rx="4"/>
<text class="vx-text" x="442" y="244">body</text>
<text class="vx-mono" x="490" y="244">i2 = i1 + 1</text>
<rect class="vx-box" x="650" y="222" width="90" height="34" rx="4"/>
<text class="vx-text" x="662" y="244">exit</text>
<line class="vx-flow" x1="515" y1="78" x2="515" y2="122" marker-end="url(#o3-f1-head)"/>
<text class="vx-text-accent" x="521" y="104">carries i0</text>
<line class="vx-line" x1="470" y1="174" x2="470" y2="222" marker-end="url(#o3-f1-head)"/>
<text class="vx-text-muted" x="476" y="202">true</text>
<line class="vx-flow" x1="580" y1="222" x2="580" y2="174" marker-end="url(#o3-f1-head)"/>
<text class="vx-text-accent" x="586" y="202">carries i2</text>
<line class="vx-line" x1="695" y1="174" x2="695" y2="222" marker-end="url(#o3-f1-head)"/>
<text class="vx-text-muted" x="701" y="202">false</text>
<text class="vx-text-muted" x="400" y="290">i2 is defined after the phi in the text, but it is</text>
<text class="vx-text-muted" x="400" y="306">used on the edge body → head, at the end of body</text>
</svg>
<figcaption>Figure 1. A phi's operands belong to edges. Left: at a join, the edge from <code>entry</code> carries <code>y1</code> and the edge from <code>then</code> carries <code>y2</code>, and the phi at the top of <code>join</code> takes whichever arrived. Right: at a loop header, the back edge carries <code>i2</code>, which the body computes after the phi in the program text; the use counts as happening at the end of <code>body</code>, where <code>i2</code> exists. The moving dashes are the edges that carry phi operands.</figcaption>
</figure>

The phis of a block run together: each reads its operand for the arriving edge before any of them writes its result. The textbook *SSA-based Compiler Design*, the SSA book from here on, describes phis as executed simultaneously, not one after another,[^ssabook] and MLIR's design rationale calls LLVM's phis atomic and warns that the behaviour surprises compiler engineers and invites bugs.[^mlir-rationale] The section on leaving SSA form shows such a bug. It follows that the order in which a block's phis are written down means nothing.

No processor executes a phi, and none needs to. When SSA form is only an intermediate step, the meaning of a phi matters only for judging whether each transformation is correct, and translating out of SSA form replaces it with ordinary copies.[^cytron] MLIR records the same information differently: a branch passes values to the **block arguments** of its target, the way a call passes arguments to a function, and MLIR's rationale describes the two forms as able to represent the same constructs.[^mlir-rationale] [M2](../mlir/m2-reading-mlir.md) reads them.

## How many phis

[O2](o2-cfg-and-dominance.md#the-dominance-frontier) answered where phis go: at the iterated dominance frontier of the blocks that assign a variable, counting the entry block as one of them. Behind that answer is Cytron's condition: whenever two non-empty paths that start at two different assignments to a variable first meet at a block, that block needs a phi for the variable.[^cytron] Placing phis exactly where the condition requires gives **minimal SSA form**, the fewest phis the condition allows.[^cytron]

Minimal is not the same as needed. In the stage 7 loop, `count` gets a phi at F, where the value tested at the header meets the value incremented before a `break`, yet nothing at F reads `count`. **Pruned SSA form** leaves out a phi wherever its variable is not **live**, that is, wherever no path leads from the block to a use of the variable before another assignment to it.[^cytron] [^ssabook]

One way to build pruned form, due to Choi, Cytron and Ferrante, computes liveness first and places a phi only where the variable is live;[^braun] another builds minimal form and then deletes the dead phis.[^ssabook] **Semi-pruned SSA form**, from Briggs and his coauthors, skips the liveness analysis and prunes only the variables that are never live across a block boundary.[^braun] Dead phis are not always waste: Cytron and his coauthors kept them on purpose, and the SSA book shows a dead phi that lets value numbering ([O6](o6-redundancy.md)) prove two values equal.[^cytron] [^ssabook]

A last property matters for correctness. A program is **strict** when, on every path, each variable is assigned before it is used; in SSA form, that is the same as each definition dominating all its uses. Java requires strictness and C does not, and giving every variable a pseudo-assignment of an undefined value in the entry block makes any program strict.[^ssabook]

Vortex programs are strict already. Every local declaration has an initializer, and a name is visible only after its declaration ([Declarations 3.5 and 3.6](../specification/declarations.md#36-scopes)), so no path reads a variable before assigning it.

The undefined value can still appear, but only where no real use sees it. Minimal form counts the entry as an assignment to every variable, so a phi that it places where a variable does not exist yet on some incoming path gets an undefined operand for that path.[^ssabook] Such a phi is always dead: in a strict program, a variable that is live at a block has been assigned on every path into it. Pruned form therefore never needs the undefined value, except in code that no path reaches, such as statements after a `return`, where Braun's construction, described below, gives a read an undefined value.[^braun]

## Renaming with a stack per variable

Placing the phis is the first step of Cytron's construction. It leaves each phi with placeholder operands, `count = phi(count, count, count)`. The second step, **renaming**, gives every assignment, phis included, its own version, and points every use, phi operands included, at the version that reaches it.[^cytron]

Once the phis are in place, exactly one assignment reaches each use, and that assignment dominates the use.[^ssabook] The blocks that dominate a point are those on the dominator tree's path from the entry down to it, so the assignment that reaches a use is the nearest one earlier in the use's own block or, failing that, in the nearest block above it on that path. A walk down the tree can therefore keep, for each variable, a stack of the versions assigned along the current path, newest on top, and read the answer for every use off the top. Cytron's algorithm visits a block this way:[^cytron]

1. Give the result of each phi a new version and push it. Then, for each ordinary statement in order, replace every use of a variable with the version on top of that variable's stack, and give each variable the statement assigns a new version and push it.
2. For each successor in the control-flow graph, fill in the operand that belongs to this edge in each of the successor's phis, with the version on top of the stack.
3. Visit the block's children in the dominator tree.
4. Pop every version the block pushed.

Step 2 uses successors in the control-flow graph, not children in the tree, because a phi operand belongs to an edge, and an edge carries whatever is current at the end of its source. Step 4 keeps the stacks matched to the tree: when the walk leaves C's subtree for F, the version C pushed is gone, as it must be, since C does not dominate F. Figure 2 steps through the stage 7 loop from O2 with its phis already placed: `count` at B and F, `total` at B.

<figure class="vx-figure">
<div class="vx-stepper">
<div class="vx-step">
<p><strong>Step 1. Visit A.</strong> Both assignments get version 0 and push it. A's only successor is B, so B's two phis get their operands for the edge from A: <code>count0</code> and <code>total0</code>.</p>
<svg viewBox="0 0 760 250" role="img" aria-label="Renaming step 1: visiting block A of the dominator tree. Renamed code: count0 = 0; total0 = 0. edge A → B: B&#x27;s phis get count0, total0. Stacks: count holds count0; total holds total0.">
<text class="vx-text-muted" x="20" y="18">dominator tree</text>
<text class="vx-text-muted" x="200" y="18">the visited block, renamed</text>
<text class="vx-text-muted" x="560" y="18">stacks of versions</text>
<line class="vx-line" x1="100" y1="57" x2="100" y2="75"/>
<line class="vx-line" x1="100" y1="101" x2="60" y2="119"/>
<line class="vx-line" x1="100" y1="101" x2="140" y2="119"/>
<line class="vx-line" x1="60" y1="145" x2="60" y2="163"/>
<line class="vx-line" x1="60" y1="189" x2="60" y2="207"/>
<rect class="vx-box-accent" x="80" y="31" width="40" height="26" rx="4"/>
<text class="vx-text" x="100" y="49" text-anchor="middle">A</text>
<rect class="vx-box" x="80" y="75" width="40" height="26" rx="4"/>
<text class="vx-text" x="100" y="93" text-anchor="middle">B</text>
<rect class="vx-box" x="40" y="119" width="40" height="26" rx="4"/>
<text class="vx-text" x="60" y="137" text-anchor="middle">C</text>
<rect class="vx-box" x="120" y="119" width="40" height="26" rx="4"/>
<text class="vx-text" x="140" y="137" text-anchor="middle">F</text>
<rect class="vx-box" x="40" y="163" width="40" height="26" rx="4"/>
<text class="vx-text" x="60" y="181" text-anchor="middle">D</text>
<rect class="vx-box" x="40" y="207" width="40" height="26" rx="4"/>
<text class="vx-text" x="60" y="225" text-anchor="middle">E</text>
<text class="vx-text" x="200" y="48">Visit A</text>
<text class="vx-mono" x="200" y="74">count0 = 0</text>
<text class="vx-mono" x="200" y="96">total0 = 0</text>
<text class="vx-text-accent" x="200" y="126">edge A → B: B&#x27;s phis get count0, total0</text>
<rect class="vx-box-accent" x="560" y="196" width="90" height="24" rx="3"/>
<text class="vx-mono" x="605" y="213" text-anchor="middle">count0</text>
<text class="vx-text-muted" x="605" y="240" text-anchor="middle">count</text>
<rect class="vx-box-accent" x="660" y="196" width="90" height="24" rx="3"/>
<text class="vx-mono" x="705" y="213" text-anchor="middle">total0</text>
<text class="vx-text-muted" x="705" y="240" text-anchor="middle">total</text>
</svg>
</div>
<div class="vx-step">
<p><strong>Step 2. Visit B.</strong> B's phis are its first assignments, so they push <code>count1</code> and <code>total1</code>, and the test reads <code>count1</code>. Of B's successors only F has a phi; its operand for the edge from B is <code>count1</code>. The operands for the edges from C and E stay empty until the walk gets there.</p>
<svg viewBox="0 0 760 250" role="img" aria-label="Renaming step 2: visiting block B of the dominator tree. Renamed code: count1 = phi(A: count0, C: ?, E: ?); total1 = phi(A: total0, C: ?, E: ?); if count1 &lt; 10. edge B → F: F&#x27;s phi gets count1; edge B → C: C has no phis. Stacks: count holds count0, count1; total holds total0, total1.">
<text class="vx-text-muted" x="20" y="18">dominator tree</text>
<text class="vx-text-muted" x="200" y="18">the visited block, renamed</text>
<text class="vx-text-muted" x="560" y="18">stacks of versions</text>
<line class="vx-line" x1="100" y1="57" x2="100" y2="75"/>
<line class="vx-line" x1="100" y1="101" x2="60" y2="119"/>
<line class="vx-line" x1="100" y1="101" x2="140" y2="119"/>
<line class="vx-line" x1="60" y1="145" x2="60" y2="163"/>
<line class="vx-line" x1="60" y1="189" x2="60" y2="207"/>
<rect class="vx-box-strong" x="80" y="31" width="40" height="26" rx="4"/>
<text class="vx-text" x="100" y="49" text-anchor="middle">A</text>
<rect class="vx-box-accent" x="80" y="75" width="40" height="26" rx="4"/>
<text class="vx-text" x="100" y="93" text-anchor="middle">B</text>
<rect class="vx-box" x="40" y="119" width="40" height="26" rx="4"/>
<text class="vx-text" x="60" y="137" text-anchor="middle">C</text>
<rect class="vx-box" x="120" y="119" width="40" height="26" rx="4"/>
<text class="vx-text" x="140" y="137" text-anchor="middle">F</text>
<rect class="vx-box" x="40" y="163" width="40" height="26" rx="4"/>
<text class="vx-text" x="60" y="181" text-anchor="middle">D</text>
<rect class="vx-box" x="40" y="207" width="40" height="26" rx="4"/>
<text class="vx-text" x="60" y="225" text-anchor="middle">E</text>
<text class="vx-text" x="200" y="48">Visit B</text>
<text class="vx-mono" x="200" y="74">count1 = phi(A: count0, C: ?, E: ?)</text>
<text class="vx-mono" x="200" y="96">total1 = phi(A: total0, C: ?, E: ?)</text>
<text class="vx-mono" x="200" y="118">if count1 &lt; 10</text>
<text class="vx-text-accent" x="200" y="148">edge B → F: F&#x27;s phi gets count1</text>
<text class="vx-text-accent" x="200" y="170">edge B → C: C has no phis</text>
<rect class="vx-box" x="560" y="196" width="90" height="24" rx="3"/>
<text class="vx-mono" x="605" y="213" text-anchor="middle">count0</text>
<rect class="vx-box-accent" x="560" y="170" width="90" height="24" rx="3"/>
<text class="vx-mono" x="605" y="187" text-anchor="middle">count1</text>
<text class="vx-text-muted" x="605" y="240" text-anchor="middle">count</text>
<rect class="vx-box" x="660" y="196" width="90" height="24" rx="3"/>
<text class="vx-mono" x="705" y="213" text-anchor="middle">total0</text>
<rect class="vx-box-accent" x="660" y="170" width="90" height="24" rx="3"/>
<text class="vx-mono" x="705" y="187" text-anchor="middle">total1</text>
<text class="vx-text-muted" x="705" y="240" text-anchor="middle">total</text>
</svg>
</div>
<div class="vx-step">
<p><strong>Step 3. Visit C.</strong> The use of <code>count</code> reads the top, <code>count1</code>, and then the assignment pushes <code>count2</code>. C's <code>continue</code> edge goes back to B, whose phis get <code>count2</code> and, since C did not assign <code>total</code>, <code>total1</code>.</p>
<svg viewBox="0 0 760 250" role="img" aria-label="Renaming step 3: visiting block C of the dominator tree. Renamed code: count2 = count1 + 1; if count2 % 2 == 0. edge C → B: B&#x27;s phis get count2, total1. Stacks: count holds count0, count1, count2; total holds total0, total1.">
<text class="vx-text-muted" x="20" y="18">dominator tree</text>
<text class="vx-text-muted" x="200" y="18">the visited block, renamed</text>
<text class="vx-text-muted" x="560" y="18">stacks of versions</text>
<line class="vx-line" x1="100" y1="57" x2="100" y2="75"/>
<line class="vx-line" x1="100" y1="101" x2="60" y2="119"/>
<line class="vx-line" x1="100" y1="101" x2="140" y2="119"/>
<line class="vx-line" x1="60" y1="145" x2="60" y2="163"/>
<line class="vx-line" x1="60" y1="189" x2="60" y2="207"/>
<rect class="vx-box-strong" x="80" y="31" width="40" height="26" rx="4"/>
<text class="vx-text" x="100" y="49" text-anchor="middle">A</text>
<rect class="vx-box-strong" x="80" y="75" width="40" height="26" rx="4"/>
<text class="vx-text" x="100" y="93" text-anchor="middle">B</text>
<rect class="vx-box-accent" x="40" y="119" width="40" height="26" rx="4"/>
<text class="vx-text" x="60" y="137" text-anchor="middle">C</text>
<rect class="vx-box" x="120" y="119" width="40" height="26" rx="4"/>
<text class="vx-text" x="140" y="137" text-anchor="middle">F</text>
<rect class="vx-box" x="40" y="163" width="40" height="26" rx="4"/>
<text class="vx-text" x="60" y="181" text-anchor="middle">D</text>
<rect class="vx-box" x="40" y="207" width="40" height="26" rx="4"/>
<text class="vx-text" x="60" y="225" text-anchor="middle">E</text>
<text class="vx-text" x="200" y="48">Visit C</text>
<text class="vx-mono" x="200" y="74">count2 = count1 + 1</text>
<text class="vx-mono" x="200" y="96">if count2 % 2 == 0</text>
<text class="vx-text-accent" x="200" y="126">edge C → B: B&#x27;s phis get count2, total1</text>
<rect class="vx-box" x="560" y="196" width="90" height="24" rx="3"/>
<text class="vx-mono" x="605" y="213" text-anchor="middle">count0</text>
<rect class="vx-box" x="560" y="170" width="90" height="24" rx="3"/>
<text class="vx-mono" x="605" y="187" text-anchor="middle">count1</text>
<rect class="vx-box-accent" x="560" y="144" width="90" height="24" rx="3"/>
<text class="vx-mono" x="605" y="161" text-anchor="middle">count2</text>
<text class="vx-text-muted" x="605" y="240" text-anchor="middle">count</text>
<rect class="vx-box" x="660" y="196" width="90" height="24" rx="3"/>
<text class="vx-mono" x="705" y="213" text-anchor="middle">total0</text>
<rect class="vx-box-accent" x="660" y="170" width="90" height="24" rx="3"/>
<text class="vx-mono" x="705" y="187" text-anchor="middle">total1</text>
<text class="vx-text-muted" x="705" y="240" text-anchor="middle">total</text>
</svg>
</div>
<div class="vx-step">
<p><strong>Step 4. Visit D.</strong> D assigns nothing. Its <code>break</code> edge fills the second operand of F's phi with <code>count2</code>, the count incremented before the loop ended.</p>
<svg viewBox="0 0 760 250" role="img" aria-label="Renaming step 4: visiting block D of the dominator tree. Renamed code: if total1 &gt; 10. edge D → F: F&#x27;s phi gets count2. Stacks: count holds count0, count1, count2; total holds total0, total1.">
<text class="vx-text-muted" x="20" y="18">dominator tree</text>
<text class="vx-text-muted" x="200" y="18">the visited block, renamed</text>
<text class="vx-text-muted" x="560" y="18">stacks of versions</text>
<line class="vx-line" x1="100" y1="57" x2="100" y2="75"/>
<line class="vx-line" x1="100" y1="101" x2="60" y2="119"/>
<line class="vx-line" x1="100" y1="101" x2="140" y2="119"/>
<line class="vx-line" x1="60" y1="145" x2="60" y2="163"/>
<line class="vx-line" x1="60" y1="189" x2="60" y2="207"/>
<rect class="vx-box-strong" x="80" y="31" width="40" height="26" rx="4"/>
<text class="vx-text" x="100" y="49" text-anchor="middle">A</text>
<rect class="vx-box-strong" x="80" y="75" width="40" height="26" rx="4"/>
<text class="vx-text" x="100" y="93" text-anchor="middle">B</text>
<rect class="vx-box-strong" x="40" y="119" width="40" height="26" rx="4"/>
<text class="vx-text" x="60" y="137" text-anchor="middle">C</text>
<rect class="vx-box" x="120" y="119" width="40" height="26" rx="4"/>
<text class="vx-text" x="140" y="137" text-anchor="middle">F</text>
<rect class="vx-box-accent" x="40" y="163" width="40" height="26" rx="4"/>
<text class="vx-text" x="60" y="181" text-anchor="middle">D</text>
<rect class="vx-box" x="40" y="207" width="40" height="26" rx="4"/>
<text class="vx-text" x="60" y="225" text-anchor="middle">E</text>
<text class="vx-text" x="200" y="48">Visit D</text>
<text class="vx-mono" x="200" y="74">if total1 &gt; 10</text>
<text class="vx-text-accent" x="200" y="104">edge D → F: F&#x27;s phi gets count2</text>
<rect class="vx-box" x="560" y="196" width="90" height="24" rx="3"/>
<text class="vx-mono" x="605" y="213" text-anchor="middle">count0</text>
<rect class="vx-box" x="560" y="170" width="90" height="24" rx="3"/>
<text class="vx-mono" x="605" y="187" text-anchor="middle">count1</text>
<rect class="vx-box-accent" x="560" y="144" width="90" height="24" rx="3"/>
<text class="vx-mono" x="605" y="161" text-anchor="middle">count2</text>
<text class="vx-text-muted" x="605" y="240" text-anchor="middle">count</text>
<rect class="vx-box" x="660" y="196" width="90" height="24" rx="3"/>
<text class="vx-mono" x="705" y="213" text-anchor="middle">total0</text>
<rect class="vx-box-accent" x="660" y="170" width="90" height="24" rx="3"/>
<text class="vx-mono" x="705" y="187" text-anchor="middle">total1</text>
<text class="vx-text-muted" x="705" y="240" text-anchor="middle">total</text>
</svg>
</div>
<div class="vx-step">
<p><strong>Step 5. Visit E.</strong> <code>total2</code> is pushed, and the edge back to B fills B's last operands. E has no children, so the walk turns back: leaving E pops <code>total2</code>, D pushed nothing, and leaving C pops <code>count2</code>.</p>
<svg viewBox="0 0 760 250" role="img" aria-label="Renaming step 5: visiting block E of the dominator tree. Renamed code: total2 = total1 + count2. edge E → B: B&#x27;s phis get count2, total2. Stacks: count holds count0, count1, count2; total holds total0, total1, total2.">
<text class="vx-text-muted" x="20" y="18">dominator tree</text>
<text class="vx-text-muted" x="200" y="18">the visited block, renamed</text>
<text class="vx-text-muted" x="560" y="18">stacks of versions</text>
<line class="vx-line" x1="100" y1="57" x2="100" y2="75"/>
<line class="vx-line" x1="100" y1="101" x2="60" y2="119"/>
<line class="vx-line" x1="100" y1="101" x2="140" y2="119"/>
<line class="vx-line" x1="60" y1="145" x2="60" y2="163"/>
<line class="vx-line" x1="60" y1="189" x2="60" y2="207"/>
<rect class="vx-box-strong" x="80" y="31" width="40" height="26" rx="4"/>
<text class="vx-text" x="100" y="49" text-anchor="middle">A</text>
<rect class="vx-box-strong" x="80" y="75" width="40" height="26" rx="4"/>
<text class="vx-text" x="100" y="93" text-anchor="middle">B</text>
<rect class="vx-box-strong" x="40" y="119" width="40" height="26" rx="4"/>
<text class="vx-text" x="60" y="137" text-anchor="middle">C</text>
<rect class="vx-box" x="120" y="119" width="40" height="26" rx="4"/>
<text class="vx-text" x="140" y="137" text-anchor="middle">F</text>
<rect class="vx-box-strong" x="40" y="163" width="40" height="26" rx="4"/>
<text class="vx-text" x="60" y="181" text-anchor="middle">D</text>
<rect class="vx-box-accent" x="40" y="207" width="40" height="26" rx="4"/>
<text class="vx-text" x="60" y="225" text-anchor="middle">E</text>
<text class="vx-text" x="200" y="48">Visit E</text>
<text class="vx-mono" x="200" y="74">total2 = total1 + count2</text>
<text class="vx-text-accent" x="200" y="104">edge E → B: B&#x27;s phis get count2, total2</text>
<text class="vx-text-muted" x="200" y="126">then leaving E pops total2,</text>
<text class="vx-text-muted" x="200" y="144">and leaving C pops count2</text>
<rect class="vx-box" x="560" y="196" width="90" height="24" rx="3"/>
<text class="vx-mono" x="605" y="213" text-anchor="middle">count0</text>
<rect class="vx-box" x="560" y="170" width="90" height="24" rx="3"/>
<text class="vx-mono" x="605" y="187" text-anchor="middle">count1</text>
<rect class="vx-box-accent" x="560" y="144" width="90" height="24" rx="3"/>
<text class="vx-mono" x="605" y="161" text-anchor="middle">count2</text>
<text class="vx-text-muted" x="605" y="240" text-anchor="middle">count</text>
<rect class="vx-box" x="660" y="196" width="90" height="24" rx="3"/>
<text class="vx-mono" x="705" y="213" text-anchor="middle">total0</text>
<rect class="vx-box" x="660" y="170" width="90" height="24" rx="3"/>
<text class="vx-mono" x="705" y="187" text-anchor="middle">total1</text>
<rect class="vx-box-accent" x="660" y="144" width="90" height="24" rx="3"/>
<text class="vx-mono" x="705" y="161" text-anchor="middle">total2</text>
<text class="vx-text-muted" x="705" y="240" text-anchor="middle">total</text>
</svg>
</div>
<div class="vx-step">
<p><strong>Step 6. Visit F.</strong> F is B's second child. C's subtree is finished, so the stacks hold only what A and B pushed; F's phi pushes <code>count3</code> on top of <code>count1</code>, and <code>print</code> reads <code>total1</code>, the total the loop header last saw. F has no successors, and the walk ends by popping everything.</p>
<svg viewBox="0 0 760 250" role="img" aria-label="Renaming step 6: visiting block F of the dominator tree. Renamed code: count3 = phi(B: count1, D: count2); print total1. F has no successors. Stacks: count holds count0, count1, count3; total holds total0, total1.">
<text class="vx-text-muted" x="20" y="18">dominator tree</text>
<text class="vx-text-muted" x="200" y="18">the visited block, renamed</text>
<text class="vx-text-muted" x="560" y="18">stacks of versions</text>
<line class="vx-line" x1="100" y1="57" x2="100" y2="75"/>
<line class="vx-line" x1="100" y1="101" x2="60" y2="119"/>
<line class="vx-line" x1="100" y1="101" x2="140" y2="119"/>
<line class="vx-line" x1="60" y1="145" x2="60" y2="163"/>
<line class="vx-line" x1="60" y1="189" x2="60" y2="207"/>
<rect class="vx-box-strong" x="80" y="31" width="40" height="26" rx="4"/>
<text class="vx-text" x="100" y="49" text-anchor="middle">A</text>
<rect class="vx-box-strong" x="80" y="75" width="40" height="26" rx="4"/>
<text class="vx-text" x="100" y="93" text-anchor="middle">B</text>
<rect class="vx-box-strong" x="40" y="119" width="40" height="26" rx="4"/>
<text class="vx-text" x="60" y="137" text-anchor="middle">C</text>
<rect class="vx-box-accent" x="120" y="119" width="40" height="26" rx="4"/>
<text class="vx-text" x="140" y="137" text-anchor="middle">F</text>
<rect class="vx-box-strong" x="40" y="163" width="40" height="26" rx="4"/>
<text class="vx-text" x="60" y="181" text-anchor="middle">D</text>
<rect class="vx-box-strong" x="40" y="207" width="40" height="26" rx="4"/>
<text class="vx-text" x="60" y="225" text-anchor="middle">E</text>
<text class="vx-text" x="200" y="48">Visit F</text>
<text class="vx-mono" x="200" y="74">count3 = phi(B: count1, D: count2)</text>
<text class="vx-mono" x="200" y="96">print total1</text>
<text class="vx-text-accent" x="200" y="126">F has no successors</text>
<text class="vx-text-muted" x="200" y="148">then leaving F, B and A</text>
<text class="vx-text-muted" x="200" y="166">empties both stacks</text>
<rect class="vx-box" x="560" y="196" width="90" height="24" rx="3"/>
<text class="vx-mono" x="605" y="213" text-anchor="middle">count0</text>
<rect class="vx-box" x="560" y="170" width="90" height="24" rx="3"/>
<text class="vx-mono" x="605" y="187" text-anchor="middle">count1</text>
<rect class="vx-box-accent" x="560" y="144" width="90" height="24" rx="3"/>
<text class="vx-mono" x="605" y="161" text-anchor="middle">count3</text>
<text class="vx-text-muted" x="605" y="240" text-anchor="middle">count</text>
<rect class="vx-box" x="660" y="196" width="90" height="24" rx="3"/>
<text class="vx-mono" x="705" y="213" text-anchor="middle">total0</text>
<rect class="vx-box-accent" x="660" y="170" width="90" height="24" rx="3"/>
<text class="vx-mono" x="705" y="187" text-anchor="middle">total1</text>
<text class="vx-text-muted" x="705" y="240" text-anchor="middle">total</text>
</svg>
</div>
</div>
<figcaption>Figure 2. Renaming the stage 7 loop, one block at a time, in the order of a depth-first walk of the dominator tree. Left: the tree, with the block being visited in the accent outline and the blocks already visited in a heavier outline. Middle: the block after renaming, and the phi operands it fills in its successors. Right: the stack of versions for each variable at the moment those operands are filled, with the top of each stack in the accent outline; the top is always the nearest assignment that dominates the current point.</figcaption>
</figure>

The first example runs the same walk, printing the stacks at each visit and then the renamed function:

--8<-- "includes/examples/optimize/o3-ssa/rename.cpp.md"

??? check "Why does the phi for `total` at B take `total1`, its own result, along the edge from C?"

    C does not assign `total`, so at the end of C the top of `total`'s stack is still `total1`, pushed by B's own phi. The edge C → B is the `continue`, which changes `count` but not `total`, and the operand records that the value going around the loop on that edge is the one that entered B.

Renaming touches each mention of a variable once, and each edge once per phi in its target. The frontiers that placement needs are usually small as well: Cytron and his coauthors gave analytical and measured evidence that the dominance frontiers of a whole program usually add up to a size linear in the program.[^cytron]

## Building SSA while lowering

Cytron's method needs a finished control-flow graph in non-SSA form, then a dominator tree, frontiers and, for pruned form, liveness, before it places the first phi. A front end that wants to produce SSA form directly must first build that non-SSA graph.[^braun] Braun and his coauthors turned the method around. Instead of pushing each assignment forward to its uses, they look backwards from each use, and only when a use asks.[^braun]

Their method starts from **local value numbering**: while filling a block, the front end keeps a map from each variable to its current value in that block, updated at each assignment and consulted at each read. A read that finds nothing in its own block asks the block's predecessors. With one predecessor, the answer is whatever that predecessor answers; with several, the answer is a new phi whose operands are the predecessors' answers. Around a loop, the question comes back to the block that asked it, so the phi is created, and recorded as that block's value, before its operands are requested; the question arriving a second time finds the phi and stops.[^braun]

Some of these phis turn out to be unnecessary. A phi whose operands are only itself and one other value, in any number, is **trivial**: the other value replaces it everywhere, and the phis that used it are checked again, since they may have become trivial too.[^braun]

A front end creates a loop header before it lowers the loop's body, so while it fills the body, the header's second predecessor, the edge back from the end of the body, does not exist yet. Braun and his coauthors call a block **sealed** once no more predecessors will be added to it. A read in an unsealed block creates a phi with no operands and remembers it; sealing the block fills in the operands of every phi remembered there.[^braun]

For a `while` loop, the first block of the body can be sealed as soon as it is created, the header after the back edge is added, and the block after the loop only when the whole loop is finished, because a `break` inside the body can still add a predecessor.[^braun] The second example builds a toy loop in that order:

--8<-- "includes/examples/optimize/o3-ssa/on_the_fly.cpp.md"

Reading `x < y` in the unsealed header creates two phis with no operands. Sealing the header completes them. The phi for `x` joins `v0`, from before the loop, with `v6`, the body's `x + 1`, and stays. The phi for `y` joins `v1` with itself, since the body never assigns `y`, so it is trivial, and `v1` replaces it everywhere, including in the comparison.

The method produces pruned SSA form for every program, because a phi exists only when a read asks for one, and minimal SSA form for every program whose control-flow graph is reducible. For irreducible graphs, a later pass removes groups of phis that only pass one value around a cycle among themselves.[^braun] The method can also simplify as it builds, folding constants, reusing values that local value numbering has already seen, and propagating copies.[^braun] The same backward lookup also repairs SSA form when a transformation adds a second definition of a value, as jump threading does when it sends a path straight past a branch whose outcome it knows, and it needs no dominator tree to do so.[^braun]

Braun and his coauthors implemented the method in Clang, on LLVM 3.1, and compared it with LLVM's usual route, which puts local variables in memory and then runs LLVM's implementation of Cytron's algorithm, on the C programs of SPEC CINT2000. Before SSA construction, a quarter of all instructions were the loads, stores and stack allocations that stood in for variables, and construction removed them. To compare the two constructions' speed, they counted the x86 instructions the compiler itself executed, on a Core i7-2600: with a variant of their method, the marker algorithm, it executed 0.28% fewer than with LLVM's tuned implementation, so the simpler method was at least as fast.[^braun]

For Vortex the method fits well. [O2](o2-cfg-and-dominance.md#reducible-and-irreducible-graphs) argued that v0.1's statements produce only reducible graphs, so the result is minimal without the extra pass. And lowering always knows when a block is complete: the join after an `if` once both arms are lowered, a loop header once every edge back to it exists (for a `while`, that includes each `continue`), and the block after a loop once the loop is done, `break`s included.

## Stack slots and mem2reg

There is a third way, the one [stage 6](../compiler/guide/stage-6-first-machine-code.md#ssa-if-you-use-llvm) suggested for a front end that emits LLVM IR. LLVM requires every register value to be in SSA form, but it does not put memory in SSA form. So a front end can give each mutable variable a **stack slot**, an `alloca` instruction in the entry block, read the variable with a load and write it with a store, and never create a phi itself. LLVM's mem2reg pass then promotes the slots to SSA values, placing phis where they are needed.[^kal7]

Promotion has conditions. mem2reg looks only at allocas in the entry block, which runs exactly once. It promotes only slots that are used by direct loads and stores, so a slot whose address is passed to a function, or used in pointer arithmetic, stays in memory. And it handles only single values, such as scalars and pointers, not structs or arrays; the SROA pass (scalar replacement of aggregates) can often split those into pieces that promote.[^kal7] LLVM's advice to front-end authors adds a detail: put the allocas at the start of the entry block, before any call, because inlining a call can split the block and leave later allocas outside it, where neither SROA nor mem2reg looks.[^perftips]

Inside, mem2reg is Cytron's method. It places phis with iterated dominance frontiers, skips blocks where the slot's value is not live, and then walks the function, rewriting loads and stores; LLVM's pass documentation calls the result pruned SSA form.[^llvm-promote] [^llvm-passes] It also has fast paths for slots stored only once and for slots used in a single block.[^llvm-promote] [^kal7] The Kaleidoscope tutorial recommends the technique strongly: Clang uses it for local mutable variables, and it suits LLVM's debug information, which relies on each variable's address being exposed.[^kal7] The third example shows a promotion and a refusal:

--8<-- "includes/examples/optimize/o3-ssa/promote.ll.md"

The four loads of `%i` and `%total` were replaced by the phis `%i.0` and `%total.0` at `%test`, and the stores to those slots disappeared. `%seen` kept its alloca, its store and its load, because `@update` receives its address.

The conditions matter for Vortex in three places:

- A `let mut` local passed as `&mut` to a function gives the callee its address. Unless the call is inlined first ([O7](o7-inlining-and-sroa.md)), that variable stays in memory, and every read of it after the call is a load.
- A local fixed-shape array becomes an alloca of array type, which mem2reg never promotes.[^kal7] SROA can often split such an array into separate scalar slots; [O7](o7-inlining-and-sroa.md) looks at when it can.
- The kernel's arrays live in its caller's memory. The references `a`, `b` and `c` are SSA values, as addresses, but no element of the arrays is. The values that do enter SSA form are `sum` and the three loop counters, which is what the loop analyses of [O8](o8-loops.md) need.

The same machinery serves memory that is not a local variable. When a loop keeps loading and storing one location that nothing else in the loop can touch, LLVM's LICM pass (loop-invariant code motion) moves the store after the loop and uses mem2reg to hold the value in a register meanwhile.[^llvm-passes] A Vortex loop that accumulates straight into `c[row, column]`, instead of into a local `sum`, depends on that promotion, and the proof that no other load or store in the loop touches the location comes from the `&mut` rules of decisions [25](../decisions/references.md#d25) and [41](../decisions/references.md#d41), through the alias analysis of [O9](o9-alias-analysis.md).

## Leaving SSA form

Eventually the program runs, and processors have no phis. Cytron and his coauthors translate out of SSA form by replacing each phi with ordinary copies, one at the end of each predecessor. For `y3 = phi(entry: y1, then: y2)`, the copy `y3 = y1` goes at the end of `entry` and `y3 = y2` at the end of `then`.[^cytron] This is **SSA destruction**. It leaves many copies behind, and a later step removes most of them: **coalescing** gives a copy's source and destination the same register when their values are never needed at the same time, which turns the copy into a copy of a register to itself, to be deleted.[^cytron] [^ssabook]

For SSA form fresh from construction, this naive replacement is correct. Call a phi's result, its operands, and everything connected to those through other phis a **phi-web**. After construction, the versions in one phi-web come from one source variable and are never live at the same time; SSA form with that property is **conventional**, and destroying it amounts to renaming each web back to one variable and deleting the phis.[^ssabook]

Optimizations do not preserve the property. Copy propagation, which replaces the uses of a copy with its source, can stretch one version's lifetime over another's, and the SSA form is then **transformed**.[^ssabook] Briggs, Cooper, Harvey and Simpson showed in 1998 that naive destruction of transformed SSA form can produce wrong code, and illustrated it with two failures now known as the **lost-copy problem** and the **swap problem**.[^ssabook]

### The lost copy

Take a loop that remembers the value it had before its last step. After copy propagation has removed the variable that did the remembering, the loop's SSA form reads:

```text
entry:  x0 = 1
        goto loop
loop:   x1 = phi(entry: x0, loop: x2)
        x2 = x1 + 1
        if x2 < 4 goto loop else done
done:   print(x1)
```

The loop runs with `x1` equal to 1, 2 and 3; on the third pass `x2` reaches 4, so the program prints 3. The edge from `loop` back to itself is critical: its source has two successors and its target has two predecessors. The naive copy for that edge, `x1 = x2`, has to go at the end of `loop`, before the branch, so it also runs on the way to `done`, and `done` prints 4 (Figure 3). A copy meant for one edge was placed where two edges share it, and the value of `x1` that `done` needed is lost.

<figure class="vx-figure">
<svg viewBox="0 0 760 330" role="img" aria-label="The lost-copy problem: a copy at the end of the loop block also runs on the exit edge, while a copy on the split back edge does not" aria-describedby="o3-f3-desc">
<title id="o3-f3-title">The lost-copy problem and the split edge</title>
<desc id="o3-f3-desc">Two lowerings of the same loop. Left, copies at the end of each predecessor. Block entry holds x0 = 1 and the copy x1 = x0. Block loop holds x2 = x1 + 1, then the copy x1 = x2, outlined with dashes and marked as running on both edges, then the test x2 < 4. An edge from the right side of loop goes back to loop while x2 is below 4, and an edge down to block done, drawn as moving dashes, leaves the loop. Done prints x1, which is 4: the value of x1 was lost. Right, the back edge split. Entry is the same. Loop holds x2 = x1 + 1 and the test. Its edge for x2 below 4 goes to a new block on the right that holds the copy x1 = x2 and returns to loop. The exit edge goes down to done, which prints 3.</desc>
<defs><marker id="o3-f3-head" viewBox="0 0 10 10" refX="10" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker></defs>
<text class="vx-text" x="20" y="24">Copies at the end of each predecessor</text>
<rect class="vx-box" x="40" y="44" width="220" height="52" rx="4"/>
<text class="vx-text" x="52" y="66">entry</text>
<text class="vx-mono" x="100" y="66">x0 = 1</text>
<text class="vx-mono" x="100" y="86">x1 = x0</text>
<rect class="vx-box" x="40" y="136" width="220" height="74" rx="4"/>
<text class="vx-text" x="52" y="158">loop</text>
<text class="vx-mono" x="100" y="158">x2 = x1 + 1</text>
<rect class="vx-box-bad" x="94" y="166" width="70" height="20" rx="3"/>
<text class="vx-mono" x="100" y="181">x1 = x2</text>
<text class="vx-text-muted" x="170" y="181">on both edges</text>
<text class="vx-mono" x="100" y="202">x2 &lt; 4 ?</text>
<rect class="vx-box" x="40" y="250" width="220" height="34" rx="4"/>
<text class="vx-text" x="52" y="272">done</text>
<text class="vx-mono" x="100" y="272">print(x1)</text>
<text class="vx-text-accent" x="40" y="312">prints 4: the value of x1 was lost</text>
<line class="vx-line" x1="150" y1="96" x2="150" y2="136" marker-end="url(#o3-f3-head)"/>
<path class="vx-line" d="M260 196 L296 196 L296 150 L260 150" marker-end="url(#o3-f3-head)"/>
<text class="vx-text-muted" x="302" y="178">x2 &lt; 4</text>
<line class="vx-flow" x1="150" y1="210" x2="150" y2="250" marker-end="url(#o3-f3-head)"/>
<text class="vx-text-muted" x="156" y="236">exit</text>
<text class="vx-text" x="410" y="24">Copy in a new block on the split edge</text>
<rect class="vx-box" x="410" y="44" width="180" height="52" rx="4"/>
<text class="vx-text" x="422" y="66">entry</text>
<text class="vx-mono" x="470" y="66">x0 = 1</text>
<text class="vx-mono" x="470" y="86">x1 = x0</text>
<rect class="vx-box" x="410" y="136" width="180" height="52" rx="4"/>
<text class="vx-text" x="422" y="158">loop</text>
<text class="vx-mono" x="470" y="158">x2 = x1 + 1</text>
<text class="vx-mono" x="470" y="178">x2 &lt; 4 ?</text>
<rect class="vx-box-accent" x="650" y="140" width="90" height="44" rx="4"/>
<text class="vx-mono" x="662" y="167">x1 = x2</text>
<text class="vx-text-muted" x="650" y="206">new block</text>
<line class="vx-line" x1="590" y1="150" x2="650" y2="150" marker-end="url(#o3-f3-head)"/>
<text class="vx-text-muted" x="596" y="143">x2 &lt; 4</text>
<line class="vx-line" x1="650" y1="174" x2="590" y2="174" marker-end="url(#o3-f3-head)"/>
<rect class="vx-box" x="410" y="250" width="180" height="34" rx="4"/>
<text class="vx-text" x="422" y="272">done</text>
<text class="vx-mono" x="470" y="272">print(x1)</text>
<text class="vx-text-accent" x="410" y="312">prints 3</text>
<line class="vx-line" x1="500" y1="96" x2="500" y2="136" marker-end="url(#o3-f3-head)"/>
<line class="vx-line" x1="500" y1="188" x2="500" y2="250" marker-end="url(#o3-f3-head)"/>
<text class="vx-text-muted" x="506" y="224">exit</text>
</svg>
<figcaption>Figure 3. The lost-copy problem. Left: the naive copies. <code>x1 = x2</code> sits at the end of <code>loop</code>, before the branch, so it also runs on the exit edge (moving dashes), and <code>done</code> prints 4. Right: the critical back edge is split, the copy moves into the new block on it, and <code>done</code> prints 3.</figcaption>
</figure>

There are two fixes. The first splits the critical edge, as [O2](o2-cfg-and-dominance.md#critical-edges) anticipated: a new block on the edge from `loop` to itself holds the copy, and the path to `done` never runs it. The SSA book presents this as the simplest method, though not the most efficient, and notes a drawback: a compiler cannot always split an edge, for instance around exception-handling code.[^ssabook]

The second fix **isolates** the phi. The copies in the predecessors write a fresh variable, and one more copy at the top of the phi's block moves that variable into the phi's result, so no predecessor ever writes the result itself. Sreedhar and his coauthors built the first destruction that was both simple and correct on this idea, and it needs no edge splitting.[^ssabook]

LLVM's PHIElimination pass, which the code generator runs as part of register allocation,[^llvm-codegen] isolates each phi this way through a new virtual register, a temporary that register allocation later maps to a real register or a stack slot. It splits a critical edge only where that helps the coalescer, and by default never splits a loop's back edge, which would put a small block of copies out of line inside the loop.[^llvm-phielim] The fourth example runs all three lowerings on a tiny machine:

--8<-- "includes/examples/optimize/o3-ssa/lost_copy.cpp.md"

??? check "In the stage 7 loop, the `continue` edge C → B is critical, and naive destruction puts `count1 = count2` at the end of C, where it also runs on the way to D. Why is no value lost?"

    Nothing on the way through D reads `count1` again: D and F read `total1`, and E reads `total1` and `count2`. The copy overwrites a value that is already dead on that path. This SSA form comes straight from construction, so it is conventional: `count1` and `count2`, one phi-web, are never live at the same time. In this section's example, copy propagation had made `x1` live after `x2` was defined, and that is the value the early copy destroyed.

    Destruction cannot count on conventional form once optimizations have run, so it must handle every critical edge. Loops tested at the bottom, like this section's example, are common too: LLVM's LoopRotate pass turns a loop into a do-while loop, placed behind a guard when the body might not run at all.[^llvm-loops] [O8](o8-loops.md) returns to rotation.

### The swap

The second problem needs no critical edge. This Vortex program swaps two variables three times:

```vortex
// program: valid
fn main() {
    let mut a = 1;
    let mut b = 2;
    let mut n = 3;
    while n > 0 {
        let t = a;
        a = b;
        b = t;
        n -= 1;
    }
    print(a);
    print(b);
}
```

It prints 2, then 1. In SSA form, once copy propagation has replaced `t` and the new versions of `a` and `b` with the values they copy, the loop header holds two phis that read each other (the overflow check on `n -= 1` is left out):

```text
entry:  a0 = 1
        b0 = 2
        n0 = 3
        goto head
head:   a1 = phi(entry: a0, body: b1)
        b1 = phi(entry: b0, body: a1)
        n1 = phi(entry: n0, body: n2)
        if n1 > 0 goto body else done
body:   n2 = n1 - 1
        goto head
done:   print(a1)
        print(b1)
```

The body now holds only the counter; the whole swap lives in the phis, which is correct, because the phis at `head` read their operands at the same moment. The naive copies at the end of `body` are not. After `a1 = b1`, the copy `b1 = a1` reads the `a1` that the first copy wrote, so both variables end up with the old `b1`, and the program prints 2 twice. No order of the two copies works. The copies that replace a block's phis on one edge must act as one **parallel copy**, which reads all its sources before it writes any destination.[^ssabook] This is not an exotic situation: copy propagation is routine, and Braun's construction performs it while it builds.[^braun]

Isolating the phis avoids the swap as well. The copies at the end of `body` then write fresh names, `a1' = b1` and `b1' = a1`, which neither copy reads, so either order is correct; the copies at the top of `head` then move the fresh names into `a1` and `b1`.[^ssabook] But isolation adds a copy for every phi, and coalescing, whose job is to remove copies, may merge `a1'` back into `a1` and `b1'` into `b1`, which recreates the swap. So the SSA book's approach for machine code isolates every phi, treats the copies placed at each point as one parallel copy, coalesces, and only then turns the remaining copies into a sequence, as the next section does.[^ssabook]

## Sequentializing a parallel copy

A parallel copy is written `(a, b) := (b, a)`: the destinations on the left, each written once, and the sources on the right.[^leroy] To turn it into ordinary copies, draw it as a graph with an arrow from each source to its destination. No register is written twice, so each register has at most one arrow coming in, and every connected piece of the graph is either a tree or a single cycle with trees growing out of it. Rideau, Serpette and Leroy call this shape a **windmill**, with the cycles as its axles and the trees as its blades.[^leroy] The graph gives the algorithm:

1. While some pending copy writes a register that no pending copy still reads, emit it. These copies are the tips of the blades, and emitting one can free the register it read.
2. When no such copy is left, every pending copy lies on a cycle. Copy one register of a cycle into a spare register `t`, and change the copy that read that register to read `t` instead. The cycle is now a chain, and step 1 finishes it.[^ssabook] [^leroy]

The result has one copy for each copy that does not copy a register to itself, plus one per cycle,[^ssabook] and one spare register is always enough, a result Rideau, Serpette and Leroy call folklore.[^leroy] Figure 4 applies the algorithm to `(a, b, c, d) := (b, c, a, a)`, a three-register cycle with `d` hanging from `a`.

<figure class="vx-figure">
<svg viewBox="0 0 760 300" role="img" aria-label="Sequentializing the parallel copy (a, b, c, d) := (b, c, a, a) with one spare register" aria-describedby="o3-f4-desc">
<title id="o3-f4-title">A windmill and its sequence of copies</title>
<desc id="o3-f4-desc">Left: the registers a, b, c and d as circles, with an arrow from each source to its destination. The arrows a to c, c to b and b to a form a cycle, and an arrow from a to d hangs off it. A dashed circle t, the spare register, has moving dashed arrows from b to t and from t to a: the detour that breaks the cycle. Right: the five copies the algorithm emits, lighting up one after another: d = a, because nothing reads d; t = b, because only the cycle is left; b = c, because nothing reads b any more; c = a, because nothing reads c any more; and a = t, which gives a the old value of b. A note says: five copies, four that are not self-copies plus one for the cycle.</desc>
<defs><marker id="o3-f4-head" viewBox="0 0 10 10" refX="10" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker></defs>
<text class="vx-mono" x="20" y="24">(a, b, c, d) := (b, c, a, a)</text>
<line class="vx-line" x1="181" y1="107" x2="239" y2="193" marker-end="url(#o3-f4-head)"/>
<line class="vx-line" x1="230" y1="210" x2="110" y2="210" marker-end="url(#o3-f4-head)"/>
<line class="vx-line" x1="101" y1="193" x2="159" y2="107" marker-end="url(#o3-f4-head)"/>
<line class="vx-line" x1="190" y1="90" x2="300" y2="90" marker-end="url(#o3-f4-head)"/>
<line class="vx-flow" x1="83" y1="191" x2="56" y2="117" marker-end="url(#o3-f4-head)"/>
<line class="vx-flow" x1="68" y1="98" x2="150" y2="92" marker-end="url(#o3-f4-head)"/>
<circle class="vx-box" cx="170" cy="90" r="20"/>
<circle class="vx-box" cx="250" cy="210" r="20"/>
<circle class="vx-box" cx="90" cy="210" r="20"/>
<circle class="vx-box" cx="320" cy="90" r="20"/>
<circle class="vx-box" cx="50" cy="100" r="18" stroke-dasharray="4 3"/>
<text class="vx-mono" x="170" y="95" text-anchor="middle">a</text>
<text class="vx-mono" x="250" y="215" text-anchor="middle">c</text>
<text class="vx-mono" x="90" y="215" text-anchor="middle">b</text>
<text class="vx-mono" x="320" y="95" text-anchor="middle">d</text>
<text class="vx-mono" x="50" y="105" text-anchor="middle">t</text>
<text class="vx-text-muted" x="50" y="72" text-anchor="middle">spare</text>
<text class="vx-text-muted" x="170" y="172" text-anchor="middle">cycle</text>
<text class="vx-text-muted" x="245" y="78" text-anchor="middle">blade</text>
<text class="vx-text-muted" x="20" y="262">an arrow carries a value from its source</text>
<text class="vx-text-muted" x="20" y="278">to its destination</text>
<text class="vx-text" x="400" y="40">Copies, in the order emitted</text>
<g class="vx-seq" style="--vx-i: 0; --vx-n: 5"><text class="vx-mono" x="400" y="80">1. d = a</text><text class="vx-text-muted" x="490" y="80">nothing reads d</text></g>
<g class="vx-seq" style="--vx-i: 1; --vx-n: 5"><text class="vx-mono" x="400" y="120">2. t = b</text><text class="vx-text-muted" x="490" y="120">only the cycle is left: save b</text></g>
<g class="vx-seq" style="--vx-i: 2; --vx-n: 5"><text class="vx-mono" x="400" y="160">3. b = c</text><text class="vx-text-muted" x="490" y="160">nothing reads b any more</text></g>
<g class="vx-seq" style="--vx-i: 3; --vx-n: 5"><text class="vx-mono" x="400" y="200">4. c = a</text><text class="vx-text-muted" x="490" y="200">nothing reads c any more</text></g>
<g class="vx-seq" style="--vx-i: 4; --vx-n: 5"><text class="vx-mono" x="400" y="240">5. a = t</text><text class="vx-text-muted" x="490" y="240">a gets the old value of b</text></g>
<text class="vx-text-muted" x="400" y="284">5 copies: 4 that are not self-copies, plus 1 for the cycle</text>
</svg>
<figcaption>Figure 4. Sequentializing the parallel copy <code>(a, b, c, d) := (b, c, a, a)</code>. Left: a, c and b form a cycle, and d hangs from a like a blade from an axle. The dashed register t is the spare, and the moving dashes show the detour, from b through t to a, that breaks the cycle. Right: the copies in the order the fifth example emits them, lit one after another.</figcaption>
</figure>

The fifth example sequentializes the swap and Figure 4's windmill, and checks each result against the parallel meaning:

--8<-- "includes/examples/optimize/o3-ssa/parallel_copy.cpp.md"

Two practical points remain. First, the spare register must be able to hold the values it saves. A Vortex function whose phis exchange two `f32` values needs a floating-point spare, and one that exchanges integers needs an integer spare; in the CompCert compiler, which uses this algorithm for the moves before a call, exactly one register of each kind is free at that point for the purpose.[^leroy]

Second, the order of sequential copies creates **interference**: two values interfere when both must be kept at the same moment, so they cannot share a register. After `a1 = a2; b1 = b2`, the value of `b2` must survive while `a1` is written, so `a1` and `b2` interfere; the other order makes `a2` and `b1` interfere instead. A compiler may therefore keep parallel copies whole until after register allocation.[^ssabook]

??? check "Sequentialize the swap loop's copies on the edge from `body` to `head`: `(a1, b1, n1) := (b1, a1, n2)`."

    `n1 = n2` first, since no pending copy reads `n1`. Then only the cycle between `a1` and `b1` is left: `t = b1`, after which `b1 = a1` and `a1 = t` are both safe. That makes four copies: three that are not self-copies, plus one for the cycle. Coalescing can then give `n1` and `n2` one register and delete `n1 = n2`, because `n1` is no longer needed once `n2` is computed.

## Your turn: the kernel in SSA form

Here is the kernel from [stage 10](../compiler/guide/stage-10-matrix-multiplication.md#the-program-the-milestone-asks-for), at the size that O1 and O2 use:

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

[O2](o2-cfg-and-dominance.md#your-turn-the-kernels-inner-loop) drew its `k` loop as a graph: P before the loop, the header H with the test `k < 64`, K1 and K2 checking the two indices, K3 doing the arithmetic, S stepping `k`, X after the loop, and R reporting an error. Here it is in SSA form, half finished, with `row` and `column` standing for values defined outside the picture:

```text
P:   sum0 = 0.0
     k0 = 0
     goto H
H:   k1 = phi(P: k0, S: ___)
     sum1 = phi(P: ___, S: sum2)
     if k1 < 64 goto K1 else X
K1:  check a[row, k1]; a failure goes to R
K2:  check b[k1, column]; a failure goes to R
K3:  sum2 = sum1 + a[row, k1] * b[k1, column]
S:   k2 = ___
     goto H
X:   c[row, column] = ___
```

Fill in the four blanks, and say which copies destruction adds. Then count the phis for the whole function, lowered the same way, with a header for each of the three loops and `sum` set to 0.0 in the block before the `k` loop: how many does minimal SSA form place, and how many does pruned SSA form keep?

??? check "What fills the blanks, which copies does destruction add, and how many phis does each form place?"

    - `k1 = phi(P: k0, S: k2)`, `sum1 = phi(P: sum0, S: sum2)`, `k2 = k1 + 1`, and the store after the loop writes `sum1`.
    - The value that reaches X is `sum1`, not `sum2`. X's only predecessor is H, where the phi defines `sum1`, and K3, which defines `sum2`, does not dominate X. The last iteration's `sum2` reaches X by going around the back edge into `sum1`.
    - Destruction puts `k1 = k0` and `sum1 = sum0` at the end of P, and `k1 = k2` and `sum1 = sum2` at the end of S. Neither edge is critical, and neither pair of copies reads the other's destination, so any order works. Coalescing can then remove all four, since the two values in each copy are never needed at the same time.
    - Minimal SSA form places 9 phis. `row` gets one at its own header. `column` gets one at its header and one at the `row` header, because it is set to 0 inside the `row` loop. `k` and `sum` each get one at all three headers, because both are assigned inside the other two loops. Pruned SSA form keeps 4: one for each counter at its own header, and one for `sum` at the `k` header. At the outer headers, `column`, `k` and `sum` are assigned again before any use, so they are not live there.
    - Three of the dead phis, those at the `row` header for `column`, `k` and `sum`, have an operand for the edge that enters the `row` loop, and no assignment reaches it, because those variables are declared only inside that loop. Minimal form fills the operand with the undefined value; pruned form never places the phis.

Three rules of the language decide what SSA form does with this kernel:

- **Strict floating point.** A phi or a copy moves a value without computing anything, so SSA construction and destruction never change a bit of any `f32` result, and both are always legal under [decision 56](../decisions/numbers.md#d56). LLVM does allow fast-math flags on a phi, as hints that permit otherwise unsafe floating-point rewrites;[^langref-phi] a Vortex compiler must never set them. The phi for `sum` matters for another reason: `sum` is what LLVM's vectorizer documentation calls a **reduction variable**, one that each iteration updates from the previous iteration's value, and vectorizing a floating-point reduction in the usual way reorders its additions. LLVM does that only when reassociation is allowed, which decision 56 forbids. On AArch64 it can instead generate ordered reductions that keep the exact result, usually more slowly.[^llvm-vec] [P10](p10-vectorization.md) returns to ordered reductions.
- **`for` loops.** Each iteration binds a new, immutable `k` ([decision 13](../decisions/statements.md#d13)). SSA form names that binding without a copy: it is the result of the phi at the header.
- **`&mut` outputs.** The store to `c[row, column]` stays a store. SSA form covers values, not memory; LLVM does not put memory in SSA form,[^kal7] and [O9](o9-alias-analysis.md) introduces MemorySSA, a separate structure for reasoning about loads and stores.

## For Vortex

!!! vortex "Exercise"

    **Decide first.** Write a design note of at most one page that chooses how your compiler reaches SSA form, and why:

    1. Braun-style construction in your own IR, while lowering the checked tree;
    2. Cytron-style construction on your own IR, using the dominator tree and frontiers from [O2](o2-cfg-and-dominance.md)'s exercise;
    3. stack slots in LLVM IR, promoted by mem2reg and SROA (only if your back end is LLVM).

    The note must say, for the first choice, which blocks your lowering seals and when; for the second, whether you prune and how you compute liveness; for the third, where the allocas go and which Vortex constructs make an address escape. It must also say who destroys SSA form: your own back end, or LLVM's.

    **Then build:**

    1. The construction you chose. For the third choice, the stack slots, with every alloca at the start of the entry block.
    2. An SSA verifier, run after construction and after every later pass: every value is defined once; every use is dominated by its definition, with each phi operand checked at the end of its predecessor; phis come first in their block, with exactly one operand per predecessor; and, if your form is pruned, no operand in a block that some path reaches is undefined, since Vortex programs are strict.
    3. If your back end is your own, destruction: split critical edges or isolate the phis, collect the copies placed at each point into a parallel copy, and sequentialize it with one spare register for each kind of register, integer and floating point.

    **Not yet:** folding, value numbering and copy propagation during construction ([O5](o5-constants-and-dead-code.md), [O6](o6-redundancy.md)); coalescing beyond deleting copies whose source and destination already share a name ([C4](../backend/c4-graph-coloring.md)); SSA form for memory ([O9](o9-alias-analysis.md)); repairing SSA form after a transformation (rebuild it instead); loop-closed and other loop forms ([O8](o8-loops.md)).

    **Proof that it works:**

    - A round trip on every test program: run it before SSA construction, in SSA form if you have an IR interpreter, and after destruction. Output, runtime error line and exit status must match the golden files byte for byte.
    - Destruction tests written directly in your IR, since your compiler may not yet produce these shapes from source: the lost-copy loop and the swap loop from this chapter, each of which must print what its phis mean, 3 for the first and 2 then 1 for the second.
    - The Vortex swap program from this chapter, whose golden output is 2, then 1.
    - The verifier rejects broken SSA: a use before its definition, a phi with an operand missing, a phi after an ordinary instruction.
    - A table for three functions, filled in from your compiler's output, with the date and your compiler's version. Counting only `let mut` variables and loop counters, the first two columns can be checked against this chapter: 3 and 2 for the stage 7 loop, 9 and 4 for the kernel.

    | Function | Phis, minimal | Phis, pruned | Phis your compiler made | Copies after destruction | Copies after coalescing |
    | --- | --- | --- | --- | --- | --- |
    | stage 7 `main` | | | | | |
    | stage 10 `multiply` | | | | | |
    | the swap program | | | | | |

## Key ideas

!!! recap "Questions you can now answer"

    - **What does SSA form guarantee?** Each variable is assigned exactly once in the program text, so every use names its one definition.
    - **When does a phi read its operands?** On the incoming edge, at the end of the predecessor, and all the phis of a block read before any of them writes.
    - **What separates minimal from pruned SSA form?** Minimal places phis at the iterated dominance frontier; pruned also drops those whose variable is not live.
    - **How does renaming find the version a use sees?** With a stack per variable during a walk of the dominator tree: the top is the nearest assignment that dominates the use.
    - **How does Braun's construction do without dominance?** It looks backwards from each read, creates phis at joins on demand, completes them when a block is sealed and removes the trivial ones.
    - **What must a stack slot satisfy for mem2reg to promote it?** It must be an alloca in the entry block, holding a single value, used only by direct loads and stores.
    - **Why can naive destruction go wrong, and what fixes it?** After optimization, versions in a phi-web can be live together: splitting critical edges prevents the lost copy, treating each edge's copies as a parallel copy prevents the swap, and isolating each phi prevents both.

## Where this comes back

!!! next "You will use this again in"

    - [O4. Dataflow analysis](o4-dataflow.md): *liveness*, *def-use chains*
    - [O5. Constants and dead code](o5-constants-and-dead-code.md): *copy propagation*, *dead phis*
    - [O6. Redundancy: CSE, GVN, PRE and LICM](o6-redundancy.md): *value numbering*, *scalar promotion*
    - [O7. Calls and aggregates: inlining and SROA](o7-inlining-and-sroa.md): *stack slots*, *mem2reg*, *escaping addresses*
    - [O8. Loops: structure, induction variables and bounds checks](o8-loops.md): *header phis*, *loop rotation*
    - [O9. Memory: alias analysis and MemorySSA](o9-alias-analysis.md): *memory outside SSA form*
    - [O12. Testing an optimizer](o12-testing-optimizers.md): *SSA verifier*, *round-trip tests*
    - [P10. Vectorization](p10-vectorization.md): *reduction phi*, *ordered reductions*
    - [A4. Calling conventions and ABIs](../backend/a4-calling-conventions.md): *parallel moves*
    - [C2. Liveness](../backend/c2-liveness.md): *strict SSA form*, *liveness*
    - [C4. Register allocation II: graphs and SSA](../backend/c4-graph-coloring.md): *coalescing*, *interference*, *parallel copies*
    - [M2. Reading MLIR](../mlir/m2-reading-mlir.md): *block arguments*

## Sources and further reading

Read Braun and his coauthors first: their construction is four short pieces of pseudocode, and the paper explains the problem of incomplete graphs that every front end meets. Then read Cytron and his coauthors for the classic method and its proofs, and chapters 2, 3 and 21 of the SSA book draft for conventional and transformed SSA form and for destruction. The Kaleidoscope chapter is the practical guide to the LLVM route.

[^cytron]: Ron Cytron, Jeanne Ferrante, Barry K. Rosen, Mark N. Wegman and F. Kenneth Zadeck, "Efficiently Computing Static Single Assignment Form and the Control Dependence Graph", *ACM Transactions on Programming Languages and Systems* 13(4), 1991: the abstract, sections 1, 2 and 3 (with footnote 1), 5.1, 5.2 (figure 12), 7, 7.1 and 7.2. <https://doi.org/10.1145/115372.115320> (free copy: <https://www.cs.utexas.edu/~pingali/CS380C/2010/papers/ssaCytron.pdf>)
[^braun]: Matthias Braun, Sebastian Buchwald, Sebastian Hack, Roland Leißa, Christoph Mallon and Andreas Zwinkau, "Simple and Efficient Construction of Static Single Assignment Form", *Compiler Construction (CC 2013)*, Springer, 2013: sections 1, 2.1 to 2.3, 3.1, 3.2, 4, 5.1, 6.1 (tables 1 and 2) and 7. <https://doi.org/10.1007/978-3-642-37051-9_6> (authors' copy: <https://pp.ipd.kit.edu/uploads/publikationen/braun13cc.pdf>)
[^ssabook]: Fabrice Rastello and Florent Bouchez Tichadou (editors), *SSA-based Compiler Design*, Springer, 2022, <https://doi.org/10.1007/978-3-030-80515-9>. Read in the draft of 8 June 2018, whose chapter numbers are used here: chapter 2, "Properties and Flavors" (Brisk, Rastello), sections 2.3 to 2.5; chapter 3, "Standard Construction and Destruction Algorithms" (Singer, Rastello), sections 3.1.4 and 3.2, with algorithms 3.5 and 3.6; chapter 18, "SSA Form and Code Generation" (Dupont de Dinechin), section 18.3; chapter 21, "SSA Destruction for Machine Code" (Rastello), sections 21.1 and 21.4. Unofficial mirror of the draft: <https://pfalcon.github.io/ssabook/latest/book-full.pdf>
[^langref-phi]: LLVM Project, "LLVM Language Reference Manual", section "'phi' Instruction". <https://llvm.org/docs/LangRef.html#phi-instruction>
[^mlir-rationale]: MLIR Project, "MLIR Rationale", section "Block Arguments vs PHI nodes". <https://mlir.llvm.org/docs/Rationale/Rationale/#block-arguments-vs-phi-nodes>
[^kal7]: LLVM Project, "My First Language Frontend with LLVM Tutorial", chapter 7, "Extending the Language: Mutable Variables", section 7.3, "Memory in LLVM". <https://llvm.org/docs/tutorial/MyFirstLanguageFrontend/LangImpl07.html#memory-in-llvm>
[^perftips]: LLVM Project, "Performance Tips for Frontend Authors", section "Use of allocas". <https://llvm.org/docs/Frontend/PerformanceTips.html#use-of-allocas>
[^llvm-passes]: LLVM Project, "LLVM's Analysis and Transform Passes", entries `mem2reg`, `sroa` and `licm`. <https://llvm.org/docs/Passes.html>
[^llvm-promote]: LLVM Project, `PromoteMemoryToRegister.cpp`, release/18.x branch: the file header, `isAllocaPromotable`, and the comments on live-in blocks and on slots with a single store. <https://github.com/llvm/llvm-project/blob/release/18.x/llvm/lib/Transforms/Utils/PromoteMemoryToRegister.cpp>
[^llvm-codegen]: LLVM Project, "The LLVM Target-Independent Code Generator", section "The SSA deconstruction phase". <https://llvm.org/docs/CodeGenerator.html#the-ssa-deconstruction-phase>
[^llvm-phielim]: LLVM Project, `PHIElimination.cpp`, release/18.x branch: the file header, `LowerPHINode`, and `SplitPHIEdges` with its comments on helping the coalescer and on loop back edges. <https://github.com/llvm/llvm-project/blob/release/18.x/llvm/lib/CodeGen/PHIElimination.cpp>
[^llvm-loops]: LLVM Project, "LLVM Loop Terminology (and Canonical Forms)", section "Rotated Loops". <https://llvm.org/docs/LoopTerminology.html#rotated-loops>
[^leroy]: Laurence Rideau, Bernard Paul Serpette and Xavier Leroy, "Tilting at Windmills with Coq: Formal Verification of a Compilation Algorithm for Parallel Moves", *Journal of Automated Reasoning*, 2008, sections 1 to 3. <https://doi.org/10.1007/s10817-007-9096-8> (author's copy: <https://xavierleroy.org/publi/parallel-move.pdf>)
[^llvm-vec]: LLVM Project, "Auto-Vectorization in LLVM", section "Reductions", read on 2026-09-24. <https://llvm.org/docs/Vectorizers.html#reductions>

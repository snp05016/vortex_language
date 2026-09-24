# C4. Register allocation II: graphs and SSA

<p class="page-intro">Chaitin and Briggs coloring turns register allocation into the literal graph problem C3 only used as a name: build the interference graph, then color it. This chapter builds one by hand, shows why SSA form gives that graph a shape one pass can color, and covers coalescing, the trick that deletes a move for free.</p>

<p class="vx-meta" markdown="1">Level: Advanced · Reading time: about 45 minutes · Builds on: [C3. Register allocation I: linear scan](c3-linear-scan.md), [O3. SSA form: construction and destruction](../optimize/o3-ssa.md)</p>

???+ remember "Before you start, remember"

    ??? question "What does it mean for two values to interfere, and what problem does allocating registers become once you know which pairs do?"

        Two values interfere when some point exists where both are live. Allocating registers becomes a coloring problem: pick a register ("color") for every value so that no two interfering values share one, using no more colors than the machine has registers.

        Introduced in [C3. Register allocation I: linear scan](c3-linear-scan.md#from-a-live-value-to-a-register).

    ??? question "What does SSA form guarantee about a value's definition, and where does a phi's use of an incoming value happen?"

        Every value has exactly one definition. A phi's use of an incoming value happens on the edge from the matching predecessor block, not inside the phi's own block; every other use must be dominated by its value's definition.

        Introduced in [O3. SSA form: construction and destruction](../optimize/o3-ssa.md#one-name-one-definition).

    ??? question "How does Rideau, Serpette and Leroy's algorithm turn a parallel copy into an ordinary sequence, and how many spare registers does it need?"

        Emit any pending copy whose destination nothing else still reads. When none is left, the remaining copies form one or more cycles: save one register of a cycle into a spare, redirect the copy that read it to read the spare instead, and the cycle becomes an ordinary chain. One spare register is always enough, however long the cycle.

        Introduced in [O3. SSA form: construction and destruction](../optimize/o3-ssa.md#sequentializing-a-parallel-copy).

    ??? question "Which registers does AAPCS64 make callee-saved, and which does the System V AMD64 ABI leave with none at all?"

        AAPCS64 makes `x19`-`x28` callee-saved among the general-purpose registers and the low 64 bits of `v8`-`v15` callee-saved among the floating-point ones. SysV AMD64 makes `rbx`, `rbp` and `r12`-`r15` callee-saved, but no XMM register at all.

        Introduced in [A4. Calling conventions and ABIs](a4-calling-conventions.md#caller-saved-and-callee-saved-registers).

!!! goals "In this chapter"

    - Explain why register allocation is graph coloring, and why coloring in general offers no shortcut a compiler can rely on for every input.
    - Run Chaitin's simplify/select algorithm by hand, then see Briggs's optimistic change avoid a spill Chaitin's own rule would take.
    - Recognize why an SSA-form program's interference graph is chordal, and use that fact to color it in one pass, with no backtracking and no guessing.
    - Apply Briggs's conservative coalescing test to delete a copy for free, and see why the test sometimes refuses a merge that would in fact have been safe.
    - Compare graph coloring against linear scan on quality and on compile time, and place the choice against what LLVM and Cranelift ship.

## A graph even a short example already needs

[C3](c3-linear-scan.md#live-intervals-one-range-instead-of-a-graph) defined a live interval from five values computed one after another:

```text
1  a = 2
2  b = 3
3  c = a + b
4  d = a * b
5  e = c + d
6  return e
```

with intervals `a: [1,4]`, `b: [1,4]`, `c: [3,5]`, `d: [4,5]`, `e: [5,6]`. C3 used these only to explain what an interval is. Take them one step further: draw the **interference graph** C3 named but never built, one node per value, one edge between every pair of intervals that overlap.

Check every pair. `a` and `b` share the same range, `[1,4]`, so they overlap and interfere. `a` and `c`, `[1,4]` against `[3,5]`, overlap at 3 and 4. `a` and `d`, `[1,4]` against `[4,5]`, overlap at 4: sharing an endpoint still counts, exactly as C3's own check question established. `b` interferes with `c` and `d` the same way `a` does, by the same ranges. `c` and `d`, `[3,5]` against `[4,5]`, overlap throughout `d`'s range. `c` and `e`, and `d` and `e`, overlap at 5. Only `a`/`e` and `b`/`e` fail to overlap: `[1,4]` ends before `[5,6]` starts.

The result is `a`, `b`, `c` and `d` pairwise connected to one another, a **complete graph** on four nodes (every pair of its nodes has an edge), with `e` hanging off `c` and `d`. A complete graph on $n$ nodes needs exactly $n$ colors: every node is every other node's neighbor, so no two can ever share one. Four values are, in fact, simultaneously live at position 4: `a` and `b` (used one more time, in `d`'s computation), `c` (defined at 3, still needed at 5), and `d` (newly defined). With three registers, one of those four cannot get one. Not from a bad choice, from the shape of the graph: **coloring** a graph means giving each node a color so that no edge joins two nodes of the same color, and this graph's clique of four forces at least four colors before any algorithm runs.

This is the fact C3 only referred to: building the graph explicitly turns a vague sense of "these values are all alive together" into an object you can compute on, at the price of building it at all, which is exactly the cost linear scan was built to avoid.

## Coloring by simplify and select

Chaitin's 1982 algorithm colors an interference graph in two passes over a stack.[^chaitin82]

**Simplify.** While some node has degree less than $k$ (fewer neighbors than available registers), remove it from the graph and push it onto a stack. A node with fewer than $k$ neighbors is always colorable later: however its neighbors end up colored, at most $k - 1$ colors are taken, leaving one free. When no such node is left but nodes remain, the graph is **stuck**.

**Select.** Pop the stack. Each popped node's neighbors were pushed later (they left the graph before it did) and are already colored; give the node any color its neighbors are not using.

Simplify alone handles the straightforward cases. The interesting question is what to do when stuck: some node's degree is $k$ or more, so no move is guaranteed safe. Chaitin's original rule picks the highest-degree remaining node and spills it immediately, generating memory traffic for it without ever trying to color it. Briggs, Cooper and Torczon's 1994 paper changes one thing: push the stuck node onto the stack anyway, marked as an **optimistic** candidate, and decide only at select time, when its actual neighbors' actual colors are known, whether a color remains.[^briggs94] This never costs more spills than Chaitin's rule, and sometimes costs fewer, because a node with high degree can still end up with neighbors that share colors among themselves.

Run both rules on one small graph, with $k = 3$: a node `s` that interferes with four others, `p`, `q`, `r`, `t`, which interfere with each other only in a 4-cycle (`p`-`q`-`r`-`t`-`p`).

--8<-- "includes/examples/backend/c4-graph-coloring/simplify_select.cpp.md"

Every node's degree is 3 or more at the start (`s` has 4, each of `p`, `q`, `r`, `t` has its two cycle neighbors plus `s`, for 3), so simplify cannot even begin: the graph is stuck immediately. Chaitin's rule spills `s`, the highest-degree node, on the spot. The cycle then simplifies freely (each of its nodes drops to degree 2 once `s` is gone) and colors with two colors, alternating around the cycle. Briggs's rule pushes `s` instead of spilling it, lets the same cycle simplify and color the same way, and only then pops `s`: its four neighbors used only two colors between them, so a third color is free, and `s` colors without ever touching memory.

<figure class="vx-figure">
<svg viewBox="0 0 760 340" role="img" aria-labelledby="c4-f1-title c4-f1-desc">
<title id="c4-f1-title">The same interference graph colored by Chaitin's rule and by Briggs's rule</title>
<desc id="c4-f1-desc">Two identical graphs, each a hub node s connected to four nodes p, q, r, t that form a cycle among themselves. Left, under Chaitin's rule, s is spilled: drawn dashed, no color. p and r hold one color, q and t hold a second color. Right, under Briggs's rule, the cycle is colored exactly the same way, but s is not spilled: since its four neighbors used only two colors, s takes a third color and every node ends up registered.</desc>
<text class="vx-text" x="60" y="30">Chaitin: spill on stuck</text>
<line class="vx-line" x1="190" y1="106" x2="190" y2="154"/>
<line class="vx-line" x1="190" y1="206" x2="190" y2="254"/>
<line class="vx-line" x1="116" y1="180" x2="164" y2="180"/>
<line class="vx-line" x1="216" y1="180" x2="264" y2="180"/>
<line class="vx-line" x1="171" y1="163" x2="109" y2="163"/>
<line class="vx-line" x1="171" y1="197" x2="109" y2="197"/>
<line class="vx-line" x1="271" y1="163" x2="209" y2="163"/>
<line class="vx-line" x1="271" y1="197" x2="209" y2="197"/>
<circle class="vx-box-bad" cx="190" cy="180" r="26"/>
<text class="vx-mono" x="190" y="185" text-anchor="middle">s</text>
<circle class="vx-box-strong" cx="190" cy="90" r="26"/>
<text class="vx-mono" x="190" y="95" text-anchor="middle">p</text>
<circle class="vx-box" cx="290" cy="180" r="26"/>
<text class="vx-mono" x="290" y="185" text-anchor="middle">q</text>
<circle class="vx-box-strong" cx="190" cy="270" r="26"/>
<text class="vx-mono" x="190" y="275" text-anchor="middle">r</text>
<circle class="vx-box" cx="90" cy="180" r="26"/>
<text class="vx-mono" x="90" y="185" text-anchor="middle">t</text>
<text class="vx-text-muted" x="190" y="315" text-anchor="middle">s: spilled</text>
<text class="vx-text-muted" x="60" y="330" text-anchor="middle">p, r: color 1</text>
<text class="vx-text-muted" x="320" y="330" text-anchor="middle">q, t: color 0</text>
<text class="vx-text" x="470" y="30">Briggs: push on stuck</text>
<line class="vx-line" x1="600" y1="106" x2="600" y2="154"/>
<line class="vx-line" x1="600" y1="206" x2="600" y2="254"/>
<line class="vx-line" x1="526" y1="180" x2="574" y2="180"/>
<line class="vx-line" x1="626" y1="180" x2="674" y2="180"/>
<line class="vx-line" x1="581" y1="163" x2="519" y2="163"/>
<line class="vx-line" x1="581" y1="197" x2="519" y2="197"/>
<line class="vx-line" x1="681" y1="163" x2="619" y2="163"/>
<line class="vx-line" x1="681" y1="197" x2="619" y2="197"/>
<circle class="vx-box-accent" cx="600" cy="180" r="26"/>
<text class="vx-mono" x="600" y="185" text-anchor="middle">s</text>
<circle class="vx-box-strong" cx="600" cy="90" r="26"/>
<text class="vx-mono" x="600" y="95" text-anchor="middle">p</text>
<circle class="vx-box" cx="700" cy="180" r="26"/>
<text class="vx-mono" x="700" y="185" text-anchor="middle">q</text>
<circle class="vx-box-strong" cx="600" cy="270" r="26"/>
<text class="vx-mono" x="600" y="275" text-anchor="middle">r</text>
<circle class="vx-box" cx="500" cy="180" r="26"/>
<text class="vx-mono" x="500" y="185" text-anchor="middle">t</text>
<text class="vx-text-muted" x="600" y="315" text-anchor="middle">s: color 2</text>
<text class="vx-text-muted" x="470" y="330" text-anchor="middle">p, r: color 1</text>
<text class="vx-text-muted" x="730" y="330" text-anchor="middle">q, t: color 0</text>
</svg>
<figcaption>Figure 1. The same graph, colored two ways. Chaitin's rule spills the hub node <code>s</code> the moment it is stuck; Briggs's rule pushes it optimistically and finds, at select time, that its neighbors left a color free. Neither rule changes how the four-node cycle itself colors.</figcaption>
</figure>

??? check "Could Briggs's rule ever produce *more* spills than Chaitin's, on the same graph?"

    No. Every node Chaitin's rule spills outright, Briggs's rule pushes onto the stack instead and only spills if, at select time, none of the k colors is free for it. Every other decision the two rules make is identical. Briggs's rule strictly dominates Chaitin's: same or fewer spills, never more, on any graph.

## Why coloring cannot be shortcut in general

Graph coloring is a classic hard problem: deciding whether a graph can be colored with $k$ or fewer colors, for $k \geq 3$, is NP-complete, and no algorithm known today solves every instance quickly as graphs grow. Simplify/select is a heuristic, not a solver: it colors correctly whenever it succeeds, but it can get stuck on a graph that a smarter search would still color within $k$, and even Briggs's optimistic push only reduces how often that happens, without removing it. This is exactly the shape C3 warned about at the start: checking every pair of values for interference already costs time growing with the square of how many values there are, before coloring is even attempted, and a compiler that must return quickly, mid-keystroke or mid-request, cannot always pay it.

SSA form removes one whole source of that difficulty, not by making the underlying coloring problem easier in general, but by guaranteeing that the specific graphs a compiler builds from SSA-form code have a shape general graphs do not.

## SSA turns coloring linear: chordal graphs

A **chord** is an edge joining two nodes of a cycle that are not next to each other on it. A graph is **chordal** when every cycle of length 4 or more has one. Hack's result, later given a simpler proof by Pereira and Palsberg, is that the interference graph of an SSA-form program is always chordal.[^hack07] [^pereira05] The reason traces straight back to the SSA property this chapter's remember box opened with: every value has one definition, and every ordinary use is dominated by it, so a value's live range follows a single, connected stretch of the dominator tree rather than jumping between unrelated definitions the way a non-SSA variable's can. That structural fact is what rules out chordless cycles.

Chordal graphs have a property simplify/select does not need to search for: a **perfect elimination ordering**, a way to remove nodes one at a time such that, at each step, the node's remaining neighbors already form a clique (are all pairwise connected). Coloring in the *reverse* of that order, greedily, always succeeds using no more colors than the graph's largest clique needs, which is provably the fewest any coloring can use.[^pereira05] No stack of "stuck" decisions, no spilling to try again: the order alone guarantees success.

**Maximum cardinality search** (MCS) finds such an ordering without knowing in advance that the graph is chordal: repeatedly visit whichever unvisited node currently has the most already-visited neighbors.[^pereira05] Run it on five nodes shaped like an SSA-form loop's live ranges after renaming: `e` live across the whole loop, interfering with everything, and `a`-`b`-`c`-`d` a path where each overlaps only its immediate neighbors (so `a` and `c`, or `b` and `d`, never interfere, the chord-free gap a genuinely non-chordal graph would need).

--8<-- "includes/examples/backend/c4-graph-coloring/chordal_coloring.cpp.md"

Coloring in the reverse MCS order uses three colors, matching the graph's largest clique (`e` together with any two path neighbors, such as `e`, `a`, `b`). Coloring the identical graph in an arbitrary order, `a`, `e`, `b`, `d`, `c`, wastes a fourth color on `c`: by the time `c` is reached, its neighbors `b`, `d` and `e` already hold three different colors between them, even though no clique in the graph needs four. The graph did not change; only throwing away the order chordality provides did.

??? check "Why does a general, non-chordal graph not offer the same guarantee?"

    A perfect elimination ordering exists only because a chordal graph's structure rules out chordless cycles of length 4 or more. A general graph can have such a cycle (four nodes each connected only to their two cycle neighbors, with no diagonal), and no ordering of that cycle's nodes makes each one's remaining neighbors a clique when it is removed, so greedy coloring in any fixed order can be forced into using an extra color that a smarter, graph-specific search might have avoided.

## Deleting moves for free: coalescing

A register allocator inherits plenty of copies: a phi resolved into ordinary moves, a value passed straight through from one variable to another, a call's argument already sitting in the right register. If two copy-related values, joined only by a move with no interference edge between them, end up with the *same* color, the move copies a register into itself and can be deleted outright. **Coalescing** decides when to force that outcome by merging the two nodes into one before coloring.

The risk is that merging can raise some other node's degree past $k$ where it would not otherwise have been, turning a graph that colored fine into one that spills. Briggs's conservative test avoids that risk without checking the merged graph's colorability directly, which would cost as much as coloring it: merge `a` and `b` only if the resulting node has fewer than $k$ neighbors whose degree, in the graph *before* merging, is already $k$ or more (a "significant-degree" neighbor).[^briggs94] The test is conservative, not exact: it sometimes refuses a merge that a full check would have allowed, but it never approves one that breaks colorability.

--8<-- "includes/examples/backend/c4-graph-coloring/coalescing.cpp.md"

The first graph's candidates, `p` and `q`, share two neighbors of degree 2 each, below the threshold of $k = 3$, and merging them leaves a triangle any three colors handle. The second graph's candidates, `a` and `b`, share three neighbors that are already a triangle among themselves, each of degree 3 or more, so Briggs's test refuses the merge; forcing it anyway, as the brute-force check confirms, produces a four-node complete graph that three colors cannot color. Two candidates, one word apart in how the test reads them, one real spill apart in what merging them would have cost.

George and Appel's 1996 **iterated coalescing** interleaves this test with simplify/select rather than running it once beforehand: simplify, coalesce whatever Briggs's test now allows (some nodes' degrees drop as others simplify away, so more merges become safe as the process continues), and repeat until neither move makes progress.[^george96] This chapter does not build that interleaving; treat it as reading once simplify/select and coalescing separately both make sense, since the payoff is fewer moves left over, not a different coloring result on graphs that were already going to color.

??? check "Why does Briggs's test use each neighbor's degree *before* merging, rather than the merged node's own degree?"

    The merged node's own final degree is exactly what the test is trying to bound without computing the whole merged graph. Using each neighbor's pre-merge degree lets the test count, cheaply, how many of the merged node's neighbors could plausibly force a spill on their own account, which is enough to guarantee safety even though it is not the tightest possible bound.

## Moves that still have to run

Coalescing deletes some moves outright; it does not delete all of them. A phi that reads two values which end up needing *different* registers still needs code to move one into the other on the edge where it happens, and once several phis in the same block are resolved together, or several coalescing decisions leave several moves live at the same program point, those moves can read each other's destinations: exactly the parallel-copy problem O3 already solved, sequentializing with one spare register regardless of how many moves are tangled together.[^leroy08] Nothing in this chapter changes that algorithm; graph coloring and coalescing only change how many moves survive to reach it, and SSA-based allocation in particular tends to leave more of them at block boundaries than a non-SSA allocator does, because SSA keeps every version of a variable as a separate node until something explicit merges them back.

## Graph coloring versus linear scan

C3 built an allocator that never constructs an interference graph at all, trading exactness for a running time close to linear. This chapter's allocator builds the graph on purpose, and inherits both the strength that comes with it (a coloring decision uses the graph's real structure, not an interval approximation with holes C3 already flagged as lossy) and the cost (building the graph, in the worst case, costs time proportional to the square of the number of values, before either algorithm's own work begins). SSA-based coloring narrows, but does not erase, that gap: building the chordal interference graph and running MCS on it is still more work than a single sorted sweep over intervals, even though the coloring step itself, once the graph exists, is a single linear pass rather than simplify/select's repeated search for a low-degree node.

Production allocators mostly did not resolve this trade-off by choosing graph coloring outright. C3 already followed LLVM's own path: linear scan through version 2.9, replaced for LLVM 3.0 by a **greedy** allocator that stayed with live intervals rather than adopting a graph, adding a priority queue (largest live ranges first), eviction of a lower-priority interval when a higher-priority one needs its register, and live-range splitting to shrink an interval around the parts that need one register continuously.[^olesen11] Cranelift's regalloc2 goes a different way again: live-range **bundles** that a backtracking search can evict and retry, checked against a symbolic verification engine that confirms the finished allocation carries the same dataflow as the pre-allocation program, a correctness net neither Chaitin's nor Briggs's original papers had.[^fallin22] [^regalloc2doc] Neither production allocator is Chaitin/Briggs graph coloring as this chapter built it; both borrow its vocabulary (spill cost, coalescing, live ranges as the unit of work) while keeping compile time closer to what C3's motivation demanded. One CMU compilers course goes further and skips Chaitin/Briggs entirely in its own teaching order, calling the classic algorithms complicated to implement correctly and teaching chordal SSA coloring instead, exactly the order this book has now covered both halves of.[^cmu411]

## Applying it to the Vortex matmul kernel

C3 listed the matmul kernel's live values and where each one's interval runs: `a`, `b` and `c` live for the whole function (already sitting in `x0`, `x1`, `x2` on entry, per AAPCS64), `row` across the outer two loops, `column` across the inner two, and `k` and `sum` only inside the innermost loop.[^stage10]

```vortex
// items: valid
fn multiply(a: &[f32; 2, 3], b: &[f32; 3, 2], c: &mut [f32; 2, 2]) {
    for row in 0..2 {
        for column in 0..2 {
            let mut sum: f32 = 0.0;
            for k in 0..3 {
                sum += a[row, k] * b[k, column];
            }
            c[row, column] = sum;
        }
    }
}
```

Build the interference graph the way this chapter's first section built one from C3's five-value example, and its shape at the innermost point is stark: `a`, `b` and `c` are live everywhere, so they interfere with every other value in the function, including each other, a clique of three by themselves. `row` and `column`, live across their enclosing loops, interfere with `a`, `b`, `c` and with whatever runs inside them. `k` and `sum`, live only in the innermost loop, still interfere with all five of the others, because all five are live at every point the innermost loop reaches. At the single program point deepest in the nest, every one of these values is simultaneously live: a complete graph, seven nodes, needing seven colors, before counting a single address-arithmetic temporary the instruction selector introduces for `a[row, k]` or `b[k, column]`. The research behind this book counts roughly ten live values at that point once those temporaries are included, which both AArch64 (31 general-purpose registers) and x86-64 (16) have room for, so the question this kernel raises is not whether register allocation *can* succeed here, but whether the allocator wastes any of that room.

A clique is, in one sense, the easiest graph a coloring algorithm ever sees: every node's remaining neighbors, at every step of simplify or of an elimination ordering, already form a clique themselves (there is only one clique, the whole thing), so any order colors it in exactly as many colors as it needs, no more. The hard cases this chapter built graphs to show, the wheel that tempts Chaitin's rule into an unnecessary spill, the arbitrary order that wastes a color on a chordal graph, come from structure *between* cliques, not from a clique's own size. Vortex's strict floating-point rules matter here too: `sum`'s accumulation must run in exactly the order the source wrote it, with no reassociation, so an allocator cannot lower register pressure by splitting `sum` into several partial accumulators combined at the end, the way it might for an operation the specification allowed to reorder.[^numbers56] Whatever register pressure this kernel has, `sum` has to be paid for as one value, one interval, one color, from the first iteration of `k` to the store into `c[row, column]`.

??? check "Does coalescing have anything to delete in this kernel, as shown?"

    As written, no ordinary register-to-register copies appear in the source: every assignment either computes a new value or updates `sum` in place. Copies worth coalescing typically appear later, introduced by the compiler itself: resolving phis at the loop headers `row`, `column` and `k` each pass through, or moving a value into the specific register a call or the ABI requires. This kernel makes no such calls, so most of the coalescing this chapter covered would matter more on a kernel with a function call inside the loop, or with phis this straight-line example never had to resolve.

## For Vortex

!!! vortex "Exercise"

    **Build.** In your own compiler, build an interference graph over the live values C2's analysis (or C3's live intervals, converted to an overlap graph the way this chapter's opening section did) produces for one function. Implement simplify/select with Briggs's optimistic push, not Chaitin's immediate spill. Separately, for a function already in SSA form, implement maximum cardinality search and color in the reverse order it finds, and confirm on a few test graphs that it never uses more colors than the graph's largest clique. Implement Briggs's conservative coalescing test and apply it, before coloring, to every copy-related pair your compiler's phi resolution or ABI lowering introduces.

    **Do not build yet.** Iterated coalescing's interleaving of coalesce and simplify; run the two separately first, and treat George and Appel's paper as reading once both halves work alone. Live-range splitting or any other change to what a "value" is before the graph is built; C5 covers splitting, and this chapter's graphs assume C3's live ranges (or SSA values) exactly as given. Any interaction with instruction scheduling; C6 is where register pressure and scheduling order meet.

    **The test that proves it works.** Reuse C3's allocation checker unchanged: it only reads a finished assignment and confirms no two same-register intervals overlap, so it checks this chapter's allocator's output exactly as it checked linear scan's. Run both allocators, C3's linear scan and this chapter's graph coloring, on the same matmul kernel and on the same fuzzed random interval sets C3's exercise generates, and record, for each input, how many values each allocator spills. Do not expect graph coloring to win every time on a straight-line, hole-free function like the kernel above; the comparison is the point, not a predicted winner, and any claim about which allocator is better belongs to the numbers your own harness prints, not to this page.

## Key ideas

!!! recap

    - **What is an interference graph, and when does simplify/select get stuck on one?** One node per live value, one edge per interfering pair; it gets stuck when every remaining node has degree k or more, so no node is safe to simplify without a decision.
    - **What does Briggs's optimistic rule change from Chaitin's original rule?** Instead of spilling a stuck node immediately, push it onto the stack anyway and decide at select time, when its actual neighbors' colors are known; this never produces more spills than Chaitin's rule and sometimes produces fewer.
    - **Why is an SSA-form program's interference graph chordal?** Because SSA gives every value one definition and confines its live range to a connected stretch of the dominator tree, which rules out the chordless long cycles a general interference graph can have.
    - **What does a perfect elimination ordering buy a chordal graph?** Coloring in its reverse uses exactly as many colors as the graph's largest clique needs, in one linear pass, with no stuck decisions and no spilling to retry.
    - **What does Briggs's conservative coalescing test check, and why is it conservative rather than exact?** Whether the merged node would have fewer than k neighbors whose pre-merge degree is already k or more; it sometimes refuses a safe merge, but it never approves one that breaks colorability.
    - **Why do neither LLVM's greedy allocator nor Cranelift's regalloc2 build a literal Chaitin/Briggs graph?** Both prioritize compile time and live-range splitting over an exact interference graph, while still borrowing graph coloring's vocabulary, spill cost, coalescing and live ranges as the unit of work.

## Where this comes back

!!! next "You will use this again in"

    - [C5. Spilling, splitting and rematerialization](c5-spilling.md): *spill cost*, *SSA spilling decided before coloring*
    - [C6. Instruction scheduling](c6-scheduling.md): *register pressure*
    - [D3. Reading real back ends](d3-real-backends.md): *regalloc2's backtracking bundles*, *coalescing*
    - [E3. LLVM's allocator, scheduler and MC layer](e3-llvm-allocator-scheduler-mc.md): *greedy allocation*, *live-range splitting*

## Sources and further reading

This chapter's two allocation rules follow Chaitin's and Briggs, Cooper and Torczon's papers directly.[^chaitin82] [^briggs94] The chordal-coloring result and the maximum-cardinality-search algorithm follow Pereira and Palsberg, with Hack's thesis as the original proof.[^pereira05] [^hack07] All three example programs are original code, checked against those descriptions, not transcribed from them.

[^chaitin82]: Gregory J. Chaitin, "Register allocation & spilling via graph coloring", SIGPLAN '82. <https://doi.org/10.1145/800230.806984>
[^briggs94]: Preston Briggs, Keith D. Cooper and Linda Torczon, "Improvements to graph coloring register allocation", ACM TOPLAS 16(3), 1994. <https://doi.org/10.1145/177492.177575>
[^george96]: Lal George and Andrew W. Appel, "Iterated register coalescing", ACM TOPLAS 18(3), 1996. <https://doi.org/10.1145/229542.229546>
[^hack07]: Sebastian Hack, "Register Allocation for Programs in SSA Form", doctoral thesis, Karlsruhe Institute of Technology, 2007. <https://publikationen.bibliothek.kit.edu/1000007166>
[^pereira05]: Fernando M. Q. Pereira and Jens Palsberg, "Register Allocation via Coloring of Chordal Graphs", APLAS 2005. <https://doi.org/10.1007/11575467_21> (author copy: <http://web.cs.ucla.edu/~palsberg/paper/aplas05.pdf>)
[^leroy08]: Xavier Rideau, Bernard P. Serpette and Xavier Leroy, "Tilting at windmills with Coq: formal verification of a compilation algorithm for parallel moves", Journal of Automated Reasoning, 2008. <https://doi.org/10.1007/s10817-007-9096-8>
[^olesen11]: Jakob Stoklund Olesen, "Greedy Register Allocation in LLVM 3.0", LLVM Project Blog, 18 September 2011. <https://blog.llvm.org/2011/09/greedy-register-allocation-in-llvm-30.html>
[^fallin22]: Chris Fallin, "Cranelift, Part 4: A New Register Allocator", 9 June 2022. <https://cfallin.org/blog/2022/06/09/cranelift-regalloc2/>
[^regalloc2doc]: Bytecode Alliance, "regalloc2: how IonMonkey's register allocator was adapted for Cranelift" (ION.md design document). <https://github.com/bytecodealliance/regalloc2/blob/main/doc/ION.md>
[^cmu411]: CMU 15-411 course notes, lecture 3, "Register Allocation" (Pfenning and Platzer). <https://www.cs.cmu.edu/~fp/courses/15411-f13/>
[^stage10]: [Build v0.1, stage 10, "The program the milestone asks for"](../compiler/guide/stage-10-matrix-multiplication.md#the-program-the-milestone-asks-for); see also [C3's own reading of the same kernel](c3-linear-scan.md#applying-it-to-the-vortex-matmul-kernel).
[^numbers56]: [Numbers, decision 56](../decisions/numbers.md#d56): every `f32` and `f64` operation gives the IEEE 754 result, rounded to nearest with ties to even, with no contraction, reordering or wider format.

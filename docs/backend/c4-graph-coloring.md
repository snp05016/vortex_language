# C4. Register allocation II: graphs and SSA

<p class="page-intro">C3 allocated registers without ever building the interference graph. This chapter builds it: from liveness, by hand, then colors it with Chaitin's and Briggs's simplify-and-select method, removes copies by coalescing, and shows why a program in SSA form makes the whole problem easier. A graph-coloring allocator is the classic answer to register allocation; knowing why SSA helps it is what lets you choose an allocator for Vortex on purpose.</p>

<p class="vx-meta" markdown="1">Level: Advanced · Reading time: about 50 minutes · Builds on: [C2. Liveness](c2-liveness.md), [C3. Register allocation I: linear scan](c3-linear-scan.md), [O3. SSA form: construction and destruction](../optimize/o3-ssa.md)</p>

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

    - Build an interference graph from liveness with the definition rule, and explain why it can need fewer registers than the graph live intervals give.
    - Run Chaitin's simplify and select by hand, and show on a small graph how Briggs's optimistic change avoids a spill that Chaitin's rule takes.
    - Explain why the interference graph of a program in SSA form never needs more colors than the most values live at one point, and color it in one pass with maximum cardinality search.
    - Apply Briggs's and George's conservative coalescing tests, and find a merge that one test refuses and the other accepts.
    - Compare graph coloring with linear scan, and place LLVM's greedy allocator and Cranelift's regalloc2 between the two.

## Building the graph from liveness

[C3](c3-linear-scan.md#live-intervals-one-range-instead-of-a-graph) used this block to explain live intervals:

```text
1  a = 2
2  b = 3
3  c = a + b
4  d = a * b
5  e = c + d
6  return e
```

To build an **interference graph**, one node per value and one edge per pair of values that must not share a register, start from the facts [C2](c2-liveness.md#interference) computes: which values are live immediately after each instruction. Walk the block backwards. Nothing is live after the `return`. The return reads `e`, so `e` is live after instruction 5. Instruction 5 defines `e` and reads `c` and `d`, so after instruction 4 the live set is `c, d`. Continuing up the block gives:

| After instruction | Live values |
| --- | --- |
| 1 `a = 2` | `a` |
| 2 `b = 3` | `a`, `b` |
| 3 `c = a + b` | `a`, `b`, `c` |
| 4 `d = a * b` | `c`, `d` |
| 5 `e = c + d` | `e` |
| 6 `return e` | none |

Now apply the **definition rule**: at each instruction that defines a value `d`, add an edge between `d` and every other value live after that instruction.[^cmu411] Instruction 2 adds `b`-`a`. Instruction 3 adds `c`-`a` and `c`-`b`. Instruction 4 adds only `d`-`c`, because `a` and `b` are read by instruction 4 and never again: they are dead after it, so `d` may reuse either one's register. Instruction 5 adds nothing. The graph is a triangle `a`, `b`, `c`, with `d` hanging off `c` and `e` alone.

Why only at definitions? A value can be overwritten only where some other value is written. If two values are ever live at the same point, one of them was defined while the other was live, so checking every definition finds every conflict. The rule has one exception, for a plain copy `t = s`: it adds no edge between `t` and `s`, even if `s` stays live, because after the copy both hold the same bits and could share a register.[^cmu411] That exception is what makes coalescing possible later in this chapter.

--8<-- "includes/examples/backend/c4-graph-coloring/interference_build.cpp.md"

The program prints the same four edges, and then the graph that live intervals give. C3's overlap test, `a.start <= b.end && b.start <= a.end`, counts two intervals that share an endpoint as overlapping. Interval `[1,4]` for `a` and `[4,5]` for `d` share position 4, so the interval view joins `d` to `a` and `b`, and at position 5 it joins `e` to `c` and `d`. Those four extra edges make `a`, `b`, `c` and `d` all adjacent to one another. The definition rule knows that instruction 4 reads `a` and `b` before it writes `d`; the interval view does not.

Two words for what the table shows. The **register pressure** at a point is the number of values live there, and the largest register pressure in a function, sometimes called **Maxlive**, is a lower bound on the registers any allocation needs without going to memory.[^ssabook] Here Maxlive is 3.

A **clique** is a set of nodes that are all adjacent to one another. **Coloring** the graph means giving each node one of $k$ colors so that no edge joins two nodes of the same color, and a clique of $n$ nodes needs $n$ colors. The definition-rule graph's largest clique is the triangle, so three registers suffice. The interval graph contains a clique of four, so an allocator working from it needs a fourth register or a spill for the same six instructions.

<figure class="vx-figure">
<svg viewBox="0 0 760 300" role="img" aria-label="The interference graph of the six-instruction block, built two ways. Solid edges come from the definition rule: a-b, a-c, b-c and c-d. Dashed edges appear only when live intervals that share an endpoint count as overlapping: a-d, b-d, c-e and d-e. With the dashed edges, a, b, c and d form a clique of four.">
<text class="vx-text" x="20" y="28">Live after each instruction</text>
<text class="vx-mono" x="20" y="62">1  a = 2</text><text class="vx-mono" x="170" y="62">{a}</text>
<text class="vx-mono" x="20" y="90">2  b = 3</text><text class="vx-mono" x="170" y="90">{a, b}</text>
<text class="vx-mono" x="20" y="118">3  c = a + b</text><text class="vx-mono" x="170" y="118">{a, b, c}</text>
<text class="vx-mono" x="20" y="146">4  d = a * b</text><text class="vx-mono" x="170" y="146">{c, d}</text>
<text class="vx-mono" x="20" y="174">5  e = c + d</text><text class="vx-mono" x="170" y="174">{e}</text>
<text class="vx-mono" x="20" y="202">6  return e</text><text class="vx-mono" x="170" y="202">{}</text>
<text class="vx-text-muted" x="20" y="240">An edge joins each defined value</text>
<text class="vx-text-muted" x="20" y="258">to the others live after it.</text>
<text class="vx-text" x="400" y="28">Interference graph</text>
<line class="vx-line" x1="430" y1="80" x2="590" y2="80"/>
<line class="vx-line" x1="430" y1="80" x2="510" y2="170"/>
<line class="vx-line" x1="590" y1="80" x2="510" y2="170"/>
<line class="vx-line" x1="510" y1="170" x2="650" y2="200"/>
<line class="vx-line" x1="430" y1="80" x2="650" y2="200" stroke-dasharray="5 4"/>
<line class="vx-line" x1="590" y1="80" x2="650" y2="200" stroke-dasharray="5 4"/>
<line class="vx-line" x1="510" y1="170" x2="600" y2="260" stroke-dasharray="5 4"/>
<line class="vx-line" x1="650" y1="200" x2="600" y2="260" stroke-dasharray="5 4"/>
<circle class="vx-box" cx="430" cy="80" r="20"/><text class="vx-mono" x="430" y="85" text-anchor="middle">a</text>
<circle class="vx-box" cx="590" cy="80" r="20"/><text class="vx-mono" x="590" y="85" text-anchor="middle">b</text>
<circle class="vx-box" cx="510" cy="170" r="20"/><text class="vx-mono" x="510" y="175" text-anchor="middle">c</text>
<circle class="vx-box" cx="650" cy="200" r="20"/><text class="vx-mono" x="650" y="205" text-anchor="middle">d</text>
<circle class="vx-box" cx="600" cy="260" r="20"/><text class="vx-mono" x="600" y="265" text-anchor="middle">e</text>
<line class="vx-line" x1="400" y1="286" x2="430" y2="286"/><text class="vx-text-muted" x="438" y="290">definition rule</text>
<line class="vx-line" x1="560" y1="286" x2="590" y2="286" stroke-dasharray="5 4"/><text class="vx-text-muted" x="598" y="290">intervals only</text>
</svg>
<figcaption>Figure 1. The same block, two graphs. The solid edges come from the definition rule and need three colors. The dashed edges exist only in the interval view, where <code>d</code>'s interval starts at the position where <code>a</code>'s and <code>b</code>'s end. With them, <code>a</code>, <code>b</code>, <code>c</code> and <code>d</code> form a clique of four.</figcaption>
</figure>

The graph costs something the interval view does not. Checking overlap between two intervals is one comparison; the graph has to decide and store an edge for every interfering pair, and the number of pairs can grow with the square of the number of values. That is the price C3 avoided, and the one this chapter pays for a more precise picture.

## Coloring by simplify and select

Graph coloring register allocation begins with Chaitin and his colleagues in the early 1980s.[^chaitin82] Its coloring step rests on one observation, which George and Appel trace back to Kempe in 1879: if a node has fewer than $k$ neighbors, remove it; if the rest of the graph can be colored with $k$ colors, so can the whole, because the node's neighbors use at most $k - 1$ colors between them and leave one free.[^george96] A node with $k$ or more neighbors is said to have **significant degree**.

That gives a two-phase algorithm on a stack:

- **Simplify.** While some node has fewer than $k$ neighbors in the remaining graph, remove it and push it on a stack. Removing it lowers its neighbors' degrees, which may let them go next.
- **Select.** Pop the nodes one by one, putting each back into the graph, and give it any color its already-colored neighbors are not using. Simplify guaranteed one is free.

Run it on the definition-rule graph from Figure 1 with $k = 3$. The degrees are `a` 2, `b` 2, `c` 3, `d` 1, `e` 0. Push `e`, then `d`. Removing `d` drops `c` to degree 2, so all three of the triangle can go: push `a`, `b`, `c`. Now pop. `c` takes color 0. `b` sees `c` and takes 1. `a` sees 0 and 1 and takes 2. `d` sees only `c`, so it takes 1. `e` has no neighbors and takes 0. Three registers, no memory.

When simplify runs out of low-degree nodes while nodes remain, it is **stuck**: every remaining node has significant degree, and the algorithm cannot prove any of them colorable. Chaitin's allocator then picks a node to **spill**, to keep in memory instead of a register. A good choice has high degree, so removing it frees many neighbors, and low **spill cost**, the estimated number of loads and stores it would add, weighted by how often they run.[^ssabook]

The spilled node leaves the graph and simplify continues. Afterwards the allocator rewrites the program with a store after each definition and a load before each use of the spilled value. Those loads and stores create new, short live ranges, so the allocator rebuilds the graph and starts over. George and Appel report that one or two rounds almost always suffice.[^george96] Figure 2 shows the loop.

<figure class="vx-figure">
<svg viewBox="0 0 760 210" role="img" aria-label="The phases of a Chaitin-style allocator as a loop. Build the interference graph from liveness, coalesce copies, simplify by pushing low-degree nodes, choose a potential spill when stuck, then select colors by popping the stack. If select finds an actual spill, rewrite the program with loads and stores and return to build. Otherwise the allocation is done.">
<defs><marker id="c4-f2-arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path class="vx-arrowhead" d="M0,0 L10,5 L0,10 z"/></marker></defs>
<rect class="vx-box" x="20" y="40" width="100" height="44" rx="6"/><text class="vx-text" x="70" y="67" text-anchor="middle">build</text>
<rect class="vx-box" x="160" y="40" width="100" height="44" rx="6"/><text class="vx-text" x="210" y="67" text-anchor="middle">coalesce</text>
<rect class="vx-box" x="300" y="40" width="100" height="44" rx="6"/><text class="vx-text" x="350" y="67" text-anchor="middle">simplify</text>
<rect class="vx-box-accent" x="440" y="40" width="130" height="44" rx="6"/><text class="vx-text" x="505" y="60" text-anchor="middle">potential</text><text class="vx-text" x="505" y="76" text-anchor="middle">spill</text>
<rect class="vx-box" x="610" y="40" width="100" height="44" rx="6"/><text class="vx-text" x="660" y="67" text-anchor="middle">select</text>
<rect class="vx-box-bad" x="440" y="140" width="130" height="44" rx="6"/><text class="vx-text" x="505" y="160" text-anchor="middle">rewrite with</text><text class="vx-text" x="505" y="176" text-anchor="middle">loads, stores</text>
<text class="vx-text-accent" x="690" y="170">done</text>
<line class="vx-line" x1="120" y1="62" x2="158" y2="62" marker-end="url(#c4-f2-arrow)"/>
<line class="vx-line" x1="260" y1="62" x2="298" y2="62" marker-end="url(#c4-f2-arrow)"/>
<line class="vx-line" x1="400" y1="62" x2="438" y2="62" marker-end="url(#c4-f2-arrow)"/>
<line class="vx-line" x1="570" y1="62" x2="608" y2="62" marker-end="url(#c4-f2-arrow)"/>
<path class="vx-line" d="M 470 40 C 460 22, 380 22, 370 38" marker-end="url(#c4-f2-arrow)"/>
<text class="vx-text-muted" x="365" y="18">still stuck? repeat</text>
<line class="vx-line" x1="660" y1="84" x2="690" y2="152" marker-end="url(#c4-f2-arrow)"/>
<path class="vx-line" d="M 640 84 C 630 150, 600 162, 572 162" marker-end="url(#c4-f2-arrow)"/>
<text class="vx-text-muted" x="536" y="114">actual spill</text>
<path class="vx-line" d="M 440 162 C 200 162, 70 150, 70 86" marker-end="url(#c4-f2-arrow)"/>
<text class="vx-text-muted" x="160" y="190">rebuild the graph and try again</text>
</svg>
<figcaption>Figure 2. The phases of a Chaitin-style allocator, following George and Appel's description. With Chaitin's own rule, a stuck node is spilled at once; with Briggs's rule it becomes a potential spill, pushed like any other node, and only select decides whether it is an actual spill. Any actual spill sends the rewritten program round the loop again.</figcaption>
</figure>

Briggs, Cooper and Torczon changed one decision.[^briggs94] When simplify is stuck, do not spill the chosen node yet. Push it on the stack like any other node and mark it a **potential spill**. At select time its neighbors already have colors, and if they happen to use fewer than $k$ distinct colors between them, the node gets the one left over. Only a potential spill that finds no free color becomes an **actual spill**. The name for this is **optimistic coloring**.[^george96]

Here is a graph where the difference shows, with $k = 3$: a node `s` interferes with `p`, `q`, `r` and `t`, and those four interfere with each other only around a ring, `p`-`q`-`r`-`t`-`p`. Every node has degree 3 or more, so simplify is stuck before it starts. All nodes cost the same to spill, so both rules give up on `s`, the node with the highest degree.

--8<-- "includes/examples/backend/c4-graph-coloring/simplify_select.cpp.md"

With `s` gone, the ring simplifies node by node, and select colors it with two alternating colors. Chaitin's rule has already spilled `s`. Briggs's rule pops `s` last, finds its four neighbors using only colors 0 and 1, and gives it color 2. The graph was 3-colorable all along; only the degree test could not see it.

<figure class="vx-figure">
<svg viewBox="0 0 760 330" role="img" aria-label="Two copies of the same graph: a hub node s joined to four nodes p, q, r and t, which form a ring p-q-r-t. Left, Chaitin's rule: s is spilled and drawn dashed; p and r have color 1, q and t have color 0. Right, Briggs's rule: the ring has the same colors, and s gets color 2, so nothing is spilled.">
<text class="vx-text" x="40" y="26">Chaitin: spill when stuck</text>
<line class="vx-line" x1="100" y1="70" x2="280" y2="70"/>
<line class="vx-line" x1="280" y1="70" x2="280" y2="250"/>
<line class="vx-line" x1="280" y1="250" x2="100" y2="250"/>
<line class="vx-line" x1="100" y1="250" x2="100" y2="70"/>
<line class="vx-line" x1="190" y1="160" x2="100" y2="70"/>
<line class="vx-line" x1="190" y1="160" x2="280" y2="70"/>
<line class="vx-line" x1="190" y1="160" x2="280" y2="250"/>
<line class="vx-line" x1="190" y1="160" x2="100" y2="250"/>
<circle class="vx-box-strong" cx="100" cy="70" r="24"/><text class="vx-mono" x="100" y="75" text-anchor="middle">p 1</text>
<circle class="vx-box" cx="280" cy="70" r="24"/><text class="vx-mono" x="280" y="75" text-anchor="middle">q 0</text>
<circle class="vx-box-strong" cx="280" cy="250" r="24"/><text class="vx-mono" x="280" y="255" text-anchor="middle">r 1</text>
<circle class="vx-box" cx="100" cy="250" r="24"/><text class="vx-mono" x="100" y="255" text-anchor="middle">t 0</text>
<circle class="vx-box-bad" cx="190" cy="160" r="24"/><text class="vx-mono" x="190" y="165" text-anchor="middle">s</text>
<text class="vx-text-muted" x="190" y="310" text-anchor="middle">s spilled: loads and stores</text>
<text class="vx-text" x="460" y="26">Briggs: push when stuck</text>
<line class="vx-line" x1="480" y1="70" x2="660" y2="70"/>
<line class="vx-line" x1="660" y1="70" x2="660" y2="250"/>
<line class="vx-line" x1="660" y1="250" x2="480" y2="250"/>
<line class="vx-line" x1="480" y1="250" x2="480" y2="70"/>
<line class="vx-line" x1="570" y1="160" x2="480" y2="70"/>
<line class="vx-line" x1="570" y1="160" x2="660" y2="70"/>
<line class="vx-line" x1="570" y1="160" x2="660" y2="250"/>
<line class="vx-line" x1="570" y1="160" x2="480" y2="250"/>
<circle class="vx-box-strong" cx="480" cy="70" r="24"/><text class="vx-mono" x="480" y="75" text-anchor="middle">p 1</text>
<circle class="vx-box" cx="660" cy="70" r="24"/><text class="vx-mono" x="660" y="75" text-anchor="middle">q 0</text>
<circle class="vx-box-strong" cx="660" cy="250" r="24"/><text class="vx-mono" x="660" y="255" text-anchor="middle">r 1</text>
<circle class="vx-box" cx="480" cy="250" r="24"/><text class="vx-mono" x="480" y="255" text-anchor="middle">t 0</text>
<circle class="vx-box-accent" cx="570" cy="160" r="24"/><text class="vx-mono" x="570" y="165" text-anchor="middle">s 2</text>
<text class="vx-text-muted" x="570" y="310" text-anchor="middle">s colored last: no spill</text>
</svg>
<figcaption>Figure 3. The same graph under both rules; the number beside each name is its color. The ring colors the same way either time. What differs is <code>s</code>: its four neighbors use only two colors, which Briggs's rule finds out at select time and Chaitin's rule never asks.</figcaption>
</figure>

??? check "Within one round of simplify and select, can Briggs's rule spill a node that Chaitin's rule keeps in a register?"

    No. Both rules remove the same node from the graph when simplify is stuck, so they push every other node in the same order. Chaitin's rule spills every potential spill; Briggs's rule spills only the potential spills that find no free color at select time. So its actual spills are a subset of Chaitin's. This says nothing about later rounds, which run on differently rewritten programs.

## Why no shortcut works on every graph

Simplify and select is a heuristic. Deciding whether a general graph can be colored with $k$ colors is NP-complete for $k \geq 3$, and Chaitin showed that every graph is the interference graph of some program, so register allocation in general inherits that hardness.[^cmu411] No known algorithm colors every graph optimally in time that stays reasonable as graphs grow. Simplify and select never produces a wrong coloring, but it can give up on a graph that some $k$-coloring exists for, as Chaitin's rule did on Figure 3's graph. Briggs's rule gives up less often, not never.

The hardness comes from graphs with arbitrary shapes. A compiler does not build arbitrary graphs: it builds them from programs, and a program in SSA form produces graphs of one particular shape.

## SSA graphs color with Maxlive registers

Here is a function in ordinary, non-SSA form, where `x` and `y` are each assigned in both arms of a branch:

```text
B0:  s = load            B1:  x = s * 2           B3:  return s + x + y
     a = load                 y = b + 1
     b = load                 goto B3
     if a < b goto B1
     else goto B2        B2:  x = a - b
                              y = a + 1
                              goto B3
```

Build its graph with the definition rule. `s` is live everywhere, so it interferes with every other value. In B1, `x` is defined while `b` is still live (the next instruction reads it), so `x`-`b`. In B2, `x` is defined while `a` is still live, so `x`-`a`. B0 gives `a`-`b`. So `s`, `a`, `b` and `x` form a clique of four.

Yet no point in the function has more than three values live: `{s, a, b}` at the branch, `{s, b, x}` or `{s, a, x}` inside an arm, `{s, x, y}` at the join. Maxlive is 3, and the program still needs four registers, because `x` conflicts with `b` in one arm and with `a` in the other, and a single register for `x` must avoid both.[^ssabook]

In SSA form, each arm defines its own version: `x1` and `y1` in B1, `x2` and `y2` in B2, and phis at the top of B3 join them into `x3` and `y3`. Now `x1` interferes with `b` only, and `x2` with `a` only. The clique is gone, and three registers suffice. Figure 4 shows both graphs.

<figure class="vx-figure">
<svg viewBox="0 0 760 300" role="img" aria-label="Two interference graphs for the branch example, with s left out because it interferes with everything. Left, non-SSA: a, b and x form a triangle and y hangs off x; with s this is a clique of four. Right, SSA: the values form a path y1-x1-b-a-x2-y2 plus a separate edge x3-y3; with s this needs only three colors.">
<text class="vx-text" x="30" y="26">Before SSA (s not drawn)</text>
<line class="vx-line" x1="80" y1="90" x2="240" y2="90"/>
<line class="vx-line" x1="80" y1="90" x2="160" y2="190"/>
<line class="vx-line" x1="240" y1="90" x2="160" y2="190"/>
<line class="vx-line" x1="160" y1="190" x2="160" y2="260"/>
<circle class="vx-box-strong" cx="80" cy="90" r="22"/><text class="vx-mono" x="80" y="95" text-anchor="middle">a</text>
<circle class="vx-box-strong" cx="240" cy="90" r="22"/><text class="vx-mono" x="240" y="95" text-anchor="middle">b</text>
<circle class="vx-box-strong" cx="160" cy="190" r="22"/><text class="vx-mono" x="160" y="195" text-anchor="middle">x</text>
<circle class="vx-box" cx="160" cy="260" r="22"/><text class="vx-mono" x="160" y="265" text-anchor="middle">y</text>
<text class="vx-text-muted" x="260" y="200">x-a from B2,</text>
<text class="vx-text-muted" x="260" y="218">x-b from B1</text>
<text class="vx-text" x="400" y="26">In SSA form (s not drawn)</text>
<line class="vx-line" x1="420" y1="110" x2="480" y2="110"/>
<line class="vx-line" x1="480" y1="110" x2="540" y2="110"/>
<line class="vx-line" x1="540" y1="110" x2="600" y2="110"/>
<line class="vx-line" x1="600" y1="110" x2="660" y2="110"/>
<line class="vx-line" x1="660" y1="110" x2="720" y2="110"/>
<line class="vx-line" x1="540" y1="220" x2="620" y2="220"/>
<circle class="vx-box" cx="420" cy="110" r="22"/><text class="vx-mono" x="420" y="115" text-anchor="middle">y1</text>
<circle class="vx-box" cx="480" cy="110" r="22"/><text class="vx-mono" x="480" y="115" text-anchor="middle">x1</text>
<circle class="vx-box" cx="540" cy="110" r="22"/><text class="vx-mono" x="540" y="115" text-anchor="middle">b</text>
<circle class="vx-box" cx="600" cy="110" r="22"/><text class="vx-mono" x="600" y="115" text-anchor="middle">a</text>
<circle class="vx-box" cx="660" cy="110" r="22"/><text class="vx-mono" x="660" y="115" text-anchor="middle">x2</text>
<circle class="vx-box" cx="720" cy="110" r="22"/><text class="vx-mono" x="720" y="115" text-anchor="middle">y2</text>
<circle class="vx-box" cx="540" cy="220" r="22"/><text class="vx-mono" x="540" y="225" text-anchor="middle">x3</text>
<circle class="vx-box" cx="620" cy="220" r="22"/><text class="vx-mono" x="620" y="225" text-anchor="middle">y3</text>
<text class="vx-text-muted" x="420" y="170">B1's values</text>
<text class="vx-text-muted" x="640" y="170">B2's values</text>
<text class="vx-text-muted" x="420" y="280">phi results at B3</text>
</svg>
<figcaption>Figure 4. The branch example's graph before and after SSA construction, leaving out <code>s</code>, which is adjacent to every node in both. Before, <code>a</code>, <code>b</code> and <code>x</code> form a triangle, so with <code>s</code> the graph needs four colors although at most three values are ever live together. After, what remains is a path and one edge, which two colors cover, three with <code>s</code>.</figcaption>
</figure>

This is not luck. In SSA form, a value's single definition dominates all of its uses, so its live range is a connected piece of the dominator tree hanging down from the definition, what the SSA book calls a tree-shaped live range.[^ssabook]

Graph theory has known since Gavril in 1974 that graphs formed by overlapping subtrees of one tree are exactly the **chordal graphs**: graphs in which every cycle of four or more nodes has a **chord**, an edge joining two nodes of the cycle that are not next to each other on it.[^ssabook] By 2005, several research groups had noticed that this makes SSA interference graphs chordal; Hack proved it for strict SSA programs (those where every use is dominated by a definition), and Brisk and colleagues proved the related result that they are perfect.[^pereira05] [^hack07]

Chordal graphs have two properties an allocator can use. First, the size of the largest clique is the number of colors needed, and for an SSA program the largest clique is the set of values live at one program point, so the colors needed equal Maxlive.[^ssabook] Second, they can be colored optimally in time linear in the number of nodes and edges.[^pereira05]

The first property changes how spilling works. In a general graph, the allocator cannot know whether it has spilled enough until coloring succeeds, which is why Chaitin's allocator loops. For an SSA program, the test is exact: spill until no point has more than $k$ values live, and coloring is then guaranteed to succeed without another spill. Spilling and coloring become two separate phases.[^ssabook] [C5](c5-spilling.md#deciding-spills-before-assigning-registers) builds the spilling half. The SSA book adds that even plain simplify and select never gets stuck on an SSA program whose Maxlive is at most $k$: some value defined last on a branch of the dominator tree always has fewer than $k$ neighbors.[^ssabook]

??? check "In the SSA version of the branch example, why can `x1` and `a` share a register when `x` and `a` could not?"

    `x1` exists only in B1, and `a` is not live anywhere in B1: nothing on the path from B1 reads it. The old conflict between `x` and `a` came from B2, where the other assignment to `x` happened while `a` was still live. SSA gives that assignment its own name, `x2`, so the conflict moves to `x2` and leaves `x1` free.

## Coloring a chordal graph in one pass

For the second property, the coloring order matters. A node is **simplicial** when its neighbors form a clique. A **simplicial elimination ordering** lists the nodes so that each node, among the nodes listed before it, has neighbors that form a clique. Coloring greedily in such an order, giving each node the lowest color its earlier neighbors do not use, is optimal: when a node is reached, its colored neighbors form a clique, and that clique plus the node is itself a clique, so the node never needs a color beyond the size of the largest clique.[^pereira05] [^cmu411] A graph has such an ordering exactly when it is chordal.[^pereira05]

**Maximum cardinality search** (MCS) finds the ordering. Give every node a weight of 0. Repeatedly pick an unvisited node of largest weight, append it to the order, and add 1 to the weight of each of its unvisited neighbors. On a chordal graph the result is a simplicial elimination ordering, found in time linear in the size of the graph.[^pereira05]

Pereira and Palsberg's allocator colors in exactly this order, and they observed that most of the graphs it meets are chordal even without SSA: 95 percent of the methods in the Java 1.5 library, compiled with the JoeQ compiler, had chordal interference graphs.[^pereira05] On a graph that is not chordal, MCS still returns some order, and greedy coloring in it is still correct, only possibly not optimal.[^cmu411]

Try it on a small chordal graph: `e` adjacent to everything, and `a`, `b`, `c`, `d` a path. The largest cliques are triangles such as `e`, `a`, `b`, so three colors are the minimum.

--8<-- "includes/examples/backend/c4-graph-coloring/chordal_coloring.cpp.md"

MCS starts at `a` (all weights are 0 and `a` is first), which makes `b` and `e` heavier; it takes `b`, then `e`, whose weight is now 2, then `c` and `d`. Greedy coloring in that order uses three colors. The arbitrary order `a`, `d`, `b`, `c`, `e` gives `a` and `d` the same color, which forces `b` and `c` onto two others, and `e`, adjacent to all four, needs a fourth. Nothing about the graph changed; only the order did.

Work the next one yourself. Take Figure 4's SSA graph and add `s` joined to every node. Run MCS starting from `s`. Which node must come second, and how many colors does greedy coloring use in the order you get?

??? note "Answer"

    Any node may come second: after `s` is visited, every other node has weight 1. The graph with `s` added is still chordal, so every MCS order is a simplicial elimination ordering and greedy coloring uses three colors, `s` plus two for the path and the separate edge.

An SSA allocator does not even need the graph. Visiting the dominator tree from the root down and coloring each value at its definition is also a simplicial elimination ordering, because the values already colored and still live at a definition are all live at that point, and so form a clique. The SSA book calls this a **tree scan**: C3's linear scan, generalized to live ranges that branch downwards but never join.[^ssabook] [^cmu411]

## Coalescing: deleting copies

Copies reach the allocator from many places: phis turned into moves when SSA form is destroyed, values moved into the registers the calling convention demands, copies an earlier pass left behind. Two values joined by a copy and not by an interference edge are **move-related**. If they get the same color, the copy moves a register into itself and can be deleted. **Coalescing** forces that outcome by merging the two nodes into one before coloring. The merged node has the union of both nodes' edges.[^george96]

Merging every such pair is **aggressive** coalescing (George and Appel call it reckless). It deletes the most copies, but the merged node is more constrained than either part, so a graph that $k$ colors could color can become one they cannot.[^george96] **Conservative** coalescing merges only when a test guarantees the graph stays colorable by simplify and select. There are two classic tests:[^george96] [^ssabook]

- **Briggs's test.** Merge `a` and `b` if the merged node would have fewer than $k$ neighbors of significant degree. Why it is safe: simplify removes every insignificant neighbor, leaving the merged node with fewer than $k$ neighbors, so it simplifies too.
- **George's test.** Merge `a` into `b` if every neighbor of `a` either already interferes with `b` or has insignificant degree. After the insignificant neighbors are simplified, the merged node has no neighbor that `b` did not already have, so the graph is no harder than before.

Both tests are conservative: they never approve a harmful merge, but they sometimes refuse a harmless one. George and Appel first used the second test to merge a value into a machine register, whose own neighbor list they did not keep.[^george96] The example runs both tests on three graphs with $k = 3$ and checks each verdict by brute force.

--8<-- "includes/examples/backend/c4-graph-coloring/coalescing.cpp.md"

In graph 1, the merged node's two neighbors each end with degree 2, so both tests merge, and the result is a triangle. In graph 2, merging `a` and `b` creates a clique of four; both tests refuse, and brute force confirms that three colors cannot color the merged graph. Graph 3 is the interesting one. The merged node `uv` has three neighbors, `x`, `y` and `z`, each of degree 3, so Briggs's test refuses. But `v`'s only neighbor, `x`, already interferes with `u`, so George's test merges `v` into `u`. The merged graph splits into two sides, `uv`, `m`, `n` against `x`, `y`, `z`, with edges only between sides: two colors color it.

??? check "Why does Briggs's test refuse graph 3's merge, and what does it fail to see?"

    It counts neighbors, not colors. The merged node has three neighbors of degree 3, and with $k = 3$ that could in general leave no color free. What the test does not look at is that `x`, `y` and `z` never interfere with one another, so they can all take the same color, leaving two colors for the merged node.

A test applied once, before any simplification, sees high degrees everywhere. George and Appel's **iterated register coalescing** interleaves the phases instead: simplify only nodes that are not move-related, then try conservative coalescing on the smaller graph, where degrees have dropped, and repeat. When neither step applies, **freeze** one low-degree move-related node, giving up on its copies so it can be simplified, and continue.[^george96] Briggs also used **biased coloring**: at select time, give a node the color of a move-related partner if that color is free, which removes some copies the tests refused.[^george96] Pereira and Palsberg's chordal allocator does its coalescing after coloring instead, merging a copy's two sides whenever some color is free in both neighborhoods.[^cmu411]

SSA form does not solve coalescing. Phis become copies when SSA form is destroyed, and choosing which copies to coalesce so as to remove the most is NP-complete, as is choosing the cheapest set of spills, even though coloring itself became polynomial.[^cmu411] Merging SSA values can also undo the property that made coloring polynomial, so the SSA book's allocator merges only when a Briggs or George test shows that simplify and select will still succeed.[^ssabook]

## Fixed registers and register classes

Some values must be in particular registers: arguments and results at a call, as [A4](a4-calling-conventions.md#caller-saved-and-callee-saved-registers) describes, or operands of instructions that name fixed registers, like x86-64 division in [B2](b2-x86-64.md#division-fixed-registers-and-a-trap). An allocator represents each machine register as a **precolored** node: its color is fixed, it can never be spilled, and all precolored nodes interfere with one another.[^george96] A value that interferes with $k$ precolored nodes of different colors cannot be given any register at all, which is one reason aggressive coalescing with precolored nodes is dangerous.[^george96] The MCS allocator handles them by treating the precolored nodes as already visited, so each ordinary node starts with a weight equal to its number of precolored neighbors.[^cmu411]

A call fits the same definition rule. The call overwrites every caller-saved register, so treat it as an instruction that defines all of them. Then every value live across the call gets an edge to every caller-saved register, and only callee-saved registers remain for it. That is how the difference in the remember box shows up in an allocation: a floating-point value that lives across a call has callee-saved candidates on AAPCS64 (the low halves of `v8`-`v15`) and none on SysV AMD64, where it must be saved to memory around the call.

Values also differ in which registers can hold them at all. An `f32` goes in a floating-point register, an address in a general-purpose one. Each value belongs to a **register class**, the set of physical registers it may use, and LLVM's allocators, for instance, assign each live range a register from its own class.[^olesen11] Values of different classes never compete for the same register, so an allocator can color each class's graph on its own.

## Moves that still have to run

Coalescing and biased coloring remove many copies, not all of them. Where the two sides of a phi end up in different registers, the move has to be emitted on the incoming edge. Several phis at the top of one block turn into a **parallel copy** on each incoming edge, and when those copies read one another's destinations they must be ordered with care, which is the problem [O3](../optimize/o3-ssa.md#sequentializing-a-parallel-copy) solved: emit copies whose destinations nobody still reads, then break each remaining cycle with one spare register.[^leroy08] Nothing in this chapter changes that algorithm. The allocator decides how many copies reach it, and an SSA allocator that leaves phi-related values in different registers leaves more of them at block boundaries.

## Graph coloring, linear scan and what compilers ship

The two approaches trade differently. Linear scan decides with one sorted sweep and never builds a graph, but its intervals over-approximate liveness, as Figure 1's dashed edges showed. A graph allocator sees interference exactly, at the cost of building and storing the graph. The SSA book sums up practice this way: linear scan is faster, and graph coloring usually produces better code.[^ssabook] SSA-based allocation narrows the gap from both sides: the tree scan is a linear sweep over the dominator tree that colors with Maxlive registers, and its quality depends on the spilling done before it and the coalescing done during or after it.

Production compilers mostly use neither in its textbook form. LLVM replaced its linear scan allocator with a **greedy** allocator for LLVM 3.0. It keeps live ranges rather than a graph and checks interference against the ranges already assigned to each physical register. It allocates the largest live ranges first, lets a range with a higher spill weight **evict** a lower one from its register, and splits ranges that cannot be assigned into smaller pieces that might be.[^olesen11] [E3](e3-llvm-allocator-scheduler-mc.md#the-greedy-allocator) reads it in detail.

Cranelift's **regalloc2** started as a port of IonMonkey's allocator and became a **backtracking** allocator: it merges live ranges into **bundles**, a coalescing step in all but name, then assigns bundles from a priority queue, undoing earlier assignments and splitting bundles when that helps.[^fallin22] [^regalloc2doc] Its developers check every allocation with a separate symbolic checker, driven by fuzzing, that confirms the allocated program moves values the same way the original did.[^fallin22] [D3](d3-real-backends.md#how-cranelift-knows-its-allocator-is-right) shows where to start reading it.

Both borrow this chapter's ideas (spill weights, coalescing, interference) while keeping compile time close to linear scan's. For teaching, CMU's compiler course skips Appel's textbook algorithms, which its notes call "complicated and difficult to implement", and teaches the chordal allocator of this chapter instead.[^cmu411]

## Applying it to the Vortex matmul kernel

Take the stage 10 kernel that C3 and O3 already used:[^stage10]

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

Sort its values by register class first. The general-purpose class holds the three array addresses `a`, `b` and `c`, and the counters `row`, `column` and `k`. All six are live inside the innermost loop, so they form a clique of six, plus whatever short-lived address arithmetic the instruction selector adds for `a[row, k]` and `b[k, column]`. The loop bounds are constants folded into compare instructions, as C3 pointed out, so they take no register.

AArch64 has 31 general-purpose registers, a few of them reserved by the platform ([A2](a2-aarch64-assembly.md#registers-by-name)), and x86-64 has sixteen, one of them the stack pointer ([B2](b2-x86-64.md#registers-by-another-name)), so six long-lived values plus a few temporaries fit on both. The floating-point class holds only `sum`, the two values loaded from `a` and `b`, and their product, a handful of values that fit on either machine as well. Neither class needs a spill in this kernel. A clique is also the simplest graph to color: any order uses exactly as many colors as the clique has nodes.

The allocator's real work here is copies. In SSA form, [O3's version of the `k` loop](../optimize/o3-ssa.md#your-turn-the-kernel-in-ssa-form) has `k1 = phi(P: k0, S: k2)` and `sum1 = phi(P: sum0, S: sum2)` at the loop header, with `k2 = k1 + 1` in the step block. Destroying SSA puts copies `k1 = k2` and `sum1 = sum2` at the end of the step block, which runs on every iteration. Whether coalescing removes them depends on interference: `k1` and `k2` can share a register only if `k1` is dead where `k2` is defined.

The loop also has an error path for a failed bounds check. If that path reports the error and never comes back into the loop, nothing is live across the call it makes, so the call adds no interference to the loop's values and does not push them into callee-saved registers.

??? check "In O3's SSA form of the `k` loop, the step block computes `k2 = k1 + 1`. Can the copy `k1 = k2` be coalesced? What if a scheduler moved the increment to the top of the loop body, before the loads?"

    As written, yes: the body's uses of `k1` all come before the step block, and after `k2 = k1 + 1` nothing reads `k1` again, so at `k2`'s definition `k1` is not live and the definition rule adds no edge between them. They can share one register and the copy disappears. If the increment moves above the loads, `k1` is still read by the address computations after `k2` exists, so the two interfere, they need different registers, and a move stays on the back edge. The order instructions come in changes the graph, which is why scheduling and allocation affect each other ([C6](c6-scheduling.md)).

## For Vortex

!!! vortex "Exercise"

    **Build.** A second register allocator for your compiler's back end, next to C3's linear scan, working on one function at a time and one register class at a time:

    - Build the interference graph from [C2](c2-liveness.md)'s liveness with the definition rule, including the copy exception, and with precolored nodes for the registers your calling convention and your instructions fix. Model each call as defining every caller-saved register.
    - Color it with simplify and select using Briggs's optimistic rule. When a potential spill becomes an actual spill, spill it everywhere (a store after its definition, a load before each use) and rebuild.
    - Add conservative coalescing with Briggs's and George's tests, applied once before simplify, to the copies your SSA destruction and ABI lowering create.
    - For functions still in SSA form, add a second coloring path: check that Maxlive is at most $k$, then color in maximum cardinality search order or by a tree scan of the dominator tree.

    **Do not build yet.** The freeze phase of iterated register coalescing; read George and Appel once both tests work on their own. Live-range splitting, rematerialization and spill placement smarter than spill-everywhere, and SSA-based spilling before coloring: all of that is [C5](c5-spilling.md). Any coupling with instruction scheduling, which is [C6](c6-scheduling.md).

    **The test that proves it works.**

    - An independent checker, the same idea as C3's but fed exact live sets from C2 instead of intervals: for every program point, no two values live there share a register, and every precolored value sits in its fixed register. Run it on every allocation either allocator produces, including the fuzzed functions from C3's exercise.
    - A unit test on this page's six-instruction block: four interference edges, Maxlive 3, three registers used, no spill. Another on the branch example from Figure 4: four colors before SSA construction, three after.
    - On SSA inputs, assert that the chordal path uses exactly Maxlive colors per register class, never more.
    - For coalescing, assert that a function that colored without spills before coalescing still does after it, and count the copies removed.

    Then compare the two allocators on the matmul kernel and on the fuzzed functions, and fill in a table like this one from your own runs:

    | Input | Allocator | Values spilled | Copies left | Compile time |
    | --- | --- | --- | --- | --- |
    | matmul kernel | linear scan (C3) | | | |
    | matmul kernel | graph coloring | | | |
    | fuzzed functions (total) | linear scan (C3) | | | |
    | fuzzed functions (total) | graph coloring | | | |

    Expect no spills on the kernel from either allocator; the difference to look for is copies. Whichever allocator you keep should be the one your own numbers favor.

## Key ideas

!!! recap

    - **How is an interference graph built from liveness?** At each definition, add an edge from the defined value to every other value live after the instruction, except the source of a plain copy.
    - **When does simplify and select get stuck, and what does Briggs's rule do then?** When every remaining node has $k$ or more neighbors; Briggs pushes a chosen node as a potential spill and spills it only if select finds no free color.
    - **Why is register allocation hard in general?** $k$-coloring is NP-complete for $k \geq 3$, and every graph is the interference graph of some program.
    - **What does SSA form guarantee about the interference graph?** It is chordal, and its largest clique is the set of values live at one point, so Maxlive registers are enough and spilling can be decided before coloring.
    - **How do you color a chordal graph optimally?** Order the nodes by maximum cardinality search (or walk the dominator tree) and color greedily in that order.
    - **What do Briggs's and George's coalescing tests check?** Briggs: the merged node has fewer than $k$ significant-degree neighbors. George: every neighbor of one node already interferes with the other or has insignificant degree.
    - **What do LLVM and Cranelift ship instead of textbook graph coloring?** LLVM's greedy allocator (priority by size, eviction, splitting over live ranges) and regalloc2 (backtracking over bundles, with a symbolic checker).

## Where this comes back

!!! next "You will use this again in"

    - [C5. Spilling, splitting and rematerialization](c5-spilling.md): *spill cost*, *spilling until Maxlive fits before coloring*
    - [C6. Instruction scheduling](c6-scheduling.md): *register pressure*, *instruction order changes interference*
    - [D3. Reading real back ends](d3-real-backends.md): *regalloc2's bundles and backtracking*, *coalescing*
    - [E3. LLVM's allocator, scheduler and MC layer](e3-llvm-allocator-scheduler-mc.md): *greedy allocation*, *eviction*, *live-range splitting*

## Sources and further reading

George and Appel's paper opens with the clearest short description of Chaitin's and Briggs's allocators, and the SSA book's register allocation chapter is the best single account of the SSA side. The CMU lecture notes are a compact route to building the chordal allocator.

[^chaitin82]: Gregory J. Chaitin, "Register allocation & spilling via graph coloring", SIGPLAN Symposium on Compiler Construction, 1982. <https://doi.org/10.1145/800230.806984>
[^briggs94]: Preston Briggs, Keith D. Cooper and Linda Torczon, "Improvements to graph coloring register allocation", ACM TOPLAS 16(3), 1994. <https://doi.org/10.1145/177492.177575>
[^george96]: Lal George and Andrew W. Appel, "Iterated register coalescing", ACM TOPLAS 18(3), 1996, sections 2, 3, 5 and 5.1. <https://doi.org/10.1145/229542.229546>
[^hack07]: Sebastian Hack, "Register Allocation for Programs in SSA Form", doctoral thesis, Universität Karlsruhe, 2007. <https://publikationen.bibliothek.kit.edu/1000007166>
[^pereira05]: Fernando Magno Quintão Pereira and Jens Palsberg, "Register Allocation via Coloring of Chordal Graphs", APLAS 2005, sections 1 to 3. <https://doi.org/10.1007/11575467_21> (author copy: <http://web.cs.ucla.edu/~palsberg/paper/aplas05.pdf>)
[^ssabook]: Florent Bouchez Tichadou, Sebastian Hack and Fabrice Rastello, "Register Allocation", chapter 22 of Fabrice Rastello and Florent Bouchez Tichadou (eds.), *SSA-based Compiler Design*, Springer, 2022; sections 22.1 to 22.4 of the free draft. <https://doi.org/10.1007/978-3-030-80515-9> (draft: <https://pfalcon.github.io/ssabook/latest/>)
[^cmu411]: Frank Pfenning and André Platzer, "Lecture Notes on Register Allocation", 15-411 Compiler Design, lecture 3, Carnegie Mellon University, 2013. <https://www.cs.cmu.edu/~fp/courses/15411-f13/lectures/03-regalloc.pdf>
[^leroy08]: Laurence Rideau, Bernard P. Serpette and Xavier Leroy, "Tilting at windmills with Coq: formal verification of a compilation algorithm for parallel moves", Journal of Automated Reasoning, 2008. <https://doi.org/10.1007/s10817-007-9096-8>
[^olesen11]: Jakob Stoklund Olesen, "Greedy Register Allocation in LLVM 3.0", LLVM Project Blog, 18 September 2011. <https://blog.llvm.org/2011/09/greedy-register-allocation-in-llvm-30.html>
[^fallin22]: Chris Fallin, "Cranelift, Part 4: A New Register Allocator", 9 June 2022. <https://cfallin.org/blog/2022/06/09/cranelift-regalloc2/>
[^regalloc2doc]: Bytecode Alliance, "Ion Design Overview", regalloc2 design document `doc/ION.md`. <https://github.com/bytecodealliance/regalloc2/blob/main/doc/ION.md>
[^stage10]: [Build v0.1, stage 10, "The program the milestone asks for"](../compiler/guide/stage-10-matrix-multiplication.md#the-program-the-milestone-asks-for).

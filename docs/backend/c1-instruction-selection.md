# C1. Instruction selection

<p class="page-intro">Macro expansion gives every tree node one fixed instruction and never looks at its neighbors. This chapter climbs Hjort Blindell's next two rungs, tree covering and optimal tiling, and stops at the rung after that: DAGs, where sharing breaks a tree-based selector, and the SSA value graph that production compilers select on instead.</p>

<p class="vx-meta" markdown="1">Level: Intermediate · Reading time: about 30 minutes · Builds on: [B1. The simplest back end that works](b1-simplest-backend.md), [B2. A second target: x86-64](b2-x86-64.md).</p>

???+ remember "Before you start, remember"

    ??? question "What does macro expansion give every kind of tree node, and what does it never look at?"

        One fixed block of instructions, chosen only by the node's own
        shape: never its operands' identity, what happens to its result, or
        the instruction that came before it.

        Introduced in [B1. The simplest back end that works](b1-simplest-backend.md).

    ??? question "Which AArch64 addressing mode computes `x1 + (x2 << 2)` as part of a single instruction?"

        Register-offset addressing with a shift, written `[x1, x2, lsl #2]`.
        The shift amount has to match the size of the element being loaded.

        Introduced in [A2. Reading and writing AArch64 assembly](a2-aarch64-assembly.md#loads-stores-and-addresses).

    ??? question "Why can x86-64 fold `b[i]`'s load directly into `mulss`, but not `a[i]`'s?"

        `mulss`'s destination register is also a source operand: `a[i]` has
        to already be sitting in that register before the instruction runs,
        so only the other operand is free to be read straight from memory.

        Introduced in [B2. A second target: x86-64](b2-x86-64.md#folding-a-load-into-the-arithmetic).

    ??? question "What does SSA form guarantee about a named value?"

        That it is the target of exactly one assignment in the program
        text. A variable assigned in several places is split into
        versions, one per assignment, and every use is renamed to the
        version that reaches it.

        Introduced in [O3. SSA form: construction and destruction](../optimize/o3-ssa.md#one-name-one-definition).

!!! goals "In this chapter"

    - Explain what a tile is, and why letting one instruction cover several tree nodes beats macro expansion's one-instruction-per-node rule.
    - Trace maximal munch's greedy, top-down tile choice on a small expression tree, and read off the instructions it selects.
    - Build a bottom-up cost table that finds the cheapest cover of a tree, and say when greedy and optimal are guaranteed to agree.
    - Recognize why folding a memory operand is a tiling decision and not a fixed per-target rule, and why a shared value breaks a tree-based selector.
    - Connect Vortex's fixed-shape array addressing to the tile sets a back end needs on AArch64 and on x86-64.

## One instruction per node, and its price

[B1](b1-simplest-backend.md) built the simplest working back end: a **macro
expander** that hands every kind of tree node one fixed block of
instructions, decided by the node's own shape and nothing else.[^appel] It is
deliberately blind. An addition next to a multiplication gets no credit for
being next to it; each gets its own instructions, as if it were the only node
in the tree.

Take the address computation an array access lowers to. For a Vortex read
like `m[row, col]`, the compiler needs the byte address `base + row * stride
+ col * element_size`, the same shape [A2](a2-aarch64-assembly.md#one-element-of-a-fixed-shape-array)
compiled by hand for the fixed-shape array `cell` function. A smaller version
of that same shape, `a + ((i * 4 + j) << 2)`, is close to what a two-dimensional
index lowers to before a back end has done anything about it: multiply an
index by a stride, add another index, scale by the element size, add the
base, load. Written as a tree, an internal node for each operation:

--8<-- "includes/examples/backend/c1-instruction-selection/macro_expand.cpp.md"

Five internal nodes, five instructions, one per node, in the order a
postorder walk visits them. That count does not depend on which machine the
back end targets. It would be five on a machine with no multiply-add
instruction at all, and it is still five on AArch64, which can compute this
entire address, shift and all, inside the addressing mode of a single
`ldr`. Macro expansion cannot see that, because it never looks past one node
at a time.

## Tiles, and maximal munch

A **tile** is a small pattern of tree nodes, together with the one
instruction that computes what the pattern as a whole computes. The
single-node tiles are exactly what macro expansion already uses: one tile
per node kind. A **fused tile** covers more than one node. AArch64 offers, for
this shape, two of them directly as instructions:

- `madd result, a, b, c` computes `a * b + c`: one instruction for an
  `add` node whose left child is a `mul` node, no separate multiply.
- A register-offset load with a shift, `ldr result, [base, index, lsl #n]`,
  computes `*(base + (index << n))`: one instruction for a `load` node whose
  address is an `add` of a base and a shifted index.

**Tree covering** is the problem of choosing a set of tiles that together
cover every node in the tree exactly once, at the lowest total instruction
count. **Maximal munch** is the simplest algorithm for it: at each node,
starting from the root, try the largest available tile first; if it
matches, emit it and recurse only into the operands it did not cover; if
nothing larger matches, fall back to the single-node tile.[^cmu] The word
"maximal" describes the greedy rule, not a promise: nothing in the algorithm
proves the choice is cheapest, only that it is the biggest tile available at
that node.

Running maximal munch on the same tree, with the two fused tiles added to
the single-node fallbacks:

--8<-- "includes/examples/backend/c1-instruction-selection/maximal_munch.cpp.md"

Two instructions instead of five. The `madd` tile swallows the multiply and
the inner add; the register-offset `ldr` swallows the outer add, the shift,
and the load itself. Matching happens top-down, root first, but because a
tile's own instruction cannot be emitted until the values it reads are
ready, the instructions themselves come out bottom-up: `madd` first, then
the `ldr` that consumes its result. [Figure 1](#fig-1) shows both directions
on the same tree.

<figure class="vx-figure" id="fig-1">
<svg viewBox="0 0 700 460" role="img" aria-labelledby="c1-tiles-title" aria-describedby="c1-tiles-desc">
<title id="c1-tiles-title">Maximal munch matches top-down, largest tile first</title>
<desc id="c1-tiles-desc">A nine-node tree for the address load(a plus ((i times 4 plus j) shifted left by 2)). Two translucent tile boundaries are drawn over it. The first, drawn first, encloses the load node, the outer add and the shift: a register-offset load tile. The second, drawn after it, encloses the inner add and the multiply: a multiply-add tile. The leaves a, i, 4, j and 2 are not covered by either tile and stay as single-node operands. A caption notes that matching runs top-down, root first, but the instructions it produces come out bottom-up, smallest tile first.</desc>

<line class="vx-line" x1="350" y1="60" x2="350" y2="90"/>
<line class="vx-line" x1="350" y1="130" x2="270" y2="164"/>
<line class="vx-line" x1="350" y1="130" x2="440" y2="180"/>
<line class="vx-line" x1="440" y1="200" x2="390" y2="244"/>
<line class="vx-line" x1="440" y1="200" x2="540" y2="246"/>
<line class="vx-line" x1="390" y1="264" x2="330" y2="314"/>
<line class="vx-line" x1="390" y1="264" x2="500" y2="316"/>
<line class="vx-line" x1="330" y1="334" x2="270" y2="378"/>
<line class="vx-line" x1="330" y1="334" x2="390" y2="378"/>

<g class="vx-seq" style="--vx-i: 0; --vx-n: 2">
<rect class="vx-box-strong" x="300" y="18" width="230" height="195" rx="18" opacity="0.16"/>
</g>
<g class="vx-seq" style="--vx-i: 1; --vx-n: 2">
<rect class="vx-box-accent" x="295" y="228" width="150" height="118" rx="16" opacity="0.22"/>
</g>

<circle class="vx-box" cx="350" cy="40" r="20"/>
<text class="vx-text" x="350" y="45" text-anchor="middle">load</text>
<circle class="vx-box" cx="350" cy="110" r="20"/>
<text class="vx-text" x="350" y="115" text-anchor="middle">add</text>
<rect class="vx-box" x="228" y="152" width="44" height="26" rx="4"/>
<text class="vx-text" x="250" y="169" text-anchor="middle">a</text>
<circle class="vx-box" cx="440" cy="180" r="20"/>
<text class="vx-text" x="440" y="185" text-anchor="middle">shl</text>
<circle class="vx-box" cx="390" cy="250" r="20"/>
<text class="vx-text" x="390" y="255" text-anchor="middle">add</text>
<rect class="vx-box" x="518" y="234" width="44" height="26" rx="4"/>
<text class="vx-text" x="540" y="251" text-anchor="middle">2</text>
<circle class="vx-box" cx="330" cy="320" r="20"/>
<text class="vx-text" x="330" y="325" text-anchor="middle">mul</text>
<rect class="vx-box" x="478" y="304" width="44" height="26" rx="4"/>
<text class="vx-text" x="500" y="321" text-anchor="middle">j</text>
<rect class="vx-box" x="248" y="368" width="44" height="26" rx="4"/>
<text class="vx-text" x="270" y="385" text-anchor="middle">i</text>
<rect class="vx-box" x="368" y="368" width="44" height="26" rx="4"/>
<text class="vx-text" x="390" y="385" text-anchor="middle">4</text>

<text class="vx-text-accent" x="616" y="60">1. ldr-regoffset tile</text>
<text class="vx-text-muted" x="616" y="78">matched first (top-down)</text>
<text class="vx-text-muted" x="616" y="96">emitted second</text>
<text class="vx-text-accent" x="616" y="270">2. madd tile</text>
<text class="vx-text-muted" x="616" y="288">matched second</text>
<text class="vx-text-muted" x="616" y="306">emitted first (bottom-up)</text>

<text class="vx-text-muted" x="20" y="430">Uncovered leaves (a, i, 4, j, 2) stay as single operands,</text>
<text class="vx-text-muted" x="20" y="448">not instructions of their own.</text>
</svg>
<figcaption>Figure 1. Maximal munch on the tree for <code>load(a + ((i * 4 + j) &lt;&lt; 2))</code>. The larger, outer tile is tried first because matching walks the tree from the root down; the instruction it stands for cannot run until the inner tile's result exists, so emission order is the reverse of match order.</figcaption>
</figure>

??? check "Why does macro expansion's instruction count never depend on the target machine, while maximal munch's does?"

    Macro expansion emits exactly one instruction per internal tree node,
    a property of the tree, not the machine. Maximal munch's count depends
    on which fused tiles the target's tile set offers: a machine with a
    `madd` and a scaled, shifted load needs two instructions for this tree;
    a machine with neither still needs five, because every fused tile it
    lacks falls back to single-node tiles.

## Folding a memory operand is a tiling decision

[B2](b2-x86-64.md#folding-a-load-into-the-arithmetic) showed x86-64 read
`b[i]` straight out of memory inside `mulss`, while `a[i]` still needed its
own `movss` first, because `mulss`'s destination register is also a source:
one of the two operands has to already be in a register before the
instruction runs, and only the other is free to come from memory. That is
not a fact about x86-64 in general. It is a fact about *this* tile,
matched against *this* tree: a two-operand, destructive multiply can fold a
memory operand on one side only, and it can do so only for a value that
nothing else in the tree still needs afterward. A value used twice cannot be
folded into two different instructions; it has to be loaded once, into a
register, and read from there each time. Whether a load is folded or kept
separate is exactly the same kind of choice as the `madd` and register-offset
tiles above, and a selector makes it the same way: by matching a tile that
covers the load only when the loaded value has nowhere else to be needed.

x86-64's own address-computing instruction, `lea` ("load effective address"),
folds a base, a scaled index and a displacement into one instruction without
touching memory at all, `lea rax, [rdi + rsi*4]`, which plays the role
AArch64's register-offset addressing plays above.[^cmu] What x86-64 has no
equivalent of is `madd`: its integer multiply has no fused three-operand
form, so a multiply-then-add still costs two instructions unless one of them
is a `lea` whose scale is a compile-time power of two, 1, 2, 4 or 8. Tiling
the same address tree for x86-64 therefore chooses different fused tiles
than AArch64 did, not because the algorithm changes, but because the tile
set the target offers does.

## Finding the cheapest cover, not only a large one

Maximal munch never asks whether a smaller tile now might let a bigger tile
match later. For the tile set above, that never matters: every fused tile
strictly covers more nodes than the single-node tiles it replaces, at the
same cost of one instruction, so taking the largest available tile can never
make the final count worse. That is not true of every tile set. Real
instruction sets have tiles with different costs (a multiply is not free to
treat as equal to a shift on every machine, and some fused forms exist only
for specific operand shapes), and a grammar with several overlapping tiles
of different sizes can let a greedy choice at one node block a cheaper
combination two levels down.

**Optimal tiling** replaces the greedy rule with a table. For every node,
bottom-up, it records the cheapest way to cover the subtree rooted there,
trying every tile that matches at that node and adding up the already-known
cost of whatever the tile leaves uncovered. Aho, Ganapathi and Tjiang set
this out as ordinary dynamic programming over a tree grammar: the cost of a
node is the minimum, over every rule that could produce it, of that rule's
own cost plus the costs already computed for its non-terminal
operands.[^aho] Because the table is filled bottom-up, before any tile is
finally chosen, the choice at the root can use costs that already account for
every choice below it: no tile is picked early on a guess. Pelegrí-Llopart
and Graham's BURS theory and Fraser, Hanson and Proebsting's `iburg` turn the
same idea into a code generator generator, compiling the cost table itself
out of a grammar rather than writing it by hand.[^burs] [^iburg] Proebsting
later showed the whole per-node cost table can be precomputed into a finite
automaton, so a compiler pays the dynamic-programming cost once, at
generator-build time, and only runs table lookups while compiling.[^bursauto]

--8<-- "includes/examples/backend/c1-instruction-selection/dp_selector.cpp.md"

The DP selector reaches the same two-instruction cover maximal munch found
for the one-address tree, and scales cleanly to a tree built from two
independent address computations added together: two fused loads, one
final add, five instructions total, not ten. For this tile set, greedy and
optimal agree everywhere, because nothing here rewards taking a smaller tile
now. That agreement is worth stating precisely rather than assuming: it
holds whenever every fused tile's cost is no more than the sum of the costs
of what it replaces, for every pair of tiles that could ever compete at the
same node. Once tile costs vary independently of what they cover, that
guarantee is gone, and only the bottom-up table is still correct.

??? check "The maximal-munch and DP selectors above choose the same instructions. Under what condition is that guaranteed, and not a coincidence of this particular tree?"

    It holds when no fused tile ever costs more than the tiles it would
    replace at the same node: taking the largest available match can then
    never make the total worse, because it never trades a cheap cover for
    an expensive one. Once tile costs differ in ways that do not track how
    much they cover, a greedy choice at one node can block a cheaper
    combination further down, and only the bottom-up table is still
    guaranteed correct.

## Where trees stop working

Everything so far assumed the input is a tree: every node has exactly one
parent, so covering it with a tile never affects any other part of the
program. Real intermediate representations are not trees. [O3](../optimize/o3-ssa.md#one-name-one-definition)
gave every SSA value exactly one definition, but a value can have many
*uses*: `x * 2` computed once and used in three later instructions is one
node with three parents, a **DAG**, not a tree. A tree-based selector faces a
choice it cannot make correctly on its own: cover the shared node once, in
only one of the three places that need it, and the other two have nowhere
to read the result from; or duplicate the computation at each use, which
gives every use its own copy of the multiply and throws away the sharing
that made the value worth naming once. Neither choice is available to
maximal munch or to the DP table above, because both assume a node belongs
to exactly one tile.

Covering a DAG optimally is harder than covering a tree: Koes and Goldstein
show it is NP-hard in general, and give a near-optimal algorithm that
partitions the DAG into a small number of trees first and tiles each one,
accepting that some sharing gets duplicated so tree methods can still
apply.[^dagcov] Ebner and coauthors take a different route for
SSA-form input specifically, matching patterns directly against the SSA value
graph, phis included, rather than converting it to a tree or a general
DAG first.[^ssagraph] Hjort Blindell's survey lines these approaches up as
a ladder with four rungs: macro expansion, tree covering, DAG covering, and
graph covering, each one looking at more of the surrounding code before
choosing an instruction, at a rising cost to compute.[^hjort] [B1](b1-simplest-backend.md)
stood on the first rung; this chapter climbed to the second; production
compilers, next, live on the third and fourth.

??? check "Why can a tree-based selector not simply reuse the same folded operand at every use of a shared value?"

    A tile that folds a computation directly into the instruction that
    consumes it consumes the tree node, not a register holding its result.
    A second use has no register to read from unless the value was
    materialized into one first, which is exactly the choice a tree method
    cannot make on its own: it never sees that the node has more than one
    parent.

## How production compilers select

LLVM's default pipeline, SelectionDAG, builds one DAG per basic block from
the IR, then runs combine passes before and after two legalization steps
(making every type and every operation one the target actually supports),
before a pattern-matching selection pass and a post-selection schedule.[^codegen]
GlobalISel is a newer alternative that skips the per-block DAG and works
directly on the whole function in MIR, LLVM's machine-level IR, through four
passes: IRTranslator, Legalizer, RegBankSelect and InstructionSelect.[^globalisel]
On this machine, llc 18.1.8, `-debug-pass=Structure` shows AArch64 running
GlobalISel's four passes at `-O0` and SelectionDAG's `aarch64-isel` at `-O2`,
checked September 2026: the faster, simpler path for a debug build, the
pattern-matching path once optimization is asked for.

Cranelift, the back end inside Wasmtime, takes a different shape again: one
backward pass over the whole function computes use counts up front, then
walks it, matching many-to-one patterns, such as a shift folded into a
load's own addressing mode, without a separate DAG-construction step.[^cranelift]
Its patterns are written in ISLE, a small typed term-rewriting language
compiled ahead of time into ordinary Rust match code, rather than tables
interpreted at compile time.[^cranelift] Go's compiler keeps its selection
even simpler: a `lower` pass turns architecture-independent SSA into
machine-specific operations, and most of its rewrite rules are generated
from a compact rule file rather than written by hand as Go
source.[^go-ssa] None of these designs contradicts what the tree examples
above showed; they differ in how much of the program a pattern can see at
once (one tree, a DAG, or a whole function's SSA graph) and in how the
pattern-to-cost table itself gets built, by hand, by a generator, or by a
compiled rewrite language.

## For Vortex

!!! vortex "Exercise"

    Design tile sets for the address arithmetic Vortex's fixed-shape array
    indexing produces, for both AArch64 and x86-64. Do not write a
    dynamic-programming engine yet: a hand-written maximal-munch matcher
    over the handful of address shapes Vortex's array indexing actually
    produces is enough for v0.1, and matches what [B1](b1-simplest-backend.md)
    already built the toolchain around.

    Work from the matrix multiplication kernel's inner loop, whose `&mut`
    output parameter ([B2](b2-x86-64.md), and [stage 8](../compiler/guide/stage-8-data-in-memory.md#references-and-passing-without-copying))
    is read and written through the same address computation on every
    iteration. List, for each target, which address shapes get a fused
    tile and which fall back to separate instructions; do not fold a load
    that the loop still needs after the fused instruction consumes it, and
    do not fold anything across the floating-point multiply and add in the
    accumulation itself; Vortex's ban on contraction ([decision
    56](../decisions/numbers.md#d56)) makes that fusion illegal regardless
    of what any tile set offers.

    Prove the tile choice is correct before asking whether it is fast:
    write a differential test, in the shape [B1](b1-simplest-backend.md#two-back-ends-one-oracle)
    already set up, that runs the tiled back end and the macro-expansion
    back end on the same Vortex program and checks they compute the same
    answer, for every address shape your tile set claims to handle. Only
    once that passes, count instructions per inner-loop iteration on both
    targets and record the count with the machine and compiler version you
    measured it on; do not carry over the counts this chapter's toy tree
    produced, since Vortex's real IR and its bounds checks will not match
    them exactly.

## Key ideas

!!! recap "You can now answer"

    - **What is a tile?** A pattern of tree nodes paired with the one instruction that computes what the whole pattern computes; a single-node tile is what macro expansion already uses.
    - **What does maximal munch do that macro expansion does not?** At each node it tries the largest matching tile first, so one instruction can cover several nodes instead of exactly one.
    - **When is maximal munch guaranteed to find the cheapest cover?** When no fused tile ever costs more than the single-node tiles it replaces at the same node; taking the biggest match can then never make the total worse.
    - **What does a bottom-up cost table add that greedy matching cannot?** A cost, already computed for everything below a node, before any tile is chosen at that node, so the choice at the root can never be based on a guess about what is cheaper further down.
    - **Why does folding a memory operand depend on more than the target ISA?** Because it depends on whether the loaded value is needed anywhere else in the program; a value used twice cannot be folded into two different instructions.
    - **Why does a tree-based selector fail on a DAG?** A shared node has more than one parent, and a tile can cover a node only once, so nothing in tree covering decides where the one covering instruction's result should be read from by the other uses.

## Where this comes back

!!! next "You will use this again in"

    - [C2. Liveness](c2-liveness.md): *which of a tile's operands still need a register after the tile consumes them*
    - [C6. Instruction scheduling](c6-scheduling.md): *reordering the instructions a tiler already chose*
    - [C7. Peephole optimization](c7-peephole.md): *catching a fusion a tree-based selector missed*
    - [E1. The LLVM code generator pipeline](e1-llvm-codegen-pipeline.md): *SelectionDAG's combine-legalize-combine-select loop, and GlobalISel's four passes*
    - [D3. Reading real back ends](d3-real-backends.md): *Cranelift's ISLE rules and Go's generated `lower` pass, read in full*

## Sources and further reading

Appel's *Modern Compiler Implementation* and Hjort Blindell's survey, both
already read for [B1](b1-simplest-backend.md), are the right next stop:
Appel's instruction-selection chapter covers maximal munch and dynamic
programming in the same order this chapter does, and Hjort Blindell's survey
gives the four-rung ladder its full treatment, with the DAG and
graph-covering literature this chapter only summarizes.[^appel] [^hjort]
Aho, Ganapathi and Tjiang is the original dynamic-programming paper and is
short enough to read in one sitting; Fraser, Hanson and Proebsting's `iburg`
paper ships with working source, worth reading alongside the paper rather
than instead of it.[^aho] [^iburg] CMU's 15-411 lecture notes cover maximal
munch and the two-address problems x86-64 raises for it in a single short
set of slides.[^cmu]

[^appel]: Andrew W. Appel, *Modern Compiler Implementation*, Cambridge University Press, 1998: chapter "Instruction Selection". <https://www.cs.princeton.edu/~appel/modern/toc.html>
[^hjort]: Gabriel Hjort Blindell, "Survey on Instruction Selection: An Extensive and Modern Literature Study", arXiv:1306.4898: the section classifying instruction-selection approaches into macro expansion, tree covering, DAG covering and graph covering. <https://arxiv.org/abs/1306.4898>
[^cmu]: Carnegie Mellon University, 15-411 Compiler Design, Frank Pfenning, lecture notes on instruction selection, maximal munch and x86-64 two-address issues. <https://www.cs.cmu.edu/~fp/courses/15411-f13/>
[^aho]: Alfred V. Aho, Mahadevan Ganapathi and Steven W. K. Tjiang, "Code generation using tree matching and dynamic programming", *ACM Transactions on Programming Languages and Systems* 11(4), 1989. <https://doi.org/10.1145/69558.75700>
[^burs]: Eduardo Pelegrí-Llopart and Susan L. Graham, "Optimal code generation for expression trees: an application of BURS theory", *POPL 1988*. <https://doi.org/10.1145/73560.73586>
[^iburg]: Christopher W. Fraser, David R. Hanson and Todd A. Proebsting, "Engineering a simple, efficient code-generator generator", *ACM Letters on Programming Languages and Systems* 1(3), 1992, and the `iburg` source. <https://doi.org/10.1145/151640.151642> ; <https://github.com/drh/iburg>
[^bursauto]: Todd A. Proebsting, "BURS automata generation", *ACM Transactions on Programming Languages and Systems* 17(3), 1995. <https://doi.org/10.1145/203095.203098>
[^dagcov]: David Ryan Koes and Seth Copen Goldstein, "Near-optimal instruction selection on DAGs", *CGO 2008*. <https://doi.org/10.1145/1356058.1356065>
[^ssagraph]: Gabriel Ebner, Florian Brandner, Bernhard Scholz, Andreas Krall, Peter Wiedermann and Albrecht Kadlec, "Generalized instruction selection using SSA-graphs", *LCTES 2008*. <https://doi.org/10.1145/1375657.1375663>
[^codegen]: LLVM Project, "The LLVM Target-Independent Code Generator": the SelectionDAG phase list and MIR. <https://llvm.org/docs/CodeGenerator.html>
[^globalisel]: LLVM Project, "GlobalISel": IRTranslator, Legalizer, RegBankSelect and InstructionSelect. <https://llvm.org/docs/GlobalISel/index.html>
[^cranelift]: Chris Fallin, "Cranelift's Instruction Selector, Part 1: Pattern-matching" (18 September 2020) and "Cranelift's ISLE, Part 4: The ISLE Language" (20 January 2023). <https://cfallin.org/blog/2020/09/18/cranelift-isel-1/> ; <https://cfallin.org/blog/2023/01/20/cranelift-isle/>
[^go-ssa]: The Go Authors, "SSA Backend", `cmd/compile/internal/ssa/README.md`: the `lower` pass and its generated rewrite rules. <https://github.com/golang/go/blob/master/src/cmd/compile/internal/ssa/README.md>

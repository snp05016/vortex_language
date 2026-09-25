# C1. Instruction selection

<p class="page-intro">Instruction selection decides which machine instructions compute each piece of a program. This chapter replaces B1's one-template-per-node rule with tiles that cover several nodes at once, chosen greedily and then optimally, and shows where tree methods stop once values are shared. For Vortex, it is the step that turns every array index in the matrix kernel into one or two instructions instead of seven.</p>

<p class="vx-meta" markdown="1">Level: Intermediate · Reading time: about 40 minutes · Builds on: [B1. The simplest back end that works](b1-simplest-backend.md), [B2. A second target: x86-64](b2-x86-64.md)</p>

???+ remember "Before you start, remember"

    ??? question "What does macro expansion give every kind of tree node, and what does it never look at?"

        One fixed block of instructions, chosen by the node's own kind
        alone: never what its operands are, what happens to its result, or
        which instruction came before it.

        Introduced in [B1. The simplest back end that works](b1-simplest-backend.md#one-template-per-operation).

    ??? question "Which shift amounts may `ldr w0, [x1, x2, lsl #n]` use, and why?"

        Only `#0` or `#2`. A 4-byte load scales its index by the element
        size or not at all; an 8-byte load allows `#0` or `#3`.

        Introduced in [A2. Reading and writing AArch64 assembly](a2-aarch64-assembly.md#loads-stores-and-addresses).

    ??? question "What does `madd x8, x1, x8, x0` compute?"

        `x1 * x8 + x0`: a multiply and an add in one instruction. All three
        sources are registers, which is why clang's listing for `cell` put
        the constant 12 into `w8` with a `mov` first.

        Introduced in [A2. Reading and writing AArch64 assembly](a2-aarch64-assembly.md#one-element-of-a-fixed-shape-array).

    ??? question "In B2's dot-product loop, which load did x86-64 fold into the multiply, and what did AArch64 do instead?"

        x86-64 read `b[i]` from memory inside `mulss`, after loading `a[i]`
        into the destination register with `movss`. AArch64 loaded both
        values with `ldr`, because its arithmetic reads only registers.

        Introduced in [B2. A second target: x86-64](b2-x86-64.md#folding-a-load-into-the-arithmetic).

    ??? question "What does SSA form guarantee about a named value, and what does it not limit?"

        Each value has exactly one definition. It may have any number of
        uses.

        Introduced in [O3. SSA form: construction and destruction](../optimize/o3-ssa.md#one-name-one-definition).

!!! goals "In this chapter"

    - Describe a tile as a pattern, an instruction and a cost, and cover an expression tree with tiles by hand.
    - Trace maximal munch on a tree and explain how its greedy choice can cost an extra instruction.
    - Fill in a bottom-up cost table that finds the cheapest cover of a tree, and say what "cheapest" depends on.
    - Compare the tiles AArch64 and x86-64 offer for array addressing, and explain why the same algorithm picks different instructions.
    - Explain why a shared value breaks tree covering, and how DAG, SSA-graph and production selectors respond.

A back end receives the program as trees or graphs of IR operations and must
print machine instructions. **Instruction selection** is the step that decides
which ones: for each part of the IR, which of the target's instructions
compute it. It runs before register allocation, so its output still names
values with unlimited **temporaries** (`t1`, `t2`, and so on) that
[C3](c3-linear-scan.md) later maps to real registers or stack slots.[^cmu]

The step matters most where Vortex spends its time. Every `a[row, k]` in the
[stage 10](../compiler/guide/stage-10-matrix-multiplication.md#the-program-the-milestone-asks-for)
kernel lowers to a small tree of multiplies, adds and shifts that ends in a
load, and both targets in this book have instructions built for exactly that
shape. [B1](b1-simplest-backend.md)'s macro expander cannot use them, because
it never looks at more than one node. This chapter covers the classical ways
to use them: covering a tree greedily, covering it optimally, and what changes
once a value has more than one use.

## One instruction per node, and its price

Take one array element. For an `i32` array whose rows hold four elements, a
`[i32; N, 4]` in Vortex, element `[i][j]` sits
$(i \times 4 + j) \times 4$ bytes from the start, and reading it is a load
from that address. Written as a tree in prefix form, with the scaling by the
4-byte element size written as a shift by 2:

```text
load(add(a, shl(add(mul(i, 4), j), 2)))
```

The variables `a`, `i` and `j` are already in registers. The constants 4 and
2 are nodes of the tree too. The program below is B1's macro expander on this
tree, printing AArch64-style instructions with temporaries:

--8<-- "includes/examples/backend/c1-instruction-selection/macro_expand.cpp.md"

Seven instructions, one per node. Even the constants cost an instruction each:
the template for `mul` cannot know that its right operand is the constant 4,
so the constant goes into a register first with `mov`, and the shift by 2
becomes a shift by a register.

LLVM does the same job in two. The example below is the same computation as
LLVM IR (`getelementptr` does the scaling by 4):

--8<-- "includes/examples/backend/c1-instruction-selection/address.ll.md"

Compiled by `llc -O2` (LLVM 18.1.8, on an Apple M4 Pro, checked 24 September
2026), the function body is:

```gas
	add	x8, x2, x1, lsl #2
	ldr	w0, [x0, x8, lsl #2]
```

The `add` computes `j + (i << 2)`, which is `i * 4 + j`, in one instruction:
its second operand is shifted before the addition. The `ldr` adds the base
`a` to that index shifted by 2 and loads, in one instruction. Five of the
tree's seven nodes and both constants disappear into two instructions. The
rest of this chapter is about how a selector finds that answer.

## Tiles and covers

A **tile** is a small tree pattern together with the instruction that computes
it and a cost, here the number of instructions. A pattern has two kinds of
**holes**, the places where it stops. A register hole, written `R`, matches
any subtree; that subtree is computed into a register first, by other tiles,
and the tile reads the register. An immediate hole, written `#c`, matches only
a constant that passes a test; the constant is written into the instruction
itself and costs nothing at run time.

These are the AArch64 tiles this chapter uses:

| Tile | Pattern | Instruction | Nodes covered |
| --- | --- | --- | --- |
| ldr-shifted | `load(add(R, shl(R, #2)))` | `ldr d, [x, y, lsl #2]` | load, add, shl, the 2 |
| add-shifted | `add(mul(R, #2^k), R)` | `add d, z, x, lsl #k` | add, mul, the constant |
| madd | `add(mul(R, R), R)` | `madd d, x, y, z` | add, mul |
| lsl-imm | `shl(R, #c)` | `lsl d, x, #c` | shl, the constant |
| ldr, add, mul | one operator, all holes `R` | `ldr d, [x]` and so on | one node |
| mov | `#c` | `mov d, #c` | one constant |

The register-offset load is the addressing mode from
[A2](a2-aarch64-assembly.md#loads-stores-and-addresses).[^a64-ldr-reg] The
add-shifted tile is the shifted-register form of `add`, whose encoding
[A1](a1-machine-model.md) took apart: its second source is shifted left by a
constant before the addition.[^a64-add-sr] Its pattern matches a multiply by a
power of two, because multiplying by $2^k$ and shifting left by $k$ give the
same result; the tile's test on the constant is part of the pattern. `madd`
multiplies two registers and adds a third, and `mov` puts a constant into a
register.[^a64-madd] [^a64-mov]

A **cover**, also called a tiling, is a set of tiles in which every operator
node and every constant belongs to exactly one tile, and the tiles fit
together: each register hole sits on a variable or on the root of another
tile. The cost of a cover is the sum of its tiles' costs. Macro expansion is
the cover built only from one-node tiles. [Figure 1](#fig-c1-cover) shows the
two-tile cover LLVM chose.

<figure class="vx-figure" id="fig-c1-cover">
<svg viewBox="0 0 720 430" role="img" aria-label="The two-tile cover of the address tree" aria-describedby="c1-cover-desc">
<title>The two-tile cover of the address tree</title>
<desc id="c1-cover-desc">The tree load(add(a, shl(add(mul(i, 4), j), 2))). Operator nodes are circles and leaves are boxes. A first outlined region, drawn first, covers the load, the add below it, the shl and the constant 2: the ldr-shifted tile, labelled ldr t2, [a, t1, lsl #2]. A second outlined region covers the inner add, the mul and the constant 4: the add-shifted tile, labelled add t1, j, i, lsl #2. The variables a, i and j lie outside both regions and stay in registers.</desc>
<line class="vx-line" x1="360" y1="62" x2="360" y2="88"/>
<line class="vx-line" x1="345" y1="126" x2="265" y2="167"/>
<line class="vx-line" x1="378" y1="122" x2="442" y2="168"/>
<line class="vx-line" x1="448" y1="198" x2="412" y2="232"/>
<line class="vx-line" x1="478" y1="192" x2="530" y2="237"/>
<line class="vx-line" x1="385" y1="265" x2="345" y2="303"/>
<line class="vx-line" x1="418" y1="262" x2="458" y2="307"/>
<line class="vx-line" x1="315" y1="336" x2="280" y2="377"/>
<line class="vx-line" x1="345" y1="336" x2="380" y2="377"/>
<g class="vx-seq" style="--vx-i: 0; --vx-n: 2">
<polygon class="vx-box-accent" fill-opacity="0.35" stroke-linejoin="round" points="325,15 395,15 395,85 500,150 580,225 580,275 505,275 425,210 325,135"/>
<text class="vx-text-accent" x="20" y="60">tile 1: ldr-shifted</text>
<text class="vx-mono" x="20" y="82">ldr t2, [a, t1, lsl #2]</text>
</g>
<g class="vx-seq" style="--vx-i: 1; --vx-n: 2">
<polygon class="vx-box-accent" fill-opacity="0.35" stroke-linejoin="round" points="372,224 428,224 428,290 418,414 362,414 302,346 302,296 372,270"/>
<text class="vx-text-accent" x="20" y="300">tile 2: add-shifted</text>
<text class="vx-mono" x="20" y="322">add t1, j, i, lsl #2</text>
</g>
<circle class="vx-box-strong" cx="360" cy="40" r="22"/>
<text class="vx-text" x="360" y="45" text-anchor="middle">load</text>
<circle class="vx-box-strong" cx="360" cy="110" r="20"/>
<text class="vx-text" x="360" y="115" text-anchor="middle">add</text>
<rect class="vx-box" x="228" y="167" width="44" height="26" rx="4"/>
<text class="vx-mono" x="250" y="185" text-anchor="middle">a</text>
<circle class="vx-box-strong" cx="460" cy="180" r="20"/>
<text class="vx-text" x="460" y="185" text-anchor="middle">shl</text>
<circle class="vx-box-strong" cx="400" cy="250" r="20"/>
<text class="vx-text" x="400" y="255" text-anchor="middle">add</text>
<rect class="vx-box" x="518" y="237" width="44" height="26" rx="4"/>
<text class="vx-mono" x="540" y="255" text-anchor="middle">2</text>
<circle class="vx-box-strong" cx="330" cy="320" r="20"/>
<text class="vx-text" x="330" y="325" text-anchor="middle">mul</text>
<rect class="vx-box" x="448" y="307" width="44" height="26" rx="4"/>
<text class="vx-mono" x="470" y="325" text-anchor="middle">j</text>
<rect class="vx-box" x="248" y="377" width="44" height="26" rx="4"/>
<text class="vx-mono" x="270" y="395" text-anchor="middle">i</text>
<rect class="vx-box" x="368" y="377" width="44" height="26" rx="4"/>
<text class="vx-mono" x="390" y="395" text-anchor="middle">4</text>
</svg>
<figcaption>Figure 1. The cheapest cover of the address tree with the tiles in the table. Each outlined region is one tile and one instruction; the constants 4 and 2 sit inside tiles, as a shift amount, instead of needing a <code>mov</code> of their own. The variables stay outside every tile: they are already in registers. Macro expansion covers the same tree with seven one-node tiles.</figcaption>
</figure>

A tile's tests are part of its meaning, not an optimization. The ldr-shifted
tile accepts only the constant 2, because a 4-byte load allows no other
nonzero shift. A tile that skipped that test would not produce slower code;
it would produce an instruction the assembler rejects.

??? check "A tree loads a 4-byte element from `a + (x << 3)`, every second element of the array. Which tiles from the table cover it, and what do they emit?"

    Not ldr-shifted: its test rejects the constant 3, and a 4-byte load
    cannot shift its index by 3. The shift gets its own tile, lsl-imm
    (`lsl t1, x, #3`), then `add t2, a, t1` and `ldr t3, [t2]`: three
    instructions with this table. A table that also had an `add` with a
    shifted `shl` operand could compute `a + (x << 3)` in one
    instruction and load with `ldr t2, [t1]`, for two.

## Maximal munch

**Maximal munch** is the simplest way to choose a cover. It walks the tree top
down: at the root, it tries the tiles from largest to smallest and takes the
first one that matches, then does the same for each subtree the tile left in a
register hole. Pfenning's 15-411 notes describe it as matching the deepest
pattern first at the root, and judge that it gives "acceptable results in
practice".[^cmu] Matching runs top down, but the instructions
come out bottom up: a tile cannot be emitted until the registers its holes
read have been computed.

On the address tree, ldr-shifted matches at the root, leaving `a` and the
index `add(mul(i, 4), j)` in register holes. At the index, two tiles match
and both cover the same two operator nodes: madd and add-shifted. Size cannot
separate them, so the order of the tile list decides. The program below runs
maximal munch twice, with those two tiles swapped:

--8<-- "includes/examples/backend/c1-instruction-selection/maximal_munch.cpp.md"

With add-shifted listed first, munch finds the same two instructions as LLVM.
With madd first, it pays three. The madd pattern has a register hole where the
constant 4 is, so the constant needs a `mov` before the `madd` can read it
([Figure 2](#fig-c1-trap)). At the moment munch chooses, it compares how much
of the tree each tile covers, not what each tile leaves behind for the tiles
below. Once a choice is made it is never revisited.

<figure class="vx-figure" id="fig-c1-trap">
<svg viewBox="0 0 740 330" role="img" aria-label="Two ways to cover the index subtree" aria-describedby="c1-trap-desc">
<title>Two ways to cover the index subtree</title>
<desc id="c1-trap-desc">Two copies of the subtree add(mul(i, 4), j). Left: one tile outlines the add and the mul, a madd, and a dashed box marks the constant 4, which is left over and needs its own mov: mov t1, #4 and madd t2, i, t1, j, two instructions. Right: one tile outlines the add, the mul and the constant 4 together, add-shifted: add t1, j, i, lsl #2, one instruction.</desc>
<line class="vx-line" x1="156" y1="74" x2="124" y2="116"/>
<line class="vx-line" x1="184" y1="74" x2="230" y2="117"/>
<line class="vx-line" x1="98" y1="146" x2="68" y2="187"/>
<line class="vx-line" x1="122" y1="146" x2="152" y2="187"/>
<polygon class="vx-box-accent" fill-opacity="0.35" stroke-linejoin="round" points="145,35 195,35 195,85 135,157 85,157 85,105"/>
<rect class="vx-box-bad" fill-opacity="0.35" x="128" y="178" width="64" height="44" rx="8"/>
<circle class="vx-box-strong" cx="170" cy="60" r="20"/>
<text class="vx-text" x="170" y="65" text-anchor="middle">add</text>
<circle class="vx-box-strong" cx="110" cy="130" r="20"/>
<text class="vx-text" x="110" y="135" text-anchor="middle">mul</text>
<rect class="vx-box" x="218" y="117" width="44" height="26" rx="4"/>
<text class="vx-mono" x="240" y="135" text-anchor="middle">j</text>
<rect class="vx-box" x="38" y="187" width="44" height="26" rx="4"/>
<text class="vx-mono" x="60" y="205" text-anchor="middle">i</text>
<rect class="vx-box" x="138" y="187" width="44" height="26" rx="4"/>
<text class="vx-mono" x="160" y="205" text-anchor="middle">4</text>
<text class="vx-text" x="20" y="255">madd listed first</text>
<text class="vx-mono" x="20" y="278">mov  t1, #4</text>
<text class="vx-mono" x="20" y="298">madd t2, i, t1, j</text>
<text class="vx-text-muted" x="20" y="320">2 instructions: the 4 is left over</text>
<line class="vx-line" x1="526" y1="74" x2="494" y2="116"/>
<line class="vx-line" x1="554" y1="74" x2="600" y2="117"/>
<line class="vx-line" x1="468" y1="146" x2="438" y2="187"/>
<line class="vx-line" x1="492" y1="146" x2="522" y2="187"/>
<polygon class="vx-box-accent" fill-opacity="0.35" stroke-linejoin="round" points="515,35 565,35 565,85 562,225 500,225 455,160 455,105"/>
<circle class="vx-box-strong" cx="540" cy="60" r="20"/>
<text class="vx-text" x="540" y="65" text-anchor="middle">add</text>
<circle class="vx-box-strong" cx="480" cy="130" r="20"/>
<text class="vx-text" x="480" y="135" text-anchor="middle">mul</text>
<rect class="vx-box" x="588" y="117" width="44" height="26" rx="4"/>
<text class="vx-mono" x="610" y="135" text-anchor="middle">j</text>
<rect class="vx-box" x="408" y="187" width="44" height="26" rx="4"/>
<text class="vx-mono" x="430" y="205" text-anchor="middle">i</text>
<rect class="vx-box" x="508" y="187" width="44" height="26" rx="4"/>
<text class="vx-mono" x="530" y="205" text-anchor="middle">4</text>
<text class="vx-text" x="400" y="255">add-shifted listed first</text>
<text class="vx-mono" x="400" y="278">add  t1, j, i, lsl #2</text>
<text class="vx-text-muted" x="400" y="320">1 instruction: the 4 becomes the shift</text>
</svg>
<figcaption>Figure 2. The same subtree covered by two tiles of equal size. Both tiles swallow the <code>add</code> and the <code>mul</code>, so maximal munch cannot tell them apart; but madd reads the 4 from a register, leaving a constant that needs its own instruction (dashed), while add-shifted writes it into the instruction as a shift amount.</figcaption>
</figure>

What maximal munch does guarantee is local. If the tile list is sorted by
size, no tile in its cover could be merged with the tile below it into one
tile from the list: munch would have taken the merged tile first. It does not
guarantee the smallest total, and a larger tile at the top can leave a costly
remainder underneath. Its advantage is speed and simplicity: one pass, each
node visited once.

Real compilers carry the repair for this particular trap as a separate
rewrite. Go's AArch64 rules include one that turns a `MADD` whose multiplier
is a power-of-two constant into `ADDshiftLL`, an add with a shifted operand:
the add-shifted tile, recovered after the fact.[^go-arm64]

??? check "Why can maximal munch not see, at the index node, that madd will cost an extra instruction?"

    The extra cost is in a subtree madd leaves behind: the constant 4 in
    a register hole. Munch compares tiles by the nodes they cover at the
    point of choice and has not yet looked below them. To compare the full
    cost of each choice, it would need the cheapest cost of every subtree a
    tile might leave in a hole, before choosing. That is what the next
    method computes.

## Optimal tiling by dynamic programming

The fix is to compute, before choosing anything, the cheapest cost of every
subtree. Label each node, bottom up, with the cheapest way to compute its
value into a register: for every tile that matches at the node, add the tile's
own cost to the already-known best costs of the subtrees in its register
holes, and keep the minimum. A variable costs 0. When the root is labelled,
walk down from it and emit the tiles the labels recorded.[^cmu]

This works because in a tree, subtrees share no nodes. The cheapest way to
compute a subtree into a register does not depend on anything above it, so
the best cover of the whole tree is built from best covers of its parts. This
is the classical **dynamic programming** selector: Aho, Ganapathi and
Tjiang's Twig finds the tiles that match, labels bottom up with costs, and
emits top down.[^aho] [^hjort] For a fixed tile set, the labelling does a
bounded amount of work per node, so it runs in time linear in the size of the
tree.[^hjort]

The program below labels the address tree and prints every candidate at every
node, then emits the cover. The same engine then runs with an x86-64 tile
set, which the next section explains:

--8<-- "includes/examples/backend/c1-instruction-selection/dp_selector.cpp.md"

Read the table from the top; it is in the order the labelling visits nodes.
The constant `4` can only be computed by `mov`: cost 1. At `mul(i, 4)`, the
one-node `mul` costs its own instruction plus the 1 for the constant: 2. At
the index `add(mul(i, 4), j)`, three tiles match. madd costs 1 plus the costs
of its holes `i`, `4` and `j`: 1 + 0 + 1 + 0 = 2. add-shifted has holes only
for `i` and `j`, since 4 is an immediate: 1 + 0 + 0 = 1. The plain `add` pays
for the whole `mul` subtree below it: 1 + 2 + 0 = 3. The label is 1, from
add-shifted.

The rest follows the same way. At the root, ldr-shifted costs 1 plus 0 for
`a` plus 1 for the index, 2 in total, against 4 for a plain `ldr` of an
address computed separately. The table also prices subtrees that the final
cover never computes on their own, such as `mul(i, 4)`. It has to: when a node
is labelled, nothing yet says whether a tile above will leave it in a hole.
[Figure 3](#fig-c1-labels) shows the labels appearing in that order.

<figure class="vx-figure" id="fig-c1-labels">
<svg viewBox="0 0 720 450" role="img" aria-label="Bottom-up cost labels on the address tree" aria-describedby="c1-labels-desc">
<title>Bottom-up cost labels on the address tree</title>
<desc id="c1-labels-desc">The address tree again, with a label beside each node giving its cheapest cost and the tile that achieves it, appearing in postorder. The constant 4: 1, mov. mul: 2, mul. The inner add: 1, add-shifted. The constant 2: 1, mov. shl: 2, lsl-imm. The outer add: 3, add. load: 2, ldr-shifted. The variables a, i and j are labelled 0.</desc>
<line class="vx-line" x1="360" y1="62" x2="360" y2="88"/>
<line class="vx-line" x1="345" y1="126" x2="265" y2="167"/>
<line class="vx-line" x1="378" y1="122" x2="442" y2="168"/>
<line class="vx-line" x1="448" y1="198" x2="412" y2="232"/>
<line class="vx-line" x1="478" y1="192" x2="530" y2="237"/>
<line class="vx-line" x1="385" y1="265" x2="345" y2="303"/>
<line class="vx-line" x1="418" y1="262" x2="458" y2="307"/>
<line class="vx-line" x1="315" y1="336" x2="280" y2="377"/>
<line class="vx-line" x1="345" y1="336" x2="380" y2="377"/>
<circle class="vx-box-strong" cx="360" cy="40" r="22"/>
<text class="vx-text" x="360" y="45" text-anchor="middle">load</text>
<circle class="vx-box-strong" cx="360" cy="110" r="20"/>
<text class="vx-text" x="360" y="115" text-anchor="middle">add</text>
<rect class="vx-box" x="228" y="167" width="44" height="26" rx="4"/>
<text class="vx-mono" x="250" y="185" text-anchor="middle">a</text>
<circle class="vx-box-strong" cx="460" cy="180" r="20"/>
<text class="vx-text" x="460" y="185" text-anchor="middle">shl</text>
<circle class="vx-box-strong" cx="400" cy="250" r="20"/>
<text class="vx-text" x="400" y="255" text-anchor="middle">add</text>
<rect class="vx-box" x="518" y="237" width="44" height="26" rx="4"/>
<text class="vx-mono" x="540" y="255" text-anchor="middle">2</text>
<circle class="vx-box-strong" cx="330" cy="320" r="20"/>
<text class="vx-text" x="330" y="325" text-anchor="middle">mul</text>
<rect class="vx-box" x="448" y="307" width="44" height="26" rx="4"/>
<text class="vx-mono" x="470" y="325" text-anchor="middle">j</text>
<rect class="vx-box" x="248" y="377" width="44" height="26" rx="4"/>
<text class="vx-mono" x="270" y="395" text-anchor="middle">i</text>
<rect class="vx-box" x="368" y="377" width="44" height="26" rx="4"/>
<text class="vx-mono" x="390" y="395" text-anchor="middle">4</text>
<text class="vx-text-muted" x="220" y="185" text-anchor="end">0</text>
<text class="vx-text-muted" x="240" y="395" text-anchor="end">0</text>
<text class="vx-text-muted" x="500" y="325">0</text>
<g class="vx-seq" style="--vx-i: 0; --vx-n: 7"><text class="vx-text-accent" x="420" y="395">1  mov</text></g>
<g class="vx-seq" style="--vx-i: 1; --vx-n: 7"><text class="vx-text-accent" x="302" y="325" text-anchor="end">2  mul</text></g>
<g class="vx-seq" style="--vx-i: 2; --vx-n: 7"><text class="vx-text-accent" x="372" y="245" text-anchor="end">1  add-shifted</text></g>
<g class="vx-seq" style="--vx-i: 3; --vx-n: 7"><text class="vx-text-accent" x="540" y="287" text-anchor="middle">1  mov</text></g>
<g class="vx-seq" style="--vx-i: 4; --vx-n: 7"><text class="vx-text-accent" x="488" y="185">2  lsl-imm</text></g>
<g class="vx-seq" style="--vx-i: 5; --vx-n: 7"><text class="vx-text-accent" x="388" y="105">3  add</text></g>
<g class="vx-seq" style="--vx-i: 6; --vx-n: 7"><text class="vx-text-accent" x="390" y="36">2  ldr-shifted</text></g>
<text class="vx-text-muted" x="20" y="425">Each label: the cheapest cost of computing that node into a register,</text>
<text class="vx-text-muted" x="20" y="443">and the tile that achieves it. Labels appear in the order they are computed.</text>
</svg>
<figcaption>Figure 3. The cost labels of the dynamic-programming selector, computed children first. The root's label, 2, uses ldr-shifted, whose register hole on the index reads that node's label, 1, from add-shifted; every other label is computed and then not used by the final cover.</figcaption>
</figure>

"Optimal" means optimal for the cost model, and the model is the weak part.
The 15-411 notes point out that the time an instruction takes is hard to
model and not additive on processors that pipeline and execute out of order,
so a cover with the fewest cycles by the table may not run fastest; code size,
by contrast, is exact and additive.[^cmu] Hjort Blindell's survey gives a
sharper case: two independent 2-cycle instructions against one 3-cycle
instruction, where the better choice depends on whether the scheduler can run
the two in parallel.[^hjort] Counting instructions, as here, is the usual
compromise; [C6](c6-scheduling.md) returns to latency.

### Complete the cover

Now a Vortex shape whose row length is not a power of two: `a[row, k]` for
`a: &[f32; 2, 3]`, the first input of the stage 10 kernel. The tree is
`load(add(a, shl(add(mul(row, 3), k), 2)))`. Fill in the labels with the
AArch64 table before opening the answer:

| Node | Candidates | Label |
| --- | --- | --- |
| `3` | mov: 1 | 1 |
| `mul(row, 3)` | mul: ? | ? |
| `add(mul(row, 3), k)` | madd: ?, add-shifted: ?, add: ? | ? |
| `2` | mov: 1 | 1 |
| `shl(..., 2)` | lsl-imm: 1 + ? | ? |
| `add(a, shl(...))` | add: 1 + 0 + ? | ? |
| root `load(...)` | ldr-shifted: ?, ldr: ? | ? |

??? note "The completed table"

    - `mul(row, 3)`: mul 1 + 0 + 1 = 2.
    - `add(mul(row, 3), k)`: madd 1 + 0 + 1 + 0 = 2; add-shifted does not
      match, because 3 is not a power of two; add 1 + 2 + 0 = 3. Label 2,
      madd.
    - `shl(..., 2)`: lsl-imm 1 + 2 = 3.
    - `add(a, shl(...))`: add 1 + 0 + 3 = 4.
    - The root: ldr-shifted 1 + 0 + 2 = 3; ldr 1 + 4 = 5. Label 3.

    The cover is `mov t1, #3`, `madd t2, row, t1, k`, and
    `ldr t3, [a, t2, lsl #2]` (on real hardware the destination of an
    `f32` load is a floating-point register, [A3](a3-floats-and-vectors.md)).
    Clang's listing for `cell` in [A2](a2-aarch64-assembly.md#one-element-of-a-fixed-shape-array)
    also used three instructions, `mov`, `madd` and a shifted `ldr`, for a
    tree that had been rewritten first: it multiplied `row` by the 12-byte
    row stride and added the base inside the `madd`.

??? check "The table gives `mul(i, 4)` the label 2, but the final cover never computes that node on its own. Could the selector skip labelling it?"

    No. When `mul(i, 4)` is labelled, the tiles above it have not been
    tried yet, and one of them, the plain `add`, would leave it in a
    register hole and need its cost. Only after the parent's candidates are
    compared is it known that the chosen tile swallows the `mul`. Bottom-up
    labelling has to price every subtree so that every choice above can be
    compared on complete costs.

## From hand-written matchers to generators

A real target needs far more tiles than this chapter's eight, and a matcher
written by hand for each is error-prone. Instead, a back end describes its tiles as a **tree grammar**:
each rule rewrites a pattern to a nonterminal such as "a value in a register",
with a cost and an instruction to emit. A **code-generator generator** reads
the grammar and writes the matcher. Twig generated dynamic-programming
selectors from such a description, and Fraser, Hanson and Proebsting's
`iburg` is a small generator in the same style that the survey reports as
simpler and faster than Twig.[^aho] [^iburg] [^hjort]

A second line of work moves the cost comparisons out of the compiler
altogether. Pelegrí-Llopart and Graham's **BURS** theory, and the table
generators that followed it, precompute a finite set of states at the time
the compiler is built; at compile time, labelling a node becomes a table
lookup on its operator and its children's states.[^burs] Proebsting generated
those states with a work queue and trimmed them to keep the tables small, the
method behind the `burg` generator.[^bursauto] [^hjort] The survey calls this **offline cost analysis** and
notes that it is a separate idea from BURS theory, though the two are often
confused.[^hjort]

LLVM keeps its patterns in TableGen `.td` files, from which a generator builds
the matching code of the SelectionDAG selector.[^codegen] As Hjort Blindell's
survey describes it, the generated matcher sorts patterns by decreasing
complexity, then increasing cost, and takes the first that matches: a
maximal munch over DAGs.[^hjort] [E2](e2-describing-a-target.md) writes such
patterns.

## The same tree on x86-64

The x86-64 tiles for the same tree look different. A memory operand has a
base, an index, a scale of 1, 2, 4 or 8, and a displacement.[^gas-mem] `lea`
computes such an address into a register without reading memory and without
changing the flags, so it works as an add with three operands, one of them
scaled.[^fc-lea] Most arithmetic is two-address: the destination is also the
first source, so computing `d = x op y` into a fresh temporary costs a copy
first.[^cmu] `imul` has a three-operand form that multiplies by an immediate,
but there is no integer multiply-add.[^fc-imul]

The x86-64 half of the program above covers the address tree with two tiles
again: `lea t1, [j + 4*i]` for the index, since 4 is a legal scale, and
`mov t2, dword ptr [a + 4*t1]` for the load. LLVM agrees. Compiling
`address.ll` with Apple clang 21 for `x86_64-unknown-linux-gnu` and for
AArch64, with the IR optimizer turned off (`-O2 -Xclang -disable-llvm-passes`)
so that only the back end acts (checked 24 September 2026), gives

```gas
	lea	rax, [rdx + 4*rsi]
	mov	eax, dword ptr [rdi + 4*rax]
```

for x86-64 (Intel syntax), and the same two instructions `llc` printed
earlier for AArch64. The toy selector and LLVM chose the same covers on both
targets.

The program's second tree, `add(mul(i, n), j)` with the stride `n` in a
register, separates the targets: one `madd` on AArch64, three instructions on
x86-64 (a copy, `imul`, then `lea` for the add). The algorithm did not
change. The tile table did. Vortex rarely meets the second tree, because its
array shapes are compile-time constants
([record 11](../decisions/arrays.md#d11)): its strides are always immediates,
powers of two become shifts or scales, and other strides need a multiply by a
constant.

The table is also where memory operands come from. B2's `mulss` that reads
`b[i]` from memory is a tile whose pattern is a multiply with a `load` in one
operand, and only the second operand, because the first is the destination
register. AArch64 has no such tile: its arithmetic reads only registers. And
the table encodes the language's rules as well as the machine's. A tile for
`fadd(fmul(x, y), z)` that emitted a fused multiply-add would round once
instead of twice, which Vortex forbids
([record 56](../decisions/numbers.md#d56)). A Vortex back end leaves that
tile out, whatever the hardware offers.

??? check "For a row length of 16, the index is `add(mul(i, 16), j)`. What does the x86-64 table choose for the whole element load, and why does `lea-sib` not help?"

    16 is not a legal scale, so lea-sib does not match. The cheapest cover
    of the index is `imul t1, i, 16` (1) then `lea t2, [t1 + j]` (1), and
    the load is `mov t3, dword ptr [a + 4*t2]`: three instructions. On
    AArch64 add-shifted still matches, since 16 is a power of two, and the
    index is one `add` with `lsl #4`: two instructions for the element.

## Shared values break trees

Everything so far assumed a tree, where every node has one parent. The IR a
back end receives is rarely a tree. In SSA form a value has one definition but
any number of uses ([O3](../optimize/o3-ssa.md#one-name-one-definition)), so
the expressions of a basic block form a **DAG**, a directed acyclic graph in
which one node can have several parents. Take `c[i] += 1`: the address
`c + (i << 2)` is used twice, by the load and by the store.

A tree selector has two options for a shared node. It can cut the DAG there:
compute the node once into a register, and treat it as a variable in each
tree that uses it. Or it can copy the shared subtree into every parent, and
let each parent's tiles cover it again. Neither is always right.
[Figure 4](#fig-c1-shared) shows both cases, with the listings Apple clang 21
wrote at `-O2` for two small C functions (checked 24 September 2026).

<figure class="vx-figure" id="fig-c1-shared">
<svg viewBox="0 0 760 330" role="img" aria-label="Duplicating or sharing a value with two uses" aria-describedby="c1-shared-desc">
<title>Duplicating or sharing a value with two uses</title>
<desc id="c1-shared-desc">Left: a load and a store both use the address a plus i shifted left by 2. Folding the address into both instructions costs 3 instructions (ldr w8, [a, i, lsl #2]; add w8, w8, #1; str w8, [a, i, lsl #2]) and is marked as the better choice; computing it once into x9 costs 4 and is dashed. Right: two loads, from a and from b, both use the index i times n plus j. Computing it once with madd into x8 costs 3 instructions and is marked better; folding it into both uses needs two madds, 4 instructions, and is dashed.</desc>
<defs><marker id="c1-sh-head" viewBox="0 0 10 10" refX="10" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker></defs>
<rect class="vx-box" x="30" y="20" width="130" height="34" rx="4"/>
<text class="vx-text" x="95" y="42" text-anchor="middle">load c[i]</text>
<rect class="vx-box" x="200" y="20" width="130" height="34" rx="4"/>
<text class="vx-text" x="265" y="42" text-anchor="middle">store c[i]</text>
<rect class="vx-box-strong" x="90" y="100" width="180" height="34" rx="4"/>
<text class="vx-mono" x="180" y="122" text-anchor="middle">c + (i &lt;&lt; 2)</text>
<line class="vx-line" x1="95" y1="54" x2="150" y2="100" marker-end="url(#c1-sh-head)"/>
<line class="vx-line" x1="265" y1="54" x2="210" y2="100" marker-end="url(#c1-sh-head)"/>
<rect class="vx-box-accent" x="10" y="160" width="345" height="72" rx="6"/>
<text class="vx-text" x="22" y="182">Fold into both uses: 3 instructions</text>
<text class="vx-mono" x="22" y="203">ldr w8, [c, i, lsl #2]; add w8, w8, #1</text>
<text class="vx-mono" x="22" y="222">str w8, [c, i, lsl #2]</text>
<rect class="vx-box-bad" x="10" y="245" width="345" height="72" rx="6"/>
<text class="vx-text" x="22" y="267">Compute once into x9: 4 instructions</text>
<text class="vx-mono" x="22" y="288">add x9, c, i, lsl #2; ldr w8, [x9]</text>
<text class="vx-mono" x="22" y="307">add w8, w8, #1; str w8, [x9]</text>
<rect class="vx-box" x="420" y="20" width="130" height="34" rx="4"/>
<text class="vx-text" x="485" y="42" text-anchor="middle">load a[t]</text>
<rect class="vx-box" x="590" y="20" width="130" height="34" rx="4"/>
<text class="vx-text" x="655" y="42" text-anchor="middle">load b[t]</text>
<rect class="vx-box-strong" x="480" y="100" width="180" height="34" rx="4"/>
<text class="vx-mono" x="570" y="122" text-anchor="middle">t = i * n + j</text>
<line class="vx-line" x1="485" y1="54" x2="540" y2="100" marker-end="url(#c1-sh-head)"/>
<line class="vx-line" x1="655" y1="54" x2="600" y2="100" marker-end="url(#c1-sh-head)"/>
<rect class="vx-box-accent" x="390" y="160" width="360" height="72" rx="6"/>
<text class="vx-text" x="402" y="182">Compute once into x8: 3 instructions</text>
<text class="vx-mono" x="402" y="203">madd x8, i, n, j; ldr w9, [a, x8, lsl #2]</text>
<text class="vx-mono" x="402" y="222">ldr w8, [b, x8, lsl #2]</text>
<rect class="vx-box-bad" x="390" y="245" width="360" height="72" rx="6"/>
<text class="vx-text" x="402" y="267">Fold into both uses: 4 instructions</text>
<text class="vx-mono" x="402" y="288">madd x8, i, n, j; ldr w9, [a, x8, lsl #2]</text>
<text class="vx-mono" x="402" y="307">madd x10, i, n, j; ldr w8, [b, x10, lsl #2]</text>
</svg>
<figcaption>Figure 4. Two values, each with two uses. Left: the address of <code>c[i]</code> disappears into each addressing mode for free, so copying it into both uses is cheaper than computing it once; clang's listing for <code>c[i] += 1</code> is the upper box. Right: the index <code>i * n + j</code> needs a <code>madd</code> wherever it is computed, so computing it once is cheaper; clang's listing for <code>a[t] + b[t]</code> uses one <code>madd</code> and two loads. Names stand for the registers clang used.</figcaption>
</figure>

Choosing the best cover of a DAG is much harder than for a tree. Hjort
Blindell's survey states that optimal selection on trees takes linear time,
while on DAGs it is NP-complete, citing Proebsting's 1995 proof and Koes and
Goldstein's reformulation of it.[^hjort]

Koes and Goldstein's NOLTIS algorithm is a practical answer. It first labels the DAG as if it were a tree, ignoring
the sharing. At each shared node it then compares an estimate of the cost of
duplicating the work against the cost of cutting the DAG there, and marks the
node fixed when cutting is cheaper. A second labelling pass lets no tile span
a fixed node, and emission follows. Against an integer-programming solver,
they report optimal selections in 99.7% of their test cases.[^dagcov] [^hjort]

Trees also cannot express an instruction with two results, because a tree
pattern has one root.[^hjort] B2's AArch64 loop used one:
`ldr s1, [x0], #4` produces the loaded value and the advanced pointer. A
comparison that sets flags as well as a result is another. Such instructions
need DAG patterns, or a later rewrite over the selected code, the subject of
[C7](c7-peephole.md).

The last step widens the view from a block to a whole function. Eckstein,
König and Scholz selected over the SSA graph of a function, the graph of all
its SSA values and their uses, and solved the choice as a **partitioned
Boolean quadratic problem** (PBQP), an optimization problem with a cost for
each node's choice and for each pair of neighboring choices.[^hjort] Ebner and
colleagues extended that model to patterns with several roots. The survey
reports that replacing LLVM 2.1's greedy DAG selector with it made code for an
ARMv5 processor 13% faster on average, on a set of selected
programs.[^ssagraph] [^hjort]

That is the survey's ladder of four rungs: macro expansion, tree covering, DAG
covering and graph covering, each looking at more of the program before
choosing an instruction.[^hjort] B1 stood on the first; this chapter built the
second and described the other two.

??? check "In `c[i] += 1`, copying the address into both uses was free. What makes copying a shared value cost something in general?"

    Copying is free only when the whole shared computation disappears into
    each user's tile, as an address does into an addressing mode. If the
    shared node needs an instruction of its own in every copy, like the
    `madd`, each copy pays for it. Copies can also keep the shared node's
    operands (`i`, `n`, `j`) alive longer, so they occupy registers
    longer, a cost that liveness ([C2](c2-liveness.md)) and register
    allocation ([C3](c3-linear-scan.md)) make visible.

## How production compilers select

LLVM has two selectors. **SelectionDAG** builds a DAG for each basic
block.[^globalisel] It then runs eight steps: build the initial DAG, optimize
it, legalize types, optimize, legalize operations, optimize, select
instructions, and schedule.[^codegen] **Legalizing** replaces types and
operations the target does not support with ones it does. Selection
pattern-matches the target's instructions against the legal DAG and produces
a DAG of target instructions.[^codegen]

**GlobalISel** works on a whole function at once, directly in MIR, the
machine-level IR the rest of the code generator uses. Its core passes are
IRTranslator, Legalizer, RegBankSelect and InstructionSelect.[^globalisel]
On the M4 Pro, `llc` 18.1.8 with `-debug-pass=Structure` on `address.ll`
lists those four passes at `-O0`, followed by the SelectionDAG pass "AArch64
Instruction Selection" and the fast register allocator. At `-O2` it lists
only the SelectionDAG pass and the greedy allocator (checked 24 September
2026). A function can still be sent to SelectionDAG at `-O0`: GlobalISel's
documentation mentions a `failedISel` attribute that does this.[^gisel-pipeline]
For `address.ll`, both paths printed the same two instructions.

Cranelift, the code generator in Wasmtime, lowers a function in one pass that
visits instructions in postorder, so that in SSA form it sees every use of a
value before its definition.
It counts the uses of each value as lowering
proceeds and generates no code for a value nothing used.

When lowering an
instruction, a back end may look at the instructions that produce its
operands and merge them in, such as a shift into an address. The merged
instruction must have no side effects, or the same "color" as its user:
colors are assigned in a forward pass and change at every side effect, so
equal colors mean no side effect lies between the two.[^cranelift] Its
patterns are written in ISLE, a typed term-rewriting language compiled ahead
of time into Rust matching code.[^cranelift]

Go's compiler has a `lower` pass that replaces machine-independent SSA
operations with machine-specific ones, and some of its passes are generated
from rewrite rules in `_gen/*.rules`.[^go-ssa] Its AArch64 rule file contains
this chapter's tiles as rewrites: a 4-byte load from `ADDshiftLL [2] ptr idx`
becomes an indexed load that scales by 4, the ldr-shifted tile.[^go-arm64]

These designs differ in how much of the program a pattern sees (a tree, a
block's DAG, a function) and in how the matcher is built (by hand, by a
generator, or from a rewrite language). The tiles they encode are the ones
this chapter drew.

## For Vortex

!!! vortex "Exercise"

    **Build** instruction selection by tiling for the array accesses your
    back end emits, on AArch64 and, if you did [B2](b2-x86-64.md), on
    x86-64.

    1. Write each target's tile table as data before any matcher: for each
       tile, the pattern over your IR, the instruction it emits, its cost,
       and every operand test (the shift amounts a load of each element
       size accepts, the x86-64 scales, the immediate ranges). Next to each
       test, name the manual page it comes from.
    2. Replace [B1](b1-simplest-backend.md)'s templates with a maximal-munch
       selector for loads and stores of array elements and the index
       arithmetic under them. Everything else keeps its B1 template.
    3. Write a second selector over the same table that labels bottom up
       and picks the cheapest cover. Keep it in your test suite as a
       reference for the first.
    4. Handle values with more than one use explicitly. Decide, and write
       down, when your selector computes such a value once and when it
       lets several tiles cover it, and make the rule a test.

    **Not yet:** DAG-wide or whole-function selection, tiles with two
    results such as post-index loads ([C7](c7-peephole.md)), a grammar or
    a generator for the tables ([E2](e2-describing-a-target.md)), and any
    change to where values live: every value still has its stack slot until
    [C3](c3-linear-scan.md). Never add a tile that fuses a floating-point
    multiply with an add ([record 56](../decisions/numbers.md#d56)).

    **Proof that it works:**

    - B1's differential test passes on your whole suite, on every runner,
      with the new selector turned on.
    - A property test generates random index trees (seeded, so a failure
      can be replayed) and checks two things for each: the bottom-up
      selector's cover never costs more than the munch cover, and both
      covers compute the same value as a direct evaluation of the tree.
    - The listing your compiler emits for A2's `cell` function contains a
      register-offset load with `lsl #2`, and no separate shift.
    - A measurement for the stage 10 kernel, filled in from your own
      compiler with its version, the machine and the date:

      | Inner-loop instructions per iteration | AArch64 | x86-64 |
      | --- | --- | --- |
      | B1 templates | | |
      | Maximal munch | | |
      | Bottom-up cover | | |

## Key ideas

!!! recap "You can now answer"

    - **What is a tile?** A tree pattern with register and immediate holes, the instruction that computes it, and a cost.
    - **What is a cover, and what is macro expansion in these terms?** A set of tiles in which every node belongs to exactly one tile; macro expansion is the cover made only of one-node tiles.
    - **How can maximal munch lose an instruction?** It picks the largest tile at each node without seeing what that tile leaves below, as madd left the constant 4 needing a `mov`.
    - **Why is bottom-up labelling optimal on a tree?** Subtrees share nothing, so the cheapest cover of the tree is built from the cheapest covers of its subtrees, each computed once.
    - **Why does the same algorithm pick different instructions on x86-64?** The tile table differs: scales of 1, 2, 4 and 8, `lea` as a three-operand add, two-address arithmetic and no integer multiply-add.
    - **What does a shared value force a selector to decide?** Whether to compute it once into a register or copy it into each use; the best choice depends on whether the copies cost instructions, and finding the optimal cover of a DAG is NP-complete.

## Where this comes back

!!! next "You will use this again in"

    - [C2. Liveness](c2-liveness.md): *temporaries*, *values with several uses*
    - [C3. Register allocation I: linear scan](c3-linear-scan.md): *temporaries mapped to registers*
    - [C6. Instruction scheduling](c6-scheduling.md): *costs that are not additive*
    - [C7. Peephole optimization](c7-peephole.md): *post-index addressing*, *repairs after selection*
    - [E1. The LLVM code generator pipeline](e1-llvm-codegen-pipeline.md): *SelectionDAG*, *GlobalISel*
    - [E2. Describing a target](e2-describing-a-target.md): *patterns as tiles*
    - [D3. Reading real back ends](d3-real-backends.md): *ISLE*, *rewrite rules*

## Sources and further reading

Pfenning's 15-411 notes are the shortest complete introduction: maximal munch,
optimal selection and the x86-64 two-address problem in five pages.[^cmu]
Appel's *Modern Compiler Implementation* covers maximal munch and dynamic
programming in the same order as this chapter.[^appel] Hjort Blindell's
survey is the map of the whole field, from macro expansion to graph covering,
and is free; read its chapters on tree and DAG covering after this
one.[^hjort] Fallin's Cranelift posts show a production selector from the
inside, and Go's rule files show the tiles of a real compiler as plain
rewrites.[^cranelift] [^go-arm64]

[^cmu]: Frank Pfenning, "Lecture Notes on Instruction Selection", 15-411 Compiler Design, Carnegie Mellon University, lecture 2, 29 August 2013: sections 4 (maximal munch), 5 (optimal instruction selection) and 6 (x86-64 considerations). <https://www.cs.cmu.edu/~fp/courses/15411-f13/lectures/02-instsel.pdf>
[^hjort]: Gabriel Hjort Blindell, "Survey on Instruction Selection: An Extensive and Modern Literature Study", arXiv:1306.4898: section 3.6 (Twig, Iburg, BURS and offline cost analysis), the LLVM description in chapter 4, section 4.2 ("Optimal pattern selection on DAGs is NP-complete"), the Koes and Goldstein discussion, and section 5.3.3 (PBQP-based techniques). <https://arxiv.org/abs/1306.4898>
[^aho]: Alfred V. Aho, Mahadevan Ganapathi and Steven W. K. Tjiang, "Code generation using tree matching and dynamic programming", *ACM Transactions on Programming Languages and Systems* 11(4), 1989. <https://doi.org/10.1145/69558.75700>
[^iburg]: Christopher W. Fraser, David R. Hanson and Todd A. Proebsting, "Engineering a simple, efficient code-generator generator", *ACM Letters on Programming Languages and Systems* 1(3), 1992, and the `iburg` source. <https://doi.org/10.1145/151640.151642> ; <https://github.com/drh/iburg>
[^burs]: Eduardo Pelegrí-Llopart and Susan L. Graham, "Optimal code generation for expression trees: an application of BURS theory", *POPL 1988*. <https://doi.org/10.1145/73560.73586>
[^bursauto]: Todd A. Proebsting, "BURS automata generation", *ACM Transactions on Programming Languages and Systems* 17(3), 1995. <https://doi.org/10.1145/203095.203098>
[^dagcov]: David Ryan Koes and Seth Copen Goldstein, "Near-optimal instruction selection on DAGs", *CGO 2008*. <https://doi.org/10.1145/1356058.1356065>
[^ssagraph]: Dietmar Ebner, Florian Brandner, Bernhard Scholz, Andreas Krall, Peter Wiedermann and Albrecht Kadlec, "Generalized instruction selection using SSA-graphs", *LCTES 2008*. <https://doi.org/10.1145/1375657.1375663>
[^a64-ldr-reg]: Arm, DDI 0602 (2026-06), "LDR (register)". <https://developer.arm.com/documentation/ddi0602/2026-06/Base-Instructions/LDR--register---Load-register--register-->
[^a64-add-sr]: Arm, "A64 Instruction Set Architecture for A-profile architecture" (DDI 0602), "ADD (shifted register)". <https://developer.arm.com/documentation/ddi0602/latest>
[^a64-madd]: Arm, DDI 0602 (2026-06), "MADD". <https://developer.arm.com/documentation/ddi0602/2026-06/Base-Instructions/MADD--Multiply-add->
[^a64-mov]: Arm, DDI 0602 (2026-06), "MOV (wide immediate)", an alias of MOVZ. <https://developer.arm.com/documentation/ddi0602/2026-06/Base-Instructions/MOV--wide-immediate---Move-wide-immediate-value--an-alias-of-MOVZ->
[^gas-mem]: GNU Binutils, *Using as*, section "i386-Memory: Memory References". <https://sourceware.org/binutils/docs/as/i386_002dMemory.html>
[^fc-lea]: Felix Cloutier, "LEA: Load Effective Address", x86 and amd64 instruction reference (unofficial, derived from the Intel SDM): "Flags Affected: None". <https://www.felixcloutier.com/x86/lea>
[^fc-imul]: Felix Cloutier, "IMUL: Signed Multiply": the three-operand form `IMUL r64, r/m64, imm32`. <https://www.felixcloutier.com/x86/imul>
[^codegen]: LLVM Project, "The LLVM Target-Independent Code Generator": "SelectionDAG Instruction Selection Process" and "SelectionDAG Select Phase". <https://llvm.org/docs/CodeGenerator.html>
[^globalisel]: LLVM Project, "Global Instruction Selection": SelectionDAG operates on individual basic blocks, GlobalISel on the whole function, in MIR; the core pipeline passes. <https://llvm.org/docs/GlobalISel/index.html>
[^gisel-pipeline]: LLVM Project, "Core Pipeline", GlobalISel documentation: the four passes and the `failedISel` attribute. <https://llvm.org/docs/GlobalISel/Pipeline.html>
[^cranelift]: Chris Fallin, "Cranelift's Instruction Selector, Part 1: Pattern-matching" (18 September 2020) and "Cranelift's ISLE, Part 4: The ISLE Language" (20 January 2023). <https://cfallin.org/blog/2020/09/18/cranelift-isel-1/> ; <https://cfallin.org/blog/2023/01/20/cranelift-isle/>
[^go-ssa]: The Go Authors, "SSA Backend", `cmd/compile/internal/ssa/README.md`: the `lower` pass, and passes generated from rewrite rules. <https://github.com/golang/go/blob/master/src/cmd/compile/internal/ssa/README.md>
[^go-arm64]: The Go Authors, `cmd/compile/internal/ssa/_gen/ARM64.rules`: the rules that rewrite `MADD` with a power-of-two constant into `ADDshiftLL`, and a load from `ADDshiftLL [2]` into `MOVWloadidx4`. <https://github.com/golang/go/blob/master/src/cmd/compile/internal/ssa/_gen/ARM64.rules>
[^appel]: Andrew W. Appel, *Modern Compiler Implementation*, Cambridge University Press, 1998: chapter "Instruction Selection". <https://www.cs.princeton.edu/~appel/modern/toc.html>

# D3. Reading real back ends

<p class="page-intro">Five small back ends that you can read: QBE, Go's compiler, Cranelift, TCC and chibicc. For each one this chapter asks what it uses as its IR, how it selects instructions, how it allocates registers and what it hands to the rest of the toolchain, and it shows you how to check those answers against the compiler's own dumps and source. Reading a back end you did not write is how you borrow a good idea for Vortex without adopting a production compiler whole.</p>

<p class="vx-meta" markdown="1">Level: Advanced · Reading time: about 45 minutes · Builds on: [C1. Instruction selection](c1-instruction-selection.md), [C2. Liveness](c2-liveness.md), [C3. Register allocation I: linear scan](c3-linear-scan.md), [C4. Register allocation II: graphs and SSA](c4-graph-coloring.md), [C5. Spilling, splitting and rematerialization](c5-spilling.md)</p>

???+ remember "Before you start, remember"

    ??? question "Why can a tree-based selector not fold a shared value into every instruction that uses it?"

        Folding puts the computation inside the consuming instruction; no
        register ever holds the result. A second consumer then has nothing to
        read unless the value is also computed on its own, so the selector
        has to choose between computing it once and repeating it at each use.

        Introduced in [C1. Instruction selection](c1-instruction-selection.md#shared-values-break-trees).

    ??? question "What does plain linear scan lose by giving each value one interval?"

        The holes. An interval runs from a value's first definition to its
        last use, so the allocator reserves a register across stretches where
        the value is dead, and spills more than an allocator with exact
        liveness would.

        Introduced in [C3. Register allocation I: linear scan](c3-linear-scan.md#live-intervals-one-range-instead-of-a-graph).

    ??? question "Why can an allocator for SSA-form code decide its spills before it assigns any register?"

        The interference graph of an SSA program is chordal. Once spilling
        has brought the number of live values down to the number of
        registers at every point, a greedy pass in the right order colors
        the rest without ever getting stuck.

        Introduced in [C5. Spilling, splitting and rematerialization](c5-spilling.md#deciding-spills-before-assigning-registers).

    ??? question "How can you check an allocation without trusting the allocator that produced it?"

        With a second, much simpler program that only checks the answer: for
        interval allocation, that no two intervals given the same register
        overlap. Run it on every allocation the allocator makes and treat a
        failure as a bug in the allocator.

        Introduced in [C3. Register allocation I: linear scan](c3-linear-scan.md#checking-an-allocation-independently).

!!! goals "In this chapter"

    - Answer four questions about any back end (its IR, its instruction selection, its register allocation, its output) from its own documentation and source, and say plainly where they are silent.
    - Trace a small function through the Go compiler's per-pass dumps and name the pass responsible for each change.
    - Explain how Cranelift's backward lowering decides whether a folded instruction is still emitted, and how regalloc2 chooses between taking a register, evicting and splitting.
    - Compare QBE's split spiller, Go's allocator and TCC's three-register value stack with the linear scan of C3.
    - Judge whether a source is current enough to build on before you cite it or copy its design.

Chapters C1 to C5 each taught one algorithm, built by hand on a small
example. Real back ends combine several of them, drop others, and add
engineering that no textbook covers. They are also large, and a first
reading of one usually stalls in the source tree. This chapter reads five
small ones in a fixed order: documentation first, then the compiler's own
dumps, then the source for the single file that answers a remaining
question. Each section ends with an example that models the back end's
central idea in a page of C++.

## Four questions for any back end

Start with what QBE's documentation says about itself, reduced to plain
facts. QBE reads a textual, SSA-based **intermediate language** (IL) written by
some front end. It runs a fixed list of optimizations. It
targets amd64, arm64 and riscv64, and its output is assembly, which a
standard assembler and linker turn into a program[^qbe-home][^qbe-il].

Those facts already answer part of four questions, which this chapter asks
of every back end:

1. **What is the IR?** The representation the back end works on, and
   whether it changes form on the way down.
2. **How does it select instructions?** One template per operation, tree
   patterns, a DAG or something else, and where the patterns are written.
3. **How does it allocate registers?** Which algorithm, when it spills, and
   how it knows the result is right.
4. **Where does its output meet the rest of the toolchain?** Assembly text,
   an object file, or machine code in memory, and which calling convention
   the code follows.

These facts answer questions 1 and 4; QBE's feature list and comparison
page fill in 2 and 3. Some answers will not be in any
document. When a source does not say, write "not documented" rather than
guessing from the name of a file: a guess repeated in your notes becomes a
fact you never checked.

The order matters too. Documentation tells you what the authors intended.
A **pass dump**, the compiler's IR printed after each step, shows what
happened to one function. The source answers the questions that remain. Reading in that order means you open the source knowing which
question you want it to answer.

## QBE: small on purpose

QBE's home page states its goal as 70% of the performance of industrial
optimizing compilers in 10% of the code[^qbe-home]. Its comparison page
adds that the project is, and will remain, under 8,000 lines of C99 with
no dependencies, and that it can dump its IL in one format after every
pass[^qbe-vs-llvm]. The comparison page is written by QBE's author, who calls it
biased[^qbe-vs-llvm]. Treat the 70%
as a design target, not a measurement.

### The IR: text, with temporaries instead of variables

A front end writes QBE's IL as text, usually one file per compilation
unit[^qbe-il]. The IL has an unlimited supply of **temporaries**, named
values such as `%n`, so a front end never has to think about registers. Here is a
function that counts the spaces in a zero-terminated string, written by hand
for this chapter:

```text
export function w $count_spaces(l %str) {
@start
        %p =l copy %str
        %n =w copy 0
@loop
        %c =w loadub %p
        jnz %c, @body, @done
@body
        %sp =w ceqw %c, 32
        %n =w add %n, %sp
        %p =l add %p, 1
        jmp @loop
@done
        ret %n
}
```

`w` and `l` are 32- and 64-bit integers, `loadub` loads an unsigned byte,
`ceqw` compares two words and yields 1 or 0, and `jnz` branches on a nonzero
value[^qbe-il]. `%n` and `%p` are assigned twice, so this is not SSA form.
QBE accepts it anyway: its IL reference says that, unlike LLVM, QBE can fix
up a program that is not in SSA form without the front end first moving
variables into memory[^qbe-il]. The comparison page shows the cost it
avoids: in LLVM IR, one increment of a variable held in a stack slot is a
load, an add and a store, where QBE needs one instruction[^qbe-vs-llvm].

Finish the example in your head before reading on. After QBE builds SSA,
`@loop` has two predecessors, `@start` and `@body`. Which temporaries need
a phi at the top of `@loop`, and which value does each phi take from each
predecessor? (Two phis: one for `%n`, taking 0 from `@start` and the sum
from `@body`, and one for `%p`, taking `%str` from `@start` and the
incremented pointer from `@body`. `%c` and `%sp` are defined once per trip
and need none.) This is the construction [O3](../optimize/o3-ssa.md) taught,
done by the back end instead of the front end.

### Selection, allocation and output

QBE's feature list gives the rest of the answers[^qbe-home]. The same IL is
used at every compilation stage, so selection rewrites IL instructions into
target-specific ones in place rather than building a new representation.
The only selection feature the page names is matching amd64 addressing
modes. The optimizations are copy elimination, sparse conditional constant
propagation, dead instruction elimination and turning small stack slots
into temporaries.

Register allocation is the most instructive part. The page lists a spiller
and a register allocator kept separate "thanks to SSA form", a spilling
heuristic based on loop analysis, and a linear allocator with **hinting**,
meaning it prefers a register that a related value already uses so that a
copy between them disappears[^qbe-home]. It calls the design simpler and
faster than graph coloring. That is the separation
[C5](c5-spilling.md#deciding-spills-before-assigning-registers) derived
from Hack's result: once the spiller guarantees that no point has more live
values than registers, SSA form guarantees that a single greedy pass, in
the right order, can assign them all.

The output is assembly text for the system toolchain; the home page's
getting-started line is `qbe -o out.s file.ssa && cc out.s`[^qbe-home].
QBE implements the C calling convention in full, so QBE code and C code
can call each other[^qbe-home]. Apple's arm64 variant became a separate
target, `arm64_apple`, in QBE 1.1, and the current release is 1.3, dated
June 2026[^qbe-releases]. [B1](b1-simplest-backend.md) made the same choice
of output for Vortex's first native back end.

??? check "QBE's allocator makes one linear pass and never spills. Which earlier pass makes that safe, and what can it not know when it runs?"

    The spiller. It runs first and spills until, at every point, no more
    values are live than there are registers; SSA form then guarantees the
    linear pass can always find a free register. What the spiller cannot
    know is which register each value will end up in, so it cannot tell
    whether a later move, or a hint the allocator fails to honor, will cost
    an extra copy. It decides on counts, not on assignments.

## Go: a back end that shows its work

Go's compiler is far larger than QBE, but its SSA back end has a short
`README` and a debugging switch that prints every pass. That makes it the
best place in this chapter to watch a back end work on your own input.

### Values, memory and a list of passes

The Go back end's IR is SSA. A **value** has an operator, a type and
arguments; a **block** is a basic block whose kind (`plain`, `if`, `exit`)
says how it ends[^go-ssa]. One idea is worth noticing before reading any
dump: memory is itself a value, of type `memory`. A store takes the current
memory state as an argument and produces a new one, and a load takes a
memory state too. Ordering between memory operations then falls out of
ordinary data dependences, with no separate list of which store must come
before which load[^go-ssa].

Passes run one function at a time, in a fixed order, each exactly once by
default. The `lower` pass is the special one: it replaces
machine-independent operators with machine-specific ones[^go-ssa]. Several
passes, `lower` among them, are not written directly in Go. They are
generated from **rewrite rules**, pattern-and-replacement pairs kept in
files under `_gen/`, one set per architecture (`ARM64.rules`,
`AMD64.rules` and so on)[^go-ssa][^go-rules]. A shared driver applies a
pass's rules to every value and block and, in the words of its own comment,
repeats "until we find no more rewrites"[^go-rewrite].

### A worked trace: one array element, pass by pass

Here is a four-line Go function, written for this chapter. It reads one
element of a 64-element array through a pointer, with the index masked so
that it is always in range:

```go
package at

func At(p *[64]int32, i int) int32 {
	return p[i&63]
}
```

Running `GOSSAFUNC='At+' go build` prints the function after every pass
and then its final assembly; without the `+`, the same content goes to an
`ssa.html` file you open in a browser[^go-ssa]. The listings below come from
that run with Go 1.26.5 on this machine (an Apple M4 Pro, macOS 27) in
September 2026, trimmed to the interesting lines, with source positions and
debug names removed.

The first thing printed is the front end's tree for the function, and one
word in it already answers a question: the indexing node is marked
`INDEX Bounded`. The front end has proved that `i&63` is always between 0
and 63, so no bounds check will be generated. After the machine-independent
optimizations, the body is:

```text
v11 = Const64 <int> [63]
v10 = NilCheck <*[64]int32> v7 v1
v12 = And64 <int> v8 v11
v6 = Const64 <uint64> [2]
v13 = Lsh64x64 <int> [true] v12 v6
v14 = AddPtr <*int32> v10 v13
v15 = Load <int32> v14 v1
```

`v7` is `p`, `v8` is `i` and `v1` is the initial memory state. Read it as
arithmetic: mask the index, shift it left by 2 (multiply by 4, the size of
an `int32`), add it to the checked pointer, and load. After `lower`, the
same function reads:

```text
v10 = LoweredNilCheck <*[64]int32> v7 v1
v12 = ANDconst <int> [63] v8
v15 = MOVWloadidx4 <int32> v10 v12 v1
```

Seven values became three. `And64` with a constant operand became
`ANDconst`, which carries the 63 as an immediate. The shift, the pointer add
and the load became one `MOVWloadidx4`: a 32-bit load whose index is scaled
by 4. The two constants are gone because nothing uses them any more. This
is the addressing-mode tile [C1](c1-instruction-selection.md#the-same-tree-on-x86-64)
built by hand, produced here by rules in `ARM64.rules`. The file reaches
`MOVWloadidx4` by more than one route (for example, from a load whose
address is a shifted add, and from an indexed load whose index is a shift),
which is one reason the driver repeats until nothing changes[^go-rules].

After register allocation the three values have registers:

```text
v12 = ANDconst <int> [63] v8 : R1
v15 = MOVWloadidx4 <int32> v7 v12 v1 : R0
```

and the final assembly is:

```text
MOVB    (R0), R27
AND     $63, R1, R1
MOVW    (R0)(R1<<2), R0
RET
```

`p` arrived in `R0` and `i` in `R1`, because Go's internal calling
convention passes integer arguments in `R0` to `R15` on arm64[^go-abi].
`(R0)(R1<<2)` is the Go assembler's spelling of the register-offset
addressing mode that [A2](a2-aarch64-assembly.md) wrote as
`[x0, x1, lsl #2]`. The first line is the nil check: `LoweredNilCheck` is
defined to fault if its pointer is nil[^go-ops], and loading one byte
through `R0` into `R27`, which nothing then reads, is how this target gets
that fault.

<figure class="vx-figure" id="fig-go-lower">
<svg viewBox="0 0 760 330" role="img" aria-label="The Go compiler lowering one array read. Before lower, seven generic values: Const64 63, NilCheck, And64, Const64 2, Lsh64x64, AddPtr and Load. After lower, three machine values: LoweredNilCheck, ANDconst with the 63 folded in, and MOVWloadidx4, which covers the shift, the pointer add and the load. After register allocation, the final instructions are AND $63, R1, R1 and MOVW (R0)(R1&lt;&lt;2), R0.">
<text class="vx-text-muted" x="130" y="20" text-anchor="middle">before lower: generic SSA</text>
<text class="vx-text-muted" x="440" y="20" text-anchor="middle">after lower</text>
<text class="vx-text-muted" x="650" y="20" text-anchor="middle">final assembly</text>
<rect class="vx-box" x="20" y="34" width="220" height="30" rx="4"/>
<text class="vx-mono" x="30" y="54">v10 = NilCheck v7 v1</text>
<rect class="vx-box" x="20" y="74" width="220" height="30" rx="4"/>
<text class="vx-mono" x="30" y="94">v11 = Const64 [63]</text>
<rect class="vx-box" x="20" y="114" width="220" height="30" rx="4"/>
<text class="vx-mono" x="30" y="134">v12 = And64 v8 v11</text>
<rect class="vx-box" x="20" y="164" width="220" height="30" rx="4"/>
<text class="vx-mono" x="30" y="184">v6 = Const64 [2]</text>
<rect class="vx-box" x="20" y="204" width="220" height="30" rx="4"/>
<text class="vx-mono" x="30" y="224">v13 = Lsh64x64 v12 v6</text>
<rect class="vx-box" x="20" y="244" width="220" height="30" rx="4"/>
<text class="vx-mono" x="30" y="264">v14 = AddPtr v10 v13</text>
<rect class="vx-box" x="20" y="284" width="220" height="30" rx="4"/>
<text class="vx-mono" x="30" y="304">v15 = Load v14 v1</text>
<g class="vx-seq" style="--vx-i: 0; --vx-n: 3">
<line class="vx-line" x1="240" y1="49" x2="330" y2="49"/>
<polygon class="vx-arrowhead" points="330,44 340,49 330,54"/>
<rect class="vx-box" x="340" y="34" width="200" height="30" rx="4"/>
<text class="vx-mono" x="350" y="54">LoweredNilCheck</text>
</g>
<g class="vx-seq" style="--vx-i: 1; --vx-n: 3">
<line class="vx-line" x1="240" y1="89" x2="300" y2="112"/>
<line class="vx-line" x1="240" y1="129" x2="300" y2="112"/>
<line class="vx-line" x1="300" y1="112" x2="330" y2="112"/>
<polygon class="vx-arrowhead" points="330,107 340,112 330,117"/>
<rect class="vx-box-accent" x="340" y="97" width="200" height="30" rx="4"/>
<text class="vx-mono" x="350" y="117">ANDconst [63] v8</text>
<line class="vx-line" x1="540" y1="112" x2="560" y2="112"/>
<polygon class="vx-arrowhead" points="560,107 570,112 560,117"/>
<rect class="vx-box-strong" x="570" y="97" width="180" height="30" rx="4"/>
<text class="vx-mono" x="580" y="117">AND $63, R1, R1</text>
</g>
<g class="vx-seq" style="--vx-i: 2; --vx-n: 3">
<line class="vx-line" x1="240" y1="179" x2="300" y2="239"/>
<line class="vx-line" x1="240" y1="219" x2="300" y2="239"/>
<line class="vx-line" x1="240" y1="259" x2="300" y2="239"/>
<line class="vx-line" x1="240" y1="299" x2="300" y2="239"/>
<line class="vx-line" x1="300" y1="239" x2="330" y2="239"/>
<polygon class="vx-arrowhead" points="330,234 340,239 330,244"/>
<rect class="vx-box-accent" x="340" y="224" width="200" height="30" rx="4"/>
<text class="vx-mono" x="350" y="244">MOVWloadidx4</text>
<line class="vx-line" x1="540" y1="239" x2="560" y2="239"/>
<polygon class="vx-arrowhead" points="560,234 570,239 560,244"/>
<rect class="vx-box-strong" x="570" y="224" width="180" height="30" rx="4"/>
<text class="vx-mono" x="580" y="244">MOVW (R0)(R1&lt;&lt;2), R0</text>
<text class="vx-text-muted" x="440" y="280" text-anchor="middle">shift, add and load: one tile</text>
</g>
</svg>
<figcaption>Figure 1. What Go's <code>lower</code> pass did to <code>p[i&amp;63]</code> on arm64 (Go 1.26.5). An operation with a constant operand became an immediate form, and four generic values (a constant, a shift, a pointer add and a load) became one scaled-index load. Register allocation then only had to find registers for three values.</figcaption>
</figure>

Now remove the mask and change the body to `return p[i]`. The front end can
no longer prove the index is in range, and the final assembly on the same
machine gains `CMP $64, R1` and a `BHS` that branches to a call of
`runtime.panicBounds`. One unsigned comparison rejects negative indexes and
indexes of 64 or more together, the check [A2](a2-aarch64-assembly.md)
built by hand.

??? check "In the lowered dump, the constant 2 that fed the shift has disappeared. Where did its information go, and which pass removed the value itself?"

    Into the operator. `MOVWloadidx4` scales its index by 4 as part of its
    addressing mode, so the shift amount is implied by the choice of
    instruction and no value needs to hold it. With no users left, the
    constant is dead, and it is already missing from the dump printed
    after `lower` itself: the rewrite driver, as `lower` calls it, deletes
    values that become dead while it rewrites.

### Rules and the driver that applies them

A rewrite rule looks only at a small pattern around one value. The driver
is what makes a table of such rules behave like a pass: it tries every rule
at every value, and when any rule fires it sweeps again, because a
replacement can expose a pattern that was not there before. The program
below is a six-rule table for a toy expression tree and a driver that
sweeps until nothing changes. Its last rule moves a lone constant to the
right-hand side of an operator, which is where the identity rules look
for it:

--8<-- "includes/examples/backend/d3-real-backends/rule_table_rewrite.cpp.md"

The second expression needs two sweeps that change something and a third
that confirms nothing is left. The first sweep only moves the constants;
the identities they expose fire on the next one. Nobody writing the table
had to know that ordering in advance. That is what a fixpoint driver buys,
and it is also its cost: two rules that undo each other would loop forever,
which is why Go's driver turns on cycle detection after a number of
iterations[^go-rewrite].

### Allocation and output

Go's register allocator is described in the comment at the top of
`regalloc.go`[^go-regalloc]. It is a version of linear scan that treats the
whole function as one long basic block and walks through it greedily. It
moves a value into a register immediately before it is used, spills only
when it must, and then spills the value whose next use is farthest in the future.

Where two paths join, it adds moves on the incoming edges to put each value
where the join block expects it. That needs every edge to have a place for
the moves, so the allocator requires that no edge runs from a block with
several successors to a block with several predecessors; the pass that
splits such **critical edges** runs shortly before allocation[^go-regalloc][^go-compile].

Compare it with [C3](c3-linear-scan.md#the-algorithm-on-eight-intervals-and-three-registers).
C3 sorted intervals by start and, when out of registers, spilled the
interval that ends last. Go walks instructions and spills the value needed
last. Both bet that the value you will not need for longest is the cheapest
to move out of the way; Go's version makes that bet with next-use distances
rather than interval ends.

The output goes to Go's own assembler, whose syntax follows the Plan 9
assemblers rather than the platform's, and into Go's own object
files[^go-asm]. The code follows Go's internal calling convention,
ABIInternal, whose specification says plainly that it is unstable and will
change between Go versions[^go-abi]. Contrast
[A4](a4-calling-conventions.md): AAPCS64 is a contract every compiler on the platform keeps, so a C function compiled
today can call one compiled ten years ago. Go can change its convention
because it controls every caller and callee that uses it; where Go code
meets hand-written assembly, which uses a separate, stable convention called
ABI0, the toolchain inserts wrappers between the two[^go-abi].

## Cranelift: a back end on a compile-time budget

Cranelift generates code for the Wasmtime WebAssembly runtime, among
other users, and its design treats compile time as a cost to keep down,
next to the speed of the code it produces[^cl-isel1][^cl-ra2]. That is the
budget of a compiler that may run while a program waits, the setting [D2](d2-jit.md) describes.

Its design is documented in a series of posts by Chris Fallin, which are
the best first reading: instruction selection first, then the allocation
checker, then the allocator, then the rule language[^cl-isel1][^cl-check][^cl-ra2][^cl-isle].

### Two IRs, and a lowering pass that walks backward

Cranelift's input, **CLIF**, is an SSA IR in which a block takes
**block parameters** where textbook SSA would put phi nodes[^cl-isel1].
Lowering produces **VCode**, short for "virtual-register code": a linear
sequence of machine instructions whose operands are still virtual
registers. VCode is deliberately not SSA, so a lowering may write a
destination register more than once, and it is built in one pass in
near-final order rather than edited afterwards[^cl-isel1].

Selection happens during that one pass. At each CLIF instruction, the
back end's lowering function looks up the tree of producers behind each
operand and decides what to absorb: a shift into an add's second operand,
an add of a constant into a load's address, a constant into an
immediate[^cl-isel1]. Absorbing a producer creates the problem
[C1](c1-instruction-selection.md#shared-values-break-trees) described: the
producer may have other consumers, and if none of them needs its result in
a register, it should not be computed at all.

Cranelift's answer is a **register-use count** per value, maintained during
lowering. It starts at zero. Each time a lowering reads a value from a
register, as opposed to absorbing its producer or using its constant, the
count goes up. The pass walks the function backward (in postorder), so every
consumer of a value is lowered before the value's own instruction is
reached. An instruction is lowered only if one of its results has a nonzero
count or it has a **side effect**, such as a store, a trap or a call, that
must happen whether or not anything reads it[^cl-isel1].

The program below models that pass on three versions of the same six-line
function. In each, `v2` shifts an index and `v3` adds it to a base, and a
load reads through the result:

--8<-- "includes/examples/backend/d3-real-backends/fold_by_use_count.cpp.md"

Read the three outputs as a walk-through. In the first, the load absorbs
both the add and the shift, so neither has a register use and neither is
emitted. In the second, `v2` has two consumers, and both absorb it: two
uses, zero register uses, still not emitted. In the third, a multiply
cannot absorb a shift, so it reads `v2` from a register; now `v2` is
computed once, and the load repeats the shift inside its addressing mode at
no extra cost. The count that matters is not how many consumers a value
has, but how many of them could not absorb it.

The post adds one more rule the toy leaves out. A producer may be absorbed
into its consumer only if doing so does not move it across another side
effect, which the pass checks by giving instructions a color that changes
at every side effect and every new block[^cl-isel1]. Without that rule, a
load could be folded past a store to the same address.

The lowering functions were first written by hand in Rust. Cranelift now
writes them in **ISLE**, a small, strongly typed language of rewrite rules
whose compiler turns them into Rust code ahead of time. A rule's left-hand
side matches through "extractors" that call ordinary Rust functions, so the
rules can query the lowering context without ISLE knowing anything about
Cranelift's IR, and the compiler checks rules for overlaps[^cl-isle]. ISLE
and Go's `.rules` files solve the same problem: patterns written as data,
turned into matching code by a generator.

??? check "A shift `v = x << 2` has three consumers: a load that absorbs it into its address, an add that absorbs it as a shifted operand, and a store that writes `v` itself to memory. How many times is the shift computed, and where?"

    Once, into a register, for the store: a store needs the value it
    writes in a register, so that consumer raises the register-use count
    to one. The load and the add still absorb `v` and repeat its shift
    inside their own operands, which costs nothing extra on AArch64.

### regalloc2: bundles, eviction and splitting

Cranelift's allocator, regalloc2, began as a port of IonMonkey's register allocator,
written in C++, to Rust, and grew its own design from there[^cl-ra2]. Its unit of work is a **bundle**: a group of live ranges
that should share one location. Ranges are merged into a bundle across a
block parameter, across a move and between an instruction's input and an
output that must reuse its register, as long as no two of them
overlap[^cl-ra2]. Allocating the whole bundle at once removes the moves
between its parts and means fewer decisions to make.

Each bundle has a **priority**, the total length of its ranges, and a
**spill weight**, the sum of its uses' weights (uses inside loops weigh
more) divided by its length[^ra2-ion]. The allocator keeps a queue ordered
by priority, so large bundles go first, while the registers are still
empty. For each bundle it tries three things in order[^cl-ra2]:

1. Take a register in which nothing already allocated overlaps the bundle.
2. Otherwise, **evict**: pick a register whose overlapping bundles all
   weigh less, send them back to the queue, and take it.
3. Otherwise, split the bundle at the first point where it conflicts, give
   the first piece a hint for that register, and put both pieces back in
   the queue.

Eviction is what makes regalloc2 a **backtracking** allocator: it can undo
earlier decisions. Its design document explains why it still finishes:
bundles only get smaller, a bundle evicts others only when it is strictly
heavier, and a minimal bundle, one that covers a single instruction, is
heavy enough to evict any larger bundle[^ra2-ion]. (The 2022 post describes how
weight changes on splitting differently; this chapter and its example follow
the design document.) The program below runs that loop on four bundles with
two registers:

--8<-- "includes/examples/backend/d3-real-backends/bundle_evict_split.cpp.md"

<figure class="vx-figure" id="fig-ra2">
<div class="vx-stepper">
<div class="vx-step">
<p><strong>Step 1. Take.</strong> A (length 20) and C (length 12) are the largest bundles, so they go first, and each finds an empty register.</p>
<svg viewBox="0 0 760 170" role="img" aria-label="Step 1: bundle A, from position 0 to 20, takes register r0; bundle C, from 4 to 16, takes register r1. The stack row is empty.">
<text class="vx-text-muted" x="20" y="56">r0</text>
<text class="vx-text-muted" x="20" y="96">r1</text>
<text class="vx-text-muted" x="20" y="136">stack</text>
<line class="vx-line" x1="80" y1="160" x2="680" y2="160"/>
<text class="vx-text-muted" x="80" y="20" text-anchor="middle">0</text>
<text class="vx-text-muted" x="200" y="20" text-anchor="middle">4</text>
<text class="vx-text-muted" x="380" y="20" text-anchor="middle">10</text>
<text class="vx-text-muted" x="680" y="20" text-anchor="middle">20</text>
<rect class="vx-box-accent" x="80" y="38" width="600" height="26" rx="4"/>
<text class="vx-mono" x="380" y="56" text-anchor="middle">A  weight 2/20</text>
<rect class="vx-box-accent" x="200" y="78" width="360" height="26" rx="4"/>
<text class="vx-mono" x="380" y="96" text-anchor="middle">C  weight 2/12</text>
</svg>
</div>
<div class="vx-step">
<p><strong>Step 2. Evict.</strong> B, from 2 to 12, is a loop body with six weighted uses. It overlaps A in r0 and C in r1, but it outweighs both, so it evicts A and takes r0. A goes back into the queue.</p>
<svg viewBox="0 0 760 170" role="img" aria-label="Step 2: bundle B, from 2 to 12 with weight 24 over 10, evicts A from register r0 and takes it. C stays in r1. A is back in the queue.">
<text class="vx-text-muted" x="20" y="56">r0</text>
<text class="vx-text-muted" x="20" y="96">r1</text>
<text class="vx-text-muted" x="20" y="136">queue</text>
<line class="vx-line" x1="80" y1="160" x2="680" y2="160"/>
<text class="vx-text-muted" x="80" y="20" text-anchor="middle">0</text>
<text class="vx-text-muted" x="140" y="20" text-anchor="middle">2</text>
<text class="vx-text-muted" x="440" y="20" text-anchor="middle">12</text>
<text class="vx-text-muted" x="680" y="20" text-anchor="middle">20</text>
<rect class="vx-box-accent" x="140" y="38" width="300" height="26" rx="4"/>
<text class="vx-mono" x="290" y="56" text-anchor="middle">B  weight 24/10</text>
<rect class="vx-box" x="200" y="78" width="360" height="26" rx="4"/>
<text class="vx-mono" x="380" y="96" text-anchor="middle">C  weight 2/12</text>
<rect class="vx-box-bad" x="80" y="118" width="600" height="26" rx="4"/>
<text class="vx-mono" x="380" y="136" text-anchor="middle">A  evicted, requeued</text>
</svg>
</div>
<div class="vx-step">
<p><strong>Step 3. Split.</strong> A now outweighs nothing. Its first conflict is at 2 in r0 and at 4 in r1, so it splits at 4: A1 takes r1 before C starts, and A2 conflicts in both registers from its first position, so it goes to a stack slot. D, from 13 to 18, fits in r0 after B.</p>
<svg viewBox="0 0 760 170" role="img" aria-label="Step 3: A is split at position 4. A1, from 0 to 4, takes r1 before C. A2, from 4 to 20, goes to a stack slot. D, from 13 to 18, takes r0 after B ends at 12.">
<text class="vx-text-muted" x="20" y="56">r0</text>
<text class="vx-text-muted" x="20" y="96">r1</text>
<text class="vx-text-muted" x="20" y="136">stack</text>
<line class="vx-line" x1="80" y1="160" x2="680" y2="160"/>
<text class="vx-text-muted" x="80" y="20" text-anchor="middle">0</text>
<text class="vx-text-muted" x="200" y="20" text-anchor="middle">4</text>
<text class="vx-text-muted" x="470" y="20" text-anchor="middle">13</text>
<text class="vx-text-muted" x="680" y="20" text-anchor="middle">20</text>
<rect class="vx-box" x="140" y="38" width="300" height="26" rx="4"/>
<text class="vx-mono" x="290" y="56" text-anchor="middle">B</text>
<rect class="vx-box-accent" x="470" y="38" width="150" height="26" rx="4"/>
<text class="vx-mono" x="545" y="56" text-anchor="middle">D</text>
<rect class="vx-box-accent" x="80" y="78" width="120" height="26" rx="4"/>
<text class="vx-mono" x="140" y="96" text-anchor="middle">A1</text>
<rect class="vx-box" x="200" y="78" width="360" height="26" rx="4"/>
<text class="vx-mono" x="380" y="96" text-anchor="middle">C</text>
<rect class="vx-box-bad" x="200" y="118" width="480" height="26" rx="4"/>
<text class="vx-mono" x="440" y="136" text-anchor="middle">A2 in a stack slot</text>
</svg>
</div>
</div>
<figcaption>Figure 2. The regalloc2-style loop from the example, on four bundles and two registers. Positions run left to right. Large bundles go first; a heavier bundle evicts a lighter one; a bundle that can neither take nor evict is split at its first conflict.</figcaption>
</figure>

The final state puts `A` in `r1` until position 4 and in a stack slot after
it, so the allocator's last step inserts a store at 4 and a load before
`A`'s use at 19. Compare C3's linear scan on the same four intervals. It also gives up `A`,
the interval that ends last, but all of it: `A` lives in memory from 0 to
20, including its use at 0. Splitting let regalloc2 keep `A` in a register
while one was free. Linear scan also chooses by end point alone; had the
hot `B` ended last, it would have been the one spilled, where regalloc2
compares weights and lets a heavy bundle evict a light one.

regalloc2 also contains a second, much simpler allocator for when compile
time matters most, based on reverse linear scan[^ra2-fast]. Fallin reported
that switching Cranelift 0.84 and Wasmtime 0.37 to regalloc2 in 2022 made
overall compilation about 20% faster and improved generated code by up to
10 to 20% on benchmarks limited by register pressure[^cl-ra2]. Those numbers
describe that release on those benchmarks, not allocators in general.

### How Cranelift knows its allocator is right

The allocation checker from Fallin's third post is the idea from this
chapter most worth taking home[^cl-check]. It runs the allocated program
**symbolically**: instead of numbers, each register and stack slot holds the
name of the virtual register whose value it must contain, or "unknown". A
move, spill or reload copies the name from source to destination.

An instruction that reads a register checks that the register holds the name
the original program read there, and an instruction that writes one stores
the name of the value it defines. At a join of two paths the analysis
keeps only what both paths agree on, iterating to a fixpoint the way
[O4](../optimize/o4-dataflow.md)'s dataflow analyses do; this style of
reasoning over summaries instead of values is called **abstract
interpretation**[^cl-check].

The checker can run on every compilation, which Cranelift supports as an
option but does not enable by default, because of its cost. The mode the
project prefers is fuzzing: generate random input programs, allocate them,
and let the checker decide pass or fail[^cl-check]. regalloc2 ships several
fuzz targets, including one that drives the full symbolic checker and one
that checks the parallel-move resolver on its own[^ra2-ion]. C3's checker
tested one property, that no register was shared by overlapping intervals.
The symbolic checker tests the property you care about: every instruction
reads the value the program meant.

## TCC and chibicc: no IR at all

The last two back ends skip the middle of the pipeline. Neither builds an
IR that a selector could look ahead in; each emits code while it walks the
source.

### TCC: a value stack instead of a tree

TCC's developer guide describes a parser that makes one pass over the
source and a code generator that produces linked binary code in the same
pass, keeping no representation of an expression except the entries on a
**value stack**[^tcc-doc]. When the parser recognizes an operand, it pushes
an entry saying where that value is right now. The guide lists the
possibilities: a CPU register, a constant, a local variable at an offset in
the stack frame, the CPU flags (the result of a comparison that has not
been turned into 0 or 1 yet) or a pending jump, and a flag marking the
entry as the address of a value rather than the value itself[^tcc-doc].

Code is generated only when an operator needs it. The function `gv` makes
the top entry live in a register, emitting a load if necessary, and the
guide calls it the most important function of the code generator. On x86,
three registers serve as temporaries, and when a fourth is needed, one is
spilled to a new temporary on the stack[^tcc-doc]. The guide lists TCC's optimizations: constant propagation for all
operations, multiplications and divisions turned into shifts where
possible, and a cache of the processor flags for comparisons. Keeping an
operand as a constant until an operator needs it is what makes the first
two possible. The guide adds that further jump optimization would need a
more abstract form of the code, which TCC does not keep[^tcc-doc].

The program below builds a value stack for an integer calculator with
three registers:

--8<-- "includes/examples/backend/d3-real-backends/value_stack_codegen.cpp.md"

The first expression is folded entirely while it is parsed; no instruction
is ever emitted. In the second, the multiply by 8 becomes a shift and the
folded `3` becomes an immediate operand. The third keeps four products
alive at once, one more than there are registers. When `f` needs a
register, the deepest register-held entry on the stack, `a*b`, goes to
slot `t0`; when `h` needs one, `c*d` goes to `t1`. Both come back only when
the additions finally need them.

Spilling the deepest entry is this example's choice, not something the
guide specifies. It works well here because a value deeper in an expression
stack is consumed later, the same bet Go's allocator makes when it spills
the value whose next use is farthest away.

TCC's back-end interface is correspondingly small. A target supplies a
handful of functions: load a stack entry into a register, store a register
to an lvalue, generate a call, a prologue and an epilogue, an integer or
floating-point binary operation on the top two entries, and conversions
between integer and floating-point types[^tcc-doc]. Porting TCC means
writing those functions and little else.

### chibicc: a tree walk, one commit at a time

chibicc builds an **abstract syntax tree** and types it, then its code
generator walks the tree and prints assembly text[^chibicc]. The generator
in `codegen.c` is a **stack machine**: to compute `a + b`, it computes the
right operand into `%rax`, pushes it, computes the left operand into
`%rax`, pops the saved value into `%rdi` and combines the two
registers[^chibicc-codegen]. That is the macro expansion of
[B1](b1-simplest-backend.md#one-template-per-operation), with the machine
stack standing in for a register allocator. The README is direct about the
result: there is no optimization pass, and the author estimates that the
code is probably at least twice as slow as GCC's[^chibicc].

What makes chibicc worth reading is its history. The README says each
commit corresponds to a section of a book about writing the compiler,
following Ghuloum's incremental approach, the same one behind the
[v0.1 guide](../compiler/guide/index.md#why-build-it-in-stages)[^chibicc].
When the author finds a bug, he rewrites history so that the commit that
introduced it never had it, which keeps every step a working
compiler[^chibicc]. Read it from the first commit, one diff at a time:
each diff is one feature and the code that emits it.

??? check "TCC and chibicc both compile `x*8` without an IR. Which of them can turn the multiply into a shift, and what lets it?"

    TCC. Its value stack still holds `8` as a constant entry when the
    multiply is generated, so the operation can see a constant operand and
    turn the multiply into a shift. chibicc's code generator evaluates each
    operand into `%rax` before combining them, so by the time the multiply
    is emitted, the 8 is a register value like any other.

## The five, side by side

<figure class="vx-figure" id="fig-compare">
<svg viewBox="0 0 900 420" role="img" aria-label="A table comparing five back ends on four questions. QBE: text IL, builds SSA itself; rewrites the same IL, amd64 addressing modes; spill first, then a linear pass with hints; assembly text for the system assembler. Go: generic SSA with memory as a value; rules in _gen/*.rules applied to a fixpoint; a linear-scan variant that spills the value with the farthest next use; its own assembler and the unstable ABIInternal. Cranelift: CLIF in SSA, then VCode with virtual registers; a backward pass with ISLE rules and register-use counts; regalloc2 with bundles, eviction, splitting and a checker; machine code without an external assembler. TCC: no IR, a value stack of operand locations; one operator at a time with constants folded; three registers on x86, spilling to a stack temporary; linked binary code in one pass. chibicc: an AST with types and no other IR; a tree walk with one template per node; a stack machine that pushes and pops rax; assembly text.">
<rect class="vx-box-strong" x="8" y="8" width="104" height="36" rx="4"/>
<text class="vx-text" x="60" y="31" text-anchor="middle">Back end</text>
<rect class="vx-box-strong" x="118" y="8" width="180" height="36" rx="4"/>
<text class="vx-text" x="208" y="31" text-anchor="middle">IR</text>
<rect class="vx-box-strong" x="304" y="8" width="200" height="36" rx="4"/>
<text class="vx-text" x="404" y="31" text-anchor="middle">Selection</text>
<rect class="vx-box-strong" x="510" y="8" width="210" height="36" rx="4"/>
<text class="vx-text" x="615" y="31" text-anchor="middle">Allocation</text>
<rect class="vx-box-strong" x="726" y="8" width="166" height="36" rx="4"/>
<text class="vx-text" x="809" y="31" text-anchor="middle">Output</text>

<rect class="vx-box-accent" x="8" y="52" width="104" height="66" rx="4"/>
<text class="vx-text" x="60" y="90" text-anchor="middle">QBE</text>
<rect class="vx-box" x="118" y="52" width="180" height="66" rx="4"/>
<text class="vx-text" x="208" y="80" text-anchor="middle">text IL; builds</text>
<text class="vx-text" x="208" y="100" text-anchor="middle">SSA itself</text>
<rect class="vx-box" x="304" y="52" width="200" height="66" rx="4"/>
<text class="vx-text" x="404" y="80" text-anchor="middle">rewrites the same IL;</text>
<text class="vx-text" x="404" y="100" text-anchor="middle">amd64 address modes</text>
<rect class="vx-box" x="510" y="52" width="210" height="66" rx="4"/>
<text class="vx-text" x="615" y="80" text-anchor="middle">spill first, then a</text>
<text class="vx-text" x="615" y="100" text-anchor="middle">linear pass with hints</text>
<rect class="vx-box" x="726" y="52" width="166" height="66" rx="4"/>
<text class="vx-text" x="809" y="80" text-anchor="middle">assembly text</text>
<text class="vx-text" x="809" y="100" text-anchor="middle">for system tools</text>

<rect class="vx-box" x="8" y="124" width="104" height="66" rx="4"/>
<text class="vx-text" x="60" y="162" text-anchor="middle">Go</text>
<rect class="vx-box" x="118" y="124" width="180" height="66" rx="4"/>
<text class="vx-text" x="208" y="152" text-anchor="middle">SSA, with memory</text>
<text class="vx-text" x="208" y="172" text-anchor="middle">as a value</text>
<rect class="vx-box" x="304" y="124" width="200" height="66" rx="4"/>
<text class="vx-text" x="404" y="152" text-anchor="middle">.rules files, applied</text>
<text class="vx-text" x="404" y="172" text-anchor="middle">until nothing changes</text>
<rect class="vx-box" x="510" y="124" width="210" height="66" rx="4"/>
<text class="vx-text" x="615" y="152" text-anchor="middle">linear-scan variant;</text>
<text class="vx-text" x="615" y="172" text-anchor="middle">spill farthest next use</text>
<rect class="vx-box" x="726" y="124" width="166" height="66" rx="4"/>
<text class="vx-text" x="809" y="152" text-anchor="middle">own assembler;</text>
<text class="vx-text" x="809" y="172" text-anchor="middle">unstable ABI</text>

<rect class="vx-box" x="8" y="196" width="104" height="66" rx="4"/>
<text class="vx-text" x="60" y="234" text-anchor="middle">Cranelift</text>
<rect class="vx-box" x="118" y="196" width="180" height="66" rx="4"/>
<text class="vx-text" x="208" y="224" text-anchor="middle">CLIF (SSA), then</text>
<text class="vx-text" x="208" y="244" text-anchor="middle">VCode (virtual regs)</text>
<rect class="vx-box" x="304" y="196" width="200" height="66" rx="4"/>
<text class="vx-text" x="404" y="224" text-anchor="middle">backward pass, ISLE</text>
<text class="vx-text" x="404" y="244" text-anchor="middle">rules, use counts</text>
<rect class="vx-box" x="510" y="196" width="210" height="66" rx="4"/>
<text class="vx-text" x="615" y="224" text-anchor="middle">regalloc2: bundles,</text>
<text class="vx-text" x="615" y="244" text-anchor="middle">evict, split; checker</text>
<rect class="vx-box" x="726" y="196" width="166" height="66" rx="4"/>
<text class="vx-text" x="809" y="224" text-anchor="middle">machine code, no</text>
<text class="vx-text" x="809" y="244" text-anchor="middle">assembler step</text>

<rect class="vx-box" x="8" y="268" width="104" height="66" rx="4"/>
<text class="vx-text" x="60" y="306" text-anchor="middle">TCC</text>
<rect class="vx-box" x="118" y="268" width="180" height="66" rx="4"/>
<text class="vx-text" x="208" y="296" text-anchor="middle">none: a value stack</text>
<text class="vx-text" x="208" y="316" text-anchor="middle">of operand locations</text>
<rect class="vx-box" x="304" y="268" width="200" height="66" rx="4"/>
<text class="vx-text" x="404" y="296" text-anchor="middle">one operator at a time,</text>
<text class="vx-text" x="404" y="316" text-anchor="middle">constants folded</text>
<rect class="vx-box" x="510" y="268" width="210" height="66" rx="4"/>
<text class="vx-text" x="615" y="296" text-anchor="middle">three registers on x86,</text>
<text class="vx-text" x="615" y="316" text-anchor="middle">spill to a stack temp</text>
<rect class="vx-box" x="726" y="268" width="166" height="66" rx="4"/>
<text class="vx-text" x="809" y="296" text-anchor="middle">linked binary</text>
<text class="vx-text" x="809" y="316" text-anchor="middle">in one pass</text>

<rect class="vx-box" x="8" y="340" width="104" height="66" rx="4"/>
<text class="vx-text" x="60" y="378" text-anchor="middle">chibicc</text>
<rect class="vx-box" x="118" y="340" width="180" height="66" rx="4"/>
<text class="vx-text" x="208" y="368" text-anchor="middle">typed AST,</text>
<text class="vx-text" x="208" y="388" text-anchor="middle">no other IR</text>
<rect class="vx-box" x="304" y="340" width="200" height="66" rx="4"/>
<text class="vx-text" x="404" y="368" text-anchor="middle">tree walk, one</text>
<text class="vx-text" x="404" y="388" text-anchor="middle">template per node</text>
<rect class="vx-box" x="510" y="340" width="210" height="66" rx="4"/>
<text class="vx-text" x="615" y="368" text-anchor="middle">stack machine: push</text>
<text class="vx-text" x="615" y="388" text-anchor="middle">and pop through %rax</text>
<rect class="vx-box" x="726" y="340" width="166" height="66" rx="4"/>
<text class="vx-text" x="809" y="378" text-anchor="middle">assembly text</text>
</svg>
<figcaption>Figure 3. Five back ends and their answers to the four questions, as their own documentation and source give them. QBE is highlighted as the one to read first: it is the smallest that still has a real register allocator.</figcaption>
</figure>

The selection column of Figure 3 reads well against the ladder
[C1](c1-instruction-selection.md#shared-values-break-trees) took from Hjort
Blindell's survey: macro expansion, tree covering, DAG covering, graph
covering[^hjort].

chibicc stands on the first rung: one template per node,
no look at the neighbors. TCC is on the same rung, with one step of
context: an operator can see whether its operands are still constants.
Go's rules and Cranelift's lowerings match tree-shaped patterns rooted at
one instruction, but they match them against an SSA graph in which a value
can have many uses, so each needs a policy for shared values: Cranelift's
register-use counts, and conditions in many of Go's ARM64 rules that a value
has exactly one use[^go-rules]. QBE's documents do not describe its
selector beyond addressing modes, so the figure does not place it.

The allocation column tells a similar story. The allocators get stronger
as the compile-time budget grows: none at all in chibicc, three registers
spilled on demand in TCC, a greedy linear pass with good spill choices in
Go and QBE, and a backtracking search with a separate checker in Cranelift.
None of them builds Chaitin's interference graph from
[C4](c4-graph-coloring.md). Neither does LLVM's default allocator, which
[E3](e3-llvm-allocator-scheduler-mc.md) reads next to regalloc2.

## Judging a source before you build on it

Every claim in this chapter comes from a page that was opened and checked,
and several of those pages carry warnings about their own age. Learning to
see those warnings is part of reading real back ends.

**Look for a date, and read the numbers with it.** TCC's home page reports
TinyCC about nine times faster than GCC, measured with TinyCC 0.9.22 and
GCC 3.2 at `-O0` on a 2.4 GHz Pentium 4[^tcc-home]. The context is on the
page, and it tells you how old the number is: it compares versions of both
compilers that nobody ships today. The same page says its author no longer works on
TCC[^tcc-home], and the developer guide still
says TCC mainly supports i386[^tcc-doc]. The design described there is
still worth learning; the details of current targets are not in it.

**Look for stated plans that have passed.** chibicc's README hopes to
publish the book in 2021[^chibicc]. That tells you the README's description
of the project may be older than its code.

**Pin what you cite.** Go's SSA `README` and allocator comment live in the
repository and change with it. While this chapter was checked, the driver
function quoted above had already moved out of `rewrite.go` on the main
branch, so the citations here point to the Go 1.26.5 release, the version
installed on this machine.

**Follow every link once.** LuaJIT was a candidate for this chapter. Its
SSA IR was documented on the project's wiki, and that address now
redirects to the project's home page, which says nothing about the
IR[^luajit]. A citation to the old address still looks like a working
source until someone clicks it. That is why LuaJIT is not in this chapter.

## For Vortex

!!! vortex "Exercise"

    **Add QBE as a second native path and test it against your own.**
    Give your compiler an output mode that prints QBE IL for the part of
    Vortex that the [stage 10 program](../compiler/guide/stage-10-matrix-multiplication.md#the-program-the-milestone-asks-for)
    uses: `i32` and `f32` arithmetic, fixed-shape arrays passed by `&` and
    `&mut`, loops, calls, and the runtime error call. Then run `qbe` with
    the `arm64_apple` target (QBE 1.1 or later) and link the result with
    `cc`. Before you write the emitter, answer the four questions of this
    chapter for QBE in a short note, marking what its documents leave open.

    Then write a test with three parts:

    1. **Differential.** Every runnable program in your stage 11 suite
       prints the same bytes and exits with the same status through the QBE
       path as through your main path, the check
       [B1](b1-simplest-backend.md#two-back-ends-one-oracle) calls two back
       ends, one oracle.
    2. **Bounds checks.** A program that indexes out of bounds fails the
       same way on both paths ([record 12](../decisions/arrays.md#d12)). QBE
       knows nothing about Vortex's rules, so the checks must be in the IL
       you emit.
    3. **No contraction.** The assembly QBE produces for `multiply`
       contains none of `fmadd`, `fmsub`, `fnmadd`, `fnmsub`, `fmla` or
       `fmls` ([record 56](../decisions/numbers.md#d56)). If it does, you
       have found something about QBE that its documents did not tell you;
       write it in your note before deciding what to do.

    **Not yet.** Do not make QBE the default path or a required build
    dependency: skip the test, with a message, when `qbe` is not installed.
    Do not emit phi instructions or build SSA for QBE; its IL accepts
    temporaries assigned more than once. Do not copy QBE's allocator into
    your own back end; [C3](c3-linear-scan.md) and [C5](c5-spilling.md) set
    what Vortex's allocator does for now.

    **Done when** the test passes on macOS arm64, and fails for each of two
    deliberate, temporary breakages in the QBE emitter: the operands of one
    subtraction swapped, and one bounds check left out. Add to your note one
    thing QBE did to your IL that your own back end does not do, found in
    its per-pass dumps, with the name of the pass from QBE's feature list.

## Key ideas

!!! recap "You can now answer"

    - **What four questions does this chapter ask of every back end?** What its IR is, how it selects instructions, how it allocates registers, and what it hands to the rest of the toolchain.
    - **How does QBE avoid spilling during allocation?** Its spiller runs first and lowers register pressure below the register count everywhere; SSA form then lets one linear pass assign every value.
    - **In Go's dumps, which pass turns a shift, an add and a load into one indexed load?** `lower`, using rules generated from `ARM64.rules` and applied until nothing changes.
    - **When does Cranelift's lowering skip an instruction it could have emitted?** When the instruction has no side effect and no consumer read its result from a register, because every consumer absorbed it.
    - **What are regalloc2's three choices for a bundle, and why does the loop finish?** Take a free register, evict lighter bundles, or split at the first conflict; it evicts only lighter bundles and split pieces get heavier.
    - **What does TCC keep instead of an IR?** A value stack whose entries say where each operand is: a register, a constant, a local, the flags or a spilled temporary.
    - **What makes a source unsafe to build on?** Undated numbers, stated plans that have passed, links to a moving branch, and links nobody has followed recently.

## Where this comes back

!!! next "You will use this again in"

    - [E1. The LLVM code generator pipeline](e1-llvm-codegen-pipeline.md): *the four questions*, *DAG covering*, *pass dumps*
    - [E2. Describing a target](e2-describing-a-target.md): *patterns written as data*, *generated matchers*
    - [E3. LLVM's allocator, scheduler and MC layer](e3-llvm-allocator-scheduler-mc.md): *eviction by spill weight*, *splitting*, *regalloc2*
    - [E4. Testing back ends](e4-testing-backends.md): *symbolic checker*, *fuzzing with an oracle*, *differential testing*
    - [O12. Testing an optimizer](../optimize/o12-testing-optimizers.md): *checkers as fuzzing oracles*

## Sources and further reading

Read QBE's home page, IL reference and comparison page first; together
they are short enough for one sitting. Then read Fallin's Cranelift posts in
the order cited here, and run `GOSSAFUNC` on a function of your own with the
Go SSA `README` open beside it.

[^qbe-home]: QBE project, "QBE: compiler backend", home page: goal, feature list (SSA IL, split spiller and allocator, loop-aware spilling, linear allocator with hinting, amd64 addressing modes, C ABI) and the getting-started command. <https://c9x.me/compile/>
[^qbe-il]: QBE project, "QBE Intermediate Language": input files, temporaries, memory and comparison instructions, and the phi section on programs not in SSA form. <https://c9x.me/compile/doc/il.html>
[^qbe-vs-llvm]: QBE project, "QBE vs LLVM": scope, size, per-pass IL dumps and the one-instruction increment. <https://c9x.me/compile/doc/llvm.html>
[^qbe-releases]: QBE project, "Releases": 1.1 (February 2023, `arm64_apple` target) to 1.3 (June 2026). <https://c9x.me/compile/releases.html>
[^go-ssa]: The Go Authors, "Introduction to the Go compiler's SSA backend", `cmd/compile/internal/ssa/README.md`, Go 1.26.5: values, memory, blocks, passes, `lower`, `GOSSAFUNC` and rewrite rules. <https://github.com/golang/go/blob/go1.26.5/src/cmd/compile/internal/ssa/README.md>
[^go-rewrite]: The Go Authors, `cmd/compile/internal/ssa/rewrite.go` and `lower.go`, Go 1.26.5: `applyRewrite`, which repeats rewrites until none apply, can delete values that become dead, and switches on cycle detection after an iteration limit. <https://github.com/golang/go/blob/go1.26.5/src/cmd/compile/internal/ssa/rewrite.go>
[^go-rules]: The Go Authors, `cmd/compile/internal/ssa/_gen/ARM64.rules`, Go 1.26.5: the rules that produce `MOVWloadidx4`, and single-use conditions. <https://github.com/golang/go/blob/go1.26.5/src/cmd/compile/internal/ssa/_gen/ARM64.rules>
[^go-ops]: The Go Authors, `cmd/compile/internal/ssa/_gen/ARM64Ops.go`, Go 1.26.5: the definition of `LoweredNilCheck`. <https://github.com/golang/go/blob/go1.26.5/src/cmd/compile/internal/ssa/_gen/ARM64Ops.go>
[^go-regalloc]: The Go Authors, `cmd/compile/internal/ssa/regalloc.go`, Go 1.26.5: the header comment describing the allocator and its spill placement. <https://github.com/golang/go/blob/go1.26.5/src/cmd/compile/internal/ssa/regalloc.go>
[^go-compile]: The Go Authors, `cmd/compile/internal/ssa/compile.go`, Go 1.26.5: the pass list, with `critical` before `regalloc`. <https://github.com/golang/go/blob/go1.26.5/src/cmd/compile/internal/ssa/compile.go>
[^go-asm]: The Go Authors, "A Quick Guide to Go's Assembler": Plan 9 input style, the semi-abstract instruction set, Go object files. <https://go.dev/doc/asm>
[^go-abi]: The Go Authors, "Go internal ABI specification", `cmd/compile/abi-internal.md`: ABIInternal is unstable, ABI0 and the wrappers between them, and arm64's use of R0 to R15 for integer arguments and results. <https://github.com/golang/go/blob/master/src/cmd/compile/abi-internal.md>
[^cl-isel1]: Chris Fallin, "A New Backend for Cranelift, Part 1: Instruction Selection", 18 September 2020: VCode, operand-tree matching, the backward pass with use counts, side-effect colors. <https://cfallin.org/blog/2020/09/18/cranelift-isel-1/>
[^cl-check]: Chris Fallin, "Cranelift, Part 3: Correctness in Register Allocation", 15 March 2021: the symbolic checker, runtime and fuzzing modes. <https://cfallin.org/blog/2021/03/15/cranelift-isel-3/>
[^cl-ra2]: Chris Fallin, "Cranelift, Part 4: A New Register Allocator", 9 June 2022: regalloc2's bundles, assignment loop, forward progress, fuzzing, and measured results. <https://cfallin.org/blog/2022/06/09/cranelift-regalloc2/>
[^cl-isle]: Chris Fallin, "Cranelift's Instruction Selector DSL, ISLE: Term-Rewriting Made Practical", 20 January 2023. <https://cfallin.org/blog/2023/01/20/cranelift-isle/>
[^ra2-ion]: Bytecode Alliance, regalloc2, `doc/ION.md`: priority, weight, split strategy, termination and fuzz targets. <https://github.com/bytecodealliance/regalloc2/blob/main/doc/ION.md>
[^ra2-fast]: Bytecode Alliance, regalloc2, `doc/FASTALLOC.md`: an allocator for fast compile times based on reverse linear scan. <https://github.com/bytecodealliance/regalloc2/blob/main/doc/FASTALLOC.md>
[^tcc-doc]: Fabrice Bellard, "Tiny C Compiler Reference Documentation", sections "Introduction" and "Code generation": value stack, `gv`, registers and spilling, back-end functions, optimizations. <https://bellard.org/tcc/tcc-doc.html>
[^tcc-home]: Fabrice Bellard, "TCC: Tiny C Compiler", home page: the notice that its author no longer works on TCC, and the compilation-speed table. <https://bellard.org/tcc/>
[^chibicc]: Rui Ueyama, chibicc `README`: commits as book sections, internals, no optimization pass, history rewriting. <https://github.com/rui314/chibicc>
[^chibicc-codegen]: Rui Ueyama, chibicc, `codegen.c`: `push`, `pop` and the code for binary operators. <https://github.com/rui314/chibicc/blob/main/codegen.c>
[^hjort]: Gabriel Hjort Blindell, "Survey on Instruction Selection: An Extensive and Modern Literature Study", arXiv:1306.4898, 2013. <https://arxiv.org/abs/1306.4898>
[^luajit]: LuaJIT project, former wiki page "SSA IR 2.0", `http://wiki.luajit.org/SSA-IR-2.0`, which redirected to <https://luajit.org/> when checked in September 2026.

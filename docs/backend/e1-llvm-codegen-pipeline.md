# E1. The LLVM code generator pipeline

<p class="page-intro">LLVM turns IR into machine code through a long, fixed list of named passes, and it will print the list, stop after any pass and show you the function at that point. This chapter follows two small functions through that list, stage by stage, so that SelectionDAG, GlobalISel and MIR become things you have read, and so that you can map each job in your own back end onto the pass in LLVM that does the same job.</p>

<p class="vx-meta" markdown="1">Level: Advanced · Reading time: about 40 minutes · Builds on: [C1. Instruction selection](c1-instruction-selection.md), [C2. Liveness](c2-liveness.md), [C4. Register allocation II: graphs and SSA](c4-graph-coloring.md), [C6. Instruction scheduling](c6-scheduling.md)</p>

???+ remember "Before you start, remember"

    ??? question "What is a tile, and how does maximal munch choose between tiles?"

        A tile is a small tree pattern together with the instruction that
        computes it and a cost, such as `madd` for an add whose operand is a
        multiply. Maximal munch starts at the root, takes the largest tile
        that matches, and repeats on each subtree the tile left for a
        register.

        Introduced in [C1. Instruction selection](c1-instruction-selection.md).

    ??? question "What is a dead definition?"

        An instruction result that no later instruction reads. The value is
        live nowhere, but the instruction still writes a register when it
        runs, so the register it writes still matters.

        Introduced in [C2. Liveness](c2-liveness.md#what-an-instruction-uses-and-defines).

    ??? question "What does a phi node do, and why can a real processor not run one?"

        At the top of a block where control paths meet, a phi picks the
        value that arrived along the edge control took. No instruction set
        has such an instruction, so before code runs every phi must become
        ordinary moves on the incoming edges.

        Introduced in [6. The first machine code](../compiler/guide/stage-6-first-machine-code.md#ssa-if-you-use-llvm).

    ??? question "What does coalescing do to a copy?"

        It gives the copy's source and destination the same register, when
        they do not interfere, so the copy moves a register into itself and
        can be deleted.

        Introduced in [C4. Register allocation II: graphs and SSA](c4-graph-coloring.md#coalescing-deleting-copies).

    ??? question "What is `cmp w0, w1` another name for?"

        `subs wzr, w0, w1`: a subtraction that sets the flags and sends its
        result to the zero register, which discards it.

        Introduced in [A2. Reading and writing AArch64 assembly](a2-aarch64-assembly.md#flags-and-conditions).

!!! goals "In this chapter"

    - Name the seven stages of LLVM's code generator and place the pass names that `llc -debug-pass=Structure` prints into them.
    - Explain what SelectionDAG, GlobalISel and FastISel each do, and predict which one `llc` uses for AArch64 at `-O0` and at `-O2`.
    - Read a MIR dump and tell from its registers, types and flags roughly where in the pipeline it was taken.
    - Follow a phi through phi elimination and register coalescing, and say where SSA form ends.
    - Predict whether `llc` will fuse a multiply and an add into `fmadd`, from the IR flags, the intrinsics and the command line, and connect that to Vortex's rule against contraction.

A **code generator pipeline** is the ordered list of passes a back end runs
to turn its input IR into machine code. Part C built the jobs in that list
one at a time, on toy examples: selecting instructions
([C1](c1-instruction-selection.md)), computing liveness
([C2](c2-liveness.md)), allocating registers
([C3](c3-linear-scan.md) to [C5](c5-spilling.md)) and scheduling
([C6](c6-scheduling.md)). LLVM runs all of them, and more, on every function
it compiles. This chapter reads LLVM's list as a worked answer to the
question Part C left open: what does a production back end run, in what
order, and why in that order?

LLVM is unusually willing to show its work. Every pass has a name, the tool
`llc` prints the list it will run, and it can stop after any pass and write
the function out as text that it can later read back in. So this chapter
does not describe the pipeline from a diagram. It runs it, on two small
functions, and reads what comes out.

All tool output on this page was observed with `llc` and `opt` 18.1.8 (the
build reports default target `arm64-apple-darwin27.0.0`) on the owner's
Apple M4 Pro under macOS 27, on 24 September 2026. Your output may differ in
register numbers or pass names on another version; the method stays the
same.

## Seven stages, one list

LLVM's own description of its code generator divides the work into seven
stages[^cg]:

1. **Instruction selection** turns LLVM IR into target instructions, using
   **virtual registers** (an unlimited supply of names, each standing for
   whichever physical register the allocator later picks) in SSA form.
2. **Scheduling and formation** gives the selected instructions an order
   and writes them out as a list of `MachineInstr` objects.
3. **SSA-based machine code optimizations** clean up that list while every
   virtual register still has one definition.
4. **Register allocation** maps the unlimited virtual registers onto the
   target's finite register file, adding spill code where they do not fit.
5. **Prolog and epilog insertion** adds the function's entry and exit code
   once the frame size is known, and replaces abstract stack slots with real
   offsets.
6. **Late machine code optimizations** work on the final instructions.
7. **Code emission** writes the function as assembly text or as bytes in an
   object file.

Part C's chapters fit into this list: C1 is stage 1, C6 is part of stage 2
and of a later scheduling pass, C2 to C5 are stage 4, and
[C7](c7-peephole.md)'s peephole rules appear in stages 3 and 6. The one idea
the list adds is the order. SSA-based optimizations come before allocation,
while each value still has one definition, which is what makes them
simple to write ([C2](c2-liveness.md#what-ssa-form-buys)). Prolog insertion
comes after allocation, because only then is the frame's size known,
spill slots included, and only then does the compiler know which
callee-saved registers the function touched and must save[^cg][^braun].

<figure class="vx-figure">
<svg viewBox="0 0 900 440" role="img" aria-label="LLVM's code generator pipeline, from LLVM IR to assembly or object bytes" aria-describedby="e1-pipe-desc">
<title id="e1-pipe-title">LLVM's code generator pipeline, from LLVM IR to assembly or object bytes</title>
<desc id="e1-pipe-desc">LLVM IR enters one of three instruction selectors: SelectionDAG, the default at -O2; GlobalISel, the AArch64 default at -O0; and FastISel, a quick selector that hands anything it cannot handle to SelectionDAG. All three produce MachineInstr in SSA form with virtual registers. A second row of stations follows: SSA machine optimizations such as machine CSE, loop-invariant code motion, sinking and peephole; leaving SSA through phi elimination, the two-address pass and the register coalescer; the machine scheduler, the greedy allocator and the virtual register rewriter; prologue and epilogue insertion; and late passes such as the post-RA scheduler and block placement. A bracket marks everything up to the rewriter as working on virtual registers and everything after as physical registers. The last station feeds the MC layer, where MCInst and MCStreamer write assembly text or object bytes.</desc>
<rect class="vx-box-strong" x="10" y="80" width="110" height="56" rx="4"/>
<text class="vx-text" x="65" y="113" text-anchor="middle">LLVM IR</text>
<path class="vx-flow" d="M120 108 L140 108 L140 44 L158 44"/>
<polygon class="vx-arrowhead" points="158,39 166,44 158,49"/>
<path class="vx-flow" d="M120 108 L158 108"/>
<polygon class="vx-arrowhead" points="158,103 166,108 158,113"/>
<path class="vx-flow" d="M140 108 L140 172 L158 172"/>
<polygon class="vx-arrowhead" points="158,167 166,172 158,177"/>
<g class="vx-seq" style="--vx-i: 0; --vx-n: 7">
<rect class="vx-box-accent" x="166" y="18" width="250" height="52" rx="4"/>
<text class="vx-text" x="291" y="40" text-anchor="middle">SelectionDAG</text>
<text class="vx-text-muted" x="291" y="58" text-anchor="middle">one DAG per block; -O2 default</text>
<rect class="vx-box-accent" x="166" y="82" width="250" height="52" rx="4"/>
<text class="vx-text" x="291" y="104" text-anchor="middle">GlobalISel</text>
<text class="vx-text-muted" x="291" y="122" text-anchor="middle">whole function; AArch64 at -O0</text>
<rect class="vx-box" x="166" y="146" width="250" height="52" rx="4"/>
<text class="vx-text" x="291" y="168" text-anchor="middle">FastISel</text>
<text class="vx-text-muted" x="291" y="186" text-anchor="middle">quick; falls back to SelectionDAG</text>
</g>
<path class="vx-flow" d="M416 44 L440 44 L440 108 L466 108"/>
<path class="vx-flow" d="M416 108 L466 108"/>
<path class="vx-flow" d="M416 172 L440 172 L440 108 L466 108"/>
<polygon class="vx-arrowhead" points="466,103 474,108 466,113"/>
<g class="vx-seq" style="--vx-i: 1; --vx-n: 7">
<rect class="vx-box-strong" x="474" y="76" width="220" height="64" rx="4"/>
<text class="vx-text" x="584" y="102" text-anchor="middle">MachineInstr in SSA</text>
<text class="vx-text-muted" x="584" y="122" text-anchor="middle">virtual registers, one def each</text>
</g>
<path class="vx-flow" d="M584 140 L584 220 L90 220 L90 244"/>
<polygon class="vx-arrowhead" points="85,244 90,252 95,244"/>
<g class="vx-seq" style="--vx-i: 2; --vx-n: 7">
<rect class="vx-box" x="10" y="252" width="160" height="80" rx="4"/>
<text class="vx-text" x="90" y="274" text-anchor="middle">SSA machine opts</text>
<text class="vx-text-muted" x="90" y="294" text-anchor="middle">machine CSE, LICM,</text>
<text class="vx-text-muted" x="90" y="310" text-anchor="middle">sinking, peephole</text>
</g>
<line class="vx-flow" x1="170" y1="292" x2="184" y2="292"/>
<polygon class="vx-arrowhead" points="184,287 192,292 184,297"/>
<g class="vx-seq" style="--vx-i: 3; --vx-n: 7">
<rect class="vx-box-accent" x="192" y="252" width="160" height="80" rx="4"/>
<text class="vx-text" x="272" y="274" text-anchor="middle">Leave SSA</text>
<text class="vx-text-muted" x="272" y="294" text-anchor="middle">phi elimination,</text>
<text class="vx-text-muted" x="272" y="310" text-anchor="middle">two-address, coalescer</text>
</g>
<line class="vx-flow" x1="352" y1="292" x2="366" y2="292"/>
<polygon class="vx-arrowhead" points="366,287 374,292 366,297"/>
<g class="vx-seq" style="--vx-i: 4; --vx-n: 7">
<rect class="vx-box-accent" x="374" y="252" width="170" height="80" rx="4"/>
<text class="vx-text" x="459" y="274" text-anchor="middle">Schedule, allocate</text>
<text class="vx-text-muted" x="459" y="294" text-anchor="middle">machine scheduler,</text>
<text class="vx-text-muted" x="459" y="310" text-anchor="middle">greedy, rewriter</text>
</g>
<line class="vx-flow" x1="544" y1="292" x2="558" y2="292"/>
<polygon class="vx-arrowhead" points="558,287 566,292 558,297"/>
<g class="vx-seq" style="--vx-i: 5; --vx-n: 7">
<rect class="vx-box" x="566" y="252" width="150" height="80" rx="4"/>
<text class="vx-text" x="641" y="274" text-anchor="middle">Prolog, epilog</text>
<text class="vx-text-muted" x="641" y="294" text-anchor="middle">frame, saves,</text>
<text class="vx-text-muted" x="641" y="310" text-anchor="middle">stack offsets</text>
</g>
<line class="vx-flow" x1="716" y1="292" x2="730" y2="292"/>
<polygon class="vx-arrowhead" points="730,287 738,292 730,297"/>
<g class="vx-seq" style="--vx-i: 6; --vx-n: 7">
<rect class="vx-box" x="738" y="252" width="152" height="80" rx="4"/>
<text class="vx-text" x="814" y="274" text-anchor="middle">Late passes</text>
<text class="vx-text-muted" x="814" y="294" text-anchor="middle">post-RA scheduler,</text>
<text class="vx-text-muted" x="814" y="310" text-anchor="middle">block placement</text>
<rect class="vx-box-strong" x="566" y="378" width="324" height="50" rx="4"/>
<text class="vx-text" x="728" y="399" text-anchor="middle">MC layer: MCInst, MCStreamer</text>
<text class="vx-text-muted" x="728" y="417" text-anchor="middle">assembly text or object bytes</text>
</g>
<line class="vx-flow" x1="814" y1="332" x2="814" y2="370"/>
<polygon class="vx-arrowhead" points="809,370 814,378 819,370"/>
<line class="vx-line" x1="10" y1="350" x2="530" y2="350"/>
<text class="vx-text-muted" x="270" y="368" text-anchor="middle">virtual registers</text>
<line class="vx-line" x1="548" y1="350" x2="890" y2="350"/>
<text class="vx-text-muted" x="680" y="368" text-anchor="middle">physical registers</text>
</svg>
<figcaption>Figure 1. The pipeline as LLVM runs it for AArch64. Three selectors lead into one representation, <code>MachineInstr</code>, and from there every function takes the same road. Everything before the virtual register rewriter works on virtual registers; everything after it works on physical ones. The stations group the passes the way LLVM's documentation and Braun's tutorial group them.</figcaption>
</figure>

One more fact explains how the list is printed. LLVM has two pass
managers, the components that decide which passes run and in what order.
The optimizer that `opt` runs uses the newer one, and target-dependent code
generation still uses the older, "legacy" one[^pm]. The flag this chapter
uses to print the code generator's list, `-debug-pass=Structure`, belongs to
the legacy one: given to `opt` with a new-style pipeline, it only prints
"-debug-pass does not work with the new PM". The optimizer's pipeline is
written and printed differently, as [O10](../optimize/o10-pass-pipelines.md)
shows.

## A clamp, followed through `llc`

The first function clamps an integer into a range:

--8<-- "includes/examples/backend/e1-llvm-codegen-pipeline/clampi32.ll.md"

Two comparisons, two selects, no branches. The example checks that the IR is
valid; the rest of this chapter feeds it to `llc`. At `-O2`, `llc` writes
this body for AArch64 (directives removed):

```text
_clampi32:
	cmp	w0, w1
	csel	w8, w1, w0, lt
	cmp	w8, w2
	csel	w0, w2, w8, gt
	ret
```

Five instructions. `csel` is AArch64's conditional select, so each
`icmp`-then-`select` pair in the IR became a compare that sets the flags and
a select that reads them ([A2](a2-aarch64-assembly.md#choosing-without-branching)).
Everything between the IR and these five lines is the pipeline.

Ask `llc` what it will run before it runs it:

```text
$ llc -O2 -debug-pass=Structure clampi32.ll -o /dev/null
```

The list goes to standard error. At `-O2` its first line, `Pass Arguments:`,
named 207 passes for this file, and at `-O0` it named 77. Most of the
difference is optimization: an `-O0` build wants the compiler to finish
quickly and leave code that a debugger can follow. Here is a shortened extract of the
`-O2` list, in its printed order, with each group placed in its stage:

| Stage | Passes in the `-O2` list, in order (extract) |
| --- | --- |
| IR passes before selection | Loop Strength Reduction, Expand memcmp() to load/stores, CodeGen Prepare, Insert stack protectors |
| 1 and 2. Selection, formation | AArch64 Instruction Selection, Finalize ISel and expand pseudo-instructions |
| 3. SSA machine optimizations | Early Tail Duplication, Optimize machine instruction PHIs, AArch64 Conditional Compares, Early If-Conversion, Early Machine Loop Invariant Code Motion, Machine Common Subexpression Elimination, Machine code sinking, Peephole Optimizations, AArch64 Dead register definitions |
| 4. Register allocation | Live Variable Analysis, Eliminate PHI nodes for register allocation, Two-Address instruction pass, Live Interval Analysis, Register Coalescer, Machine Instruction Scheduler, Greedy Register Allocator, Virtual Register Rewriter, Stack Slot Coloring |
| 5. Prolog and epilog | Shrink Wrapping analysis, Prologue/Epilogue Insertion & Frame Finalization |
| 6. Late optimizations | Control Flow Optimizer, Tail Duplication, Post-RA pseudo instruction expansion pass, AArch64 load / store optimization pass, PostRA Machine Instruction Scheduler, Branch Probability Basic Block Placement, Machine Outliner, Branch relaxation pass |
| 7. Emission | AArch64 Assembly Printer |

Three things stand out. First, the code generator starts with ordinary IR
passes: `llc` rewrites the IR (strength reduction, expanding `memcmp`,
inserting stack protectors) before any instruction is selected. Second, the
machine scheduler sits inside stage 4, immediately before the allocator, and a
second scheduler runs after allocation; that is C6's split between
scheduling before and after allocation, as LLVM ships it. Third, names that
start with `AArch64` are target passes placed among the shared ones. Braun's
tutorial on the machine representation shows how: a target supplies a
`TargetPassConfig` whose hooks, such as `addPreRegAlloc`, add, replace or
remove passes at fixed points in the shared list[^braun]. The same tutorial groups the shared passes into machine SSA
passes, the optimized register allocation passes and late passes, which is
the grouping Figure 1 follows[^braun].

## Three ways in: SelectionDAG, GlobalISel and FastISel

Stage 1 is the only stage with a choice of implementation. LLVM has three
instruction selectors, and which one runs depends on the target and the
optimization level.

### SelectionDAG

**SelectionDAG** is the selector `llc` uses at `-O2`. It builds a
**directed acyclic graph** (DAG) for each basic block: one node per
operation, and an edge from each node to the nodes whose values it
uses[^cg]. Unlike a tree, a DAG lets one value feed several users, the
case [C1](c1-instruction-selection.md) showed breaking tree selectors: the
clamp's first `select` feeds both the second comparison and the second
`select`. Operations with side effects, such as loads, stores and
calls, are also threaded together by **chain** edges, which carry order
rather than data[^cg]. The clamp has none.

The selector works on the DAG in eight steps[^cg]:

1. **Build** the initial DAG from the block's IR. The translation is
   close to one node per IR instruction.
2. **Combine**: simplify the DAG with local rewrites.
3. **Legalize types**: rewrite every value whose type the target cannot
   hold, either by **promoting** a small type to a larger one or by
   **expanding** a large one into several smaller pieces.
4. **Combine** again, to clean up after type legalization.
5. **Legalize operations**: rewrite every operation the target cannot
   perform on its type, by expanding it into other operations, promoting it,
   or calling a target hook.
6. **Combine** a third time.
7. **Select**: match target instruction patterns against the legal DAG,
   producing a DAG of machine nodes.
8. **Schedule**: put the machine nodes in a line and emit them as
   `MachineInstr`s. This is stage 2.

A graph is **legal** for a target when it uses only types and operations the
target supports. LLVM's own example is 32-bit PowerPC, where a DAG holding
an `i1`, `i8`, `i16` or `i64` value is illegal, and so is one that uses a
remainder operation[^cg]. The legalizers exist so that every target does not
have to handle every type and operation in its selector; the combines exist
so that the legalizers can be simple, producing correct but clumsy code that
the next combine cleans up[^cg].

The select step is C1's tree covering, generalized to DAGs. Most of the
patterns come from the target's TableGen description
([E2](e2-describing-a-target.md)): each instruction record can carry a
pattern such as "an `fadd` whose operand is an `fmul`", and a generator
turns the patterns into matching code when LLVM itself is built[^cg].
Figure 2 draws the clamp's DAG before and after selection.

<figure class="vx-figure">
<svg viewBox="0 0 760 380" role="img" aria-label="The clamp's SelectionDAG before and after instruction selection" aria-describedby="e1-dag-desc">
<title id="e1-dag-title">The clamp's SelectionDAG before and after instruction selection</title>
<desc id="e1-dag-desc">Left, the DAG built from the IR: three argument nodes x, lo and hi at the top. setcc slt reads x and lo. select reads that setcc, lo and x. setcc sgt reads the first select and hi. A second select reads the second setcc, hi and the first select, and the return reads the second select. The first select has three users, which a tree could not express. Right, the same block after the select phase: subs reads x and lo and writes the flags, csel lt reads lo, x and the flags, a second subs reads the first csel and hi, and csel gt reads hi, the first csel and the new flags. Each setcc and select pair became a subs and csel pair joined by a flags edge.</desc>
<text class="vx-text" x="190" y="22" text-anchor="middle">built from the IR</text>
<text class="vx-text" x="570" y="22" text-anchor="middle">after the select phase</text>
<line class="vx-line" x1="380" y1="36" x2="380" y2="370"/>
<rect class="vx-box" x="40" y="40" width="60" height="30" rx="4"/>
<text class="vx-mono" x="70" y="60" text-anchor="middle">x</text>
<rect class="vx-box" x="160" y="40" width="60" height="30" rx="4"/>
<text class="vx-mono" x="190" y="60" text-anchor="middle">lo</text>
<rect class="vx-box" x="280" y="40" width="60" height="30" rx="4"/>
<text class="vx-mono" x="310" y="60" text-anchor="middle">hi</text>
<g class="vx-seq" style="--vx-i: 0; --vx-n: 2">
<rect class="vx-box-accent" x="60" y="100" width="120" height="30" rx="4"/>
<text class="vx-mono" x="120" y="120" text-anchor="middle">setcc slt</text>
<rect class="vx-box-accent" x="100" y="160" width="120" height="30" rx="4"/>
<text class="vx-mono" x="160" y="180" text-anchor="middle">select</text>
<rect class="vx-box-accent" x="180" y="220" width="120" height="30" rx="4"/>
<text class="vx-mono" x="240" y="240" text-anchor="middle">setcc sgt</text>
<rect class="vx-box-accent" x="160" y="280" width="120" height="30" rx="4"/>
<text class="vx-mono" x="220" y="300" text-anchor="middle">select</text>
<rect class="vx-box" x="160" y="336" width="120" height="30" rx="4"/>
<text class="vx-mono" x="220" y="356" text-anchor="middle">return</text>
<line class="vx-line" x1="70" y1="70" x2="100" y2="100"/>
<line class="vx-line" x1="190" y1="70" x2="140" y2="100"/>
<line class="vx-line" x1="120" y1="130" x2="140" y2="160"/>
<line class="vx-line" x1="190" y1="70" x2="170" y2="160"/>
<line class="vx-line" x1="70" y1="70" x2="112" y2="160"/>
<line class="vx-line" x1="160" y1="190" x2="220" y2="220"/>
<line class="vx-line" x1="310" y1="70" x2="270" y2="220"/>
<line class="vx-line" x1="240" y1="250" x2="230" y2="280"/>
<line class="vx-line" x1="310" y1="70" x2="262" y2="280"/>
<line class="vx-line" x1="150" y1="190" x2="180" y2="280"/>
<line class="vx-line" x1="220" y1="310" x2="220" y2="336"/>
</g>
<rect class="vx-box" x="420" y="40" width="60" height="30" rx="4"/>
<text class="vx-mono" x="450" y="60" text-anchor="middle">x</text>
<rect class="vx-box" x="540" y="40" width="60" height="30" rx="4"/>
<text class="vx-mono" x="570" y="60" text-anchor="middle">lo</text>
<rect class="vx-box" x="660" y="40" width="60" height="30" rx="4"/>
<text class="vx-mono" x="690" y="60" text-anchor="middle">hi</text>
<g class="vx-seq" style="--vx-i: 1; --vx-n: 2">
<rect class="vx-box-strong" x="440" y="100" width="120" height="30" rx="4"/>
<text class="vx-mono" x="500" y="120" text-anchor="middle">SUBSWrr</text>
<rect class="vx-box-strong" x="480" y="160" width="120" height="30" rx="4"/>
<text class="vx-mono" x="540" y="180" text-anchor="middle">CSELWr lt</text>
<rect class="vx-box-strong" x="560" y="220" width="120" height="30" rx="4"/>
<text class="vx-mono" x="620" y="240" text-anchor="middle">SUBSWrr</text>
<rect class="vx-box-strong" x="540" y="280" width="120" height="30" rx="4"/>
<text class="vx-mono" x="600" y="300" text-anchor="middle">CSELWr gt</text>
<rect class="vx-box" x="540" y="336" width="120" height="30" rx="4"/>
<text class="vx-mono" x="600" y="356" text-anchor="middle">return</text>
<line class="vx-line" x1="450" y1="70" x2="480" y2="100"/>
<line class="vx-line" x1="570" y1="70" x2="520" y2="100"/>
<path class="vx-flow" d="M500 130 L520 160"/>
<text class="vx-text-accent" x="470" y="152" text-anchor="middle">flags</text>
<line class="vx-line" x1="570" y1="70" x2="550" y2="160"/>
<line class="vx-line" x1="450" y1="70" x2="492" y2="160"/>
<line class="vx-line" x1="540" y1="190" x2="600" y2="220"/>
<line class="vx-line" x1="690" y1="70" x2="650" y2="220"/>
<path class="vx-flow" d="M620 250 L610 280"/>
<text class="vx-text-accent" x="662" y="272" text-anchor="middle">flags</text>
<line class="vx-line" x1="690" y1="70" x2="642" y2="280"/>
<line class="vx-line" x1="530" y1="190" x2="560" y2="280"/>
<line class="vx-line" x1="600" y1="310" x2="600" y2="336"/>
</g>
</svg>
<figcaption>Figure 2. The clamp's single block as a DAG, drawn by hand from the IR with simplified node names (lines run from a value down to its users). Before selection the nodes are target-independent: <code>setcc</code> is a comparison producing a truth value and <code>select</code> chooses between two values. The first <code>select</code> has three users, which a tree cannot express. After selection each comparison-and-select pair has become an AArch64 subtract that sets the flags and a conditional select that reads them, the instructions the MIR later in this chapter shows.</figcaption>
</figure>

A release build of `llc` like this one cannot print the DAG itself. LLVM's
documentation suggests `-debug-only=isel` and a family of `-view-*-dags`
options for that[^cg], but this `llc` answered
`Unknown command line argument '-debug-only=isel'`. The debug output behind
`-debug-only` is compiled out of builds made without assertions[^progman],
and a packaged release is usually such a build. The DAG is a private
data structure of one pass. What the pass leaves behind, the
`MachineInstr` list, can be printed from any build, and the next sections
read it.

### GlobalISel

**GlobalISel** is the newer selector. Its documentation gives three reasons
for it[^gisel]. SelectionDAG builds a separate representation for each
block, which costs compile time; GlobalISel works directly on the machine
representation the rest of the pipeline uses. SelectionDAG sees one basic
block at a time, which hides opportunities that span blocks; GlobalISel sees
the whole function. And SelectionDAG and FastISel share little code;
GlobalISel's passes are shared by its fast and optimizing configurations.

GlobalISel's input is **generic MIR** (gMIR): machine instructions with
target-independent opcodes such as `G_ADD`, whose virtual registers carry a
**low-level type** such as `s32` (a 32-bit scalar) instead of a register
class[^gmir]. Four passes then narrow it step by step until it is ordinary
machine code[^gisel-pipe]:

- **IRTranslator** turns LLVM IR into gMIR, nearly one instruction for one,
  and lowers arguments and return values by the calling convention.
- **Legalizer** replaces every operation the target cannot perform. Unlike
  SelectionDAG, it has no separate type and operation phases[^gisel-leg].
- **RegBankSelect** assigns every virtual register a **register bank**, a
  group of registers of one kind, such as the general-purpose registers or
  the floating-point and vector registers.
- **InstructionSelect** replaces the generic instructions with target
  instructions, after which no gMIR remains.

Optional **combiner** passes can run between them to replace patterns with
better ones[^gisel-pipe].

`llc -stop-after=<pass>` stops the pipeline after the named pass and prints
the function. Stopping `clampi32.ll` after each of GlobalISel's four passes
at `-O0` shows the narrowing on real code. Step through it:

<div class="vx-stepper" markdown="1">
<div class="vx-step" markdown="1">

**Step 1. After `irtranslator`**

```text
%0:_(s32) = COPY $w0
%1:_(s32) = COPY $w1
%2:_(s32) = COPY $w2
%3:_(s1) = G_ICMP intpred(slt), %0(s32), %1
%4:_(s32) = G_SELECT %3(s1), %1, %0
%5:_(s1) = G_ICMP intpred(sgt), %4(s32), %2
%6:_(s32) = G_SELECT %5(s1), %2, %4
$w0 = COPY %6(s32)
RET_ReallyLR implicit $w0
```

One generic instruction per IR instruction. The arguments arrive in the
physical registers `$w0` to `$w2`, as the calling convention says
([A4](a4-calling-conventions.md)), and are copied at once into virtual
registers. The `_` in `%3:_(s1)` means "no register bank or class yet"; `s1`
is the one-bit truth value an `icmp` produces.

</div>
<div class="vx-step" markdown="1">

**Step 2. After `legalizer`**

```text
%10:_(s32) = G_ICMP intpred(slt), %0(s32), %1
%4:_(s32) = G_SELECT %10(s32), %1, %0
%8:_(s32) = G_ICMP intpred(sgt), %4(s32), %2
%6:_(s32) = G_SELECT %8(s32), %2, %4
```

The comparisons now produce `s32`, not `s1`. No illegal operation may
survive the legalizer[^gisel-pipe], so AArch64's rules evidently do not
accept a one-bit comparison result here; the legalizer widened it and renamed
the registers that changed.

</div>
<div class="vx-step" markdown="1">

**Step 3. After `regbankselect`**

```text
%10:gpr(s32) = G_ICMP intpred(slt), %0(s32), %1
%4:gpr(s32) = G_SELECT %10(s32), %1, %0
```

Every register now carries a bank, `gpr`, the general-purpose registers.
The instructions are still generic. At `-O0` this pass uses its fast mode,
which takes the target's default bank for each instruction[^gisel-rbs].

</div>
<div class="vx-step" markdown="1">

**Step 4. After `instruction-select`**

```text
%0:gpr32 = COPY $w0
%1:gpr32 = COPY $w1
%2:gpr32 = COPY $w2
%12:gpr32 = SUBSWrr %0, %1, implicit-def $nzcv
%4:gpr32 = CSELWr %1, %0, 11, implicit $nzcv
%11:gpr32 = SUBSWrr %4, %2, implicit-def $nzcv
%6:gpr32 = CSELWr %2, %4, 12, implicit $nzcv
$w0 = COPY %6
RET_ReallyLR implicit $w0
```

Target instructions, and each virtual register now has a **register class**,
`gpr32`, the set of 32-bit general-purpose registers
([E2](e2-describing-a-target.md) shows where the class is defined). This is
ordinary MIR, the same four instructions SelectionDAG produced at `-O2`,
apart from register numbers.

</div>
</div>

Two different selectors reached the same four instructions. That is the
point of the shared representation: from here on, nothing in the pipeline
needs to know which selector ran.

### FastISel, and who runs when

**FastISel** is a selector for unoptimized code: it handles common
instructions directly and passes anything it cannot handle to
SelectionDAG. GlobalISel's stated first goal was to replace FastISel on
AArch64, and to fall back to SelectionDAG when selection fails[^gisel]. The
pass lists show where that stands in this build:

| Command | Selection passes in the list, in order | Allocator |
| --- | --- | --- |
| `llc -O2` | AArch64 Instruction Selection | Greedy Register Allocator |
| `llc -O0` | IRTranslator, AArch64O0PreLegalizerCombiner, Localizer, Legalizer, AArch64PostLegalizerLowering, RegBankSelect, InstructionSelect, ResetMachineFunction, AArch64 Instruction Selection | Fast Register Allocator |
| `llc -O0 -global-isel=0` | AArch64 Instruction Selection | Fast Register Allocator |
| `llc -O2 -global-isel` | IRTranslator, AArch64PreLegalizerCombiner, Localizer, LoadStoreOpt, Legalizer, AArch64PostLegalizerCombiner, AArch64PostLegalizerLowering, RegBankSelect, InstructionSelect, AArch64 Post Select Optimizer, ResetMachineFunction (no SelectionDAG pass) | Greedy Register Allocator |

AArch64 enables GlobalISel at `-O0` through a target option, which
`llc --help-hidden` lists as `--aarch64-enable-global-isel-at-O`. With
GlobalISel turned off at `-O0`, the list shows only the SelectionDAG pass:
FastISel runs inside that pass rather than as a pass of its own, and
`llc --help-hidden` lists an option, `--fast-isel-report-on-fallback`, that
reports each time FastISel hands an instruction back to SelectionDAG. The
choice of allocator follows the level too: the `llc` manual names the fast
allocator the default for unoptimized code and greedy the default for
optimized code[^llc].

The last row is worth running once. With `-O2 -global-isel` the clamp came
out as `cmp w1, w0` followed by `csel w8, w1, w0, gt`: the operands of each
comparison swapped and the condition reversed to match. Same meaning,
different text. A test that compares whole listings would call that a
failure, which is one reason [E4](e4-testing-backends.md) tests properties
of assembly instead.

??? check "The `-O0` list runs GlobalISel's four passes and then `AArch64 Instruction Selection` as well. GlobalISel selected every instruction in the clamp, so what is the SelectionDAG pass doing there?"

    Standing by. GlobalISel on AArch64 falls back to SelectionDAG for a
    function it cannot select[^gisel], so the SelectionDAG pass must stay in
    the list, after a pass that resets the half-finished function. For a
    function GlobalISel handled, such as the clamp, the SelectionDAG pass
    finds nothing to do. The listing is a list of passes that may run, not a
    record of which ones changed the code.

## MachineInstr and MIR: a shared language mid-pipeline

All three selectors produce the same thing: a **MachineFunction** holding
**MachineBasicBlocks**, each a list of **MachineInstrs**. A MachineInstr is
an opcode plus a list of operands, which may be registers, immediates, block
references, stack slots and more[^cg][^braun]. The documentation states the
rule that matters most for reading them: MachineInstrs are selected in SSA
form and stay in SSA form until register allocation, with LLVM's phis
becoming machine phis and each virtual register allowed a single
definition[^cg].

**MIR** is the text form of a MachineFunction: a YAML file that `llc` can
write and read back in. Its reference manual says it exists for testing code
generation passes[^mir], and three flags make it a tool for reading the
pipeline as well[^mir][^braun]:

- `-stop-after=<pass>` runs the pipeline up to and including the named pass
  and writes MIR.
- `-stop-before=<pass>` does the same but stops before the pass.
- `-run-pass=<pass>` reads a MIR file, runs only the named pass (with the
  analyses it needs) and writes the result.

Pass names in these flags are the short names from the `Pass Arguments:`
line, such as `finalize-isel`, `phi-node-elimination` or `greedy`, not the
long names in the indented list. Stopping the clamp after `finalize-isel`,
the end of SelectionDAG's work at `-O2`, gives this body (the YAML header,
which records frame information and register classes, is omitted):

```text
body:             |
  bb.0.entry:
    liveins: $w0, $w1, $w2

    %2:gpr32 = COPY $w2
    %1:gpr32 = COPY $w1
    %0:gpr32 = COPY $w0
    %3:gpr32 = SUBSWrr %0, %1, implicit-def $nzcv
    %4:gpr32 = CSELWr %1, %0, 11, implicit $nzcv
    %5:gpr32 = SUBSWrr %4, %2, implicit-def $nzcv
    %6:gpr32 = CSELWr %2, %4, 12, implicit $nzcv
    $w0 = COPY %6
    RET_ReallyLR implicit $w0
```

Read it from the syntax out:

- `bb.0.entry` is basic block 0, which came from the IR block `entry`.
  `liveins` lists the physical registers that hold values on entry.
- `%3` is a virtual register, `$w0` a physical one. The class after the
  colon, `gpr32`, restricts which physical registers `%3` may later
  receive.
- `SUBSWrr` is AArch64's 32-bit ("W") subtract that sets flags ("S"), with
  two register operands ("rr"). Its result, `%3`, is the difference, which
  nobody needs; the flags are what the next instruction wants.
- `implicit-def $nzcv` says the instruction writes the flags register
  although the assembly will not name it, and `implicit $nzcv` on `CSELWr`
  says it reads them. These operands tie each select to its compare.
- `11` and `12` are condition codes as numbers. LLVM's AArch64 back end
  numbers them in the architecture's order, where 11 is `lt` and 12 is
  `gt`[^a64cc], matching the `csel ... lt` and `csel ... gt` in the final
  assembly.

The MIR reference defines a set of operand flags that later dumps add[^mir]:

| Flag | Meaning |
| --- | --- |
| `implicit`, `implicit-def` | a use or definition the assembly does not write out |
| `dead` | a definition whose value nobody reads |
| `killed` | the last use of a register |
| `undef` | a use whose value does not matter |
| `renamable` | a physical register that later passes may rename |

So the shape of a dump tells you roughly where it came from. Generic
opcodes and types like `s32` mean GlobalISel has not finished. Target
opcodes with `%` registers mean selection is done and allocation is not.
Only `$` registers, many marked `renamable`, mean the virtual register
rewriter has run.

## From virtual registers to `cmp w0, w1`

The clamp still has three stages to cross before it becomes the five
instructions at the top of the chapter. Follow the first comparison, which
is the easiest to track.

Stop before the greedy allocator (`-stop-before=greedy`) and the two
subtractions look different:

```text
    dead $wzr = SUBSWrr %0, %1, implicit-def $nzcv
    %4:gpr32 = CSELWr %1, %0, 11, implicit killed $nzcv
```

Between `finalize-isel` and here, a target pass in stage 3, `AArch64 Dead
register definitions`, noticed that nothing reads `%3` and replaced it with
`$wzr`, the zero register, marking the definition `dead`. A subtraction that
writes the zero register is exactly `cmp` ([A2](a2-aarch64-assembly.md#flags-and-conditions)).
Nothing about the result changed; one register less needs allocating.

Now run only the allocator on the file saved after `finalize-isel`:

```text
$ llc -run-pass=greedy clampi32.mir -o -
```

```text
    dead %3:gpr32 = SUBSWrr %0, %1, implicit-def $nzcv
    %4:gpr32 = CSELWr %1, %0, 11, implicit $nzcv
    dead %5:gpr32 = SUBSWrr %4, %2, implicit-def $nzcv
    %6:gpr32 = CSELWr %2, %4, 12, implicit $nzcv
```

Two surprises. The registers are still virtual: the allocator records its
choices in a side table, the virtual register map, the "indirect mapping"
the documentation describes[^cg], and leaves the instructions alone. And
`%3` and `%5` gained `dead` flags, though no pass in this run exists to
find dead values. Adding `-debug-pass=Structure` to the same command
explains both: `-run-pass=greedy` also ran the analyses the allocator
requires, among them `Virtual Register Map` and `Live Interval Analysis`.
Running `-run-pass=liveintervals` on its own adds the same two `dead`
flags, so the liveness analysis is what marks them. They are
C2's dead definitions ([C2](c2-liveness.md#what-an-instruction-uses-and-defines)),
recorded in the IR where every later pass can see them.

The pass that applies the map is the virtual register rewriter. Stop after
it (`-stop-after=virtregrewriter`) and the body finally names physical
registers:

```text
    dead $wzr = SUBSWrr renamable $w0, renamable $w1, implicit-def $nzcv
    renamable $w8 = CSELWr killed renamable $w1, killed renamable $w0, 11, implicit killed $nzcv
    dead $wzr = SUBSWrr renamable $w8, renamable $w2, implicit-def $nzcv
    renamable $w0 = CSELWr killed renamable $w2, killed renamable $w8, 12, implicit killed $nzcv
    RET_ReallyLR implicit $w0
```

The `COPY` instructions from the arguments and to the result are gone: the
allocator put `%0`, `%1` and `%2` in the registers they arrived in, and `%6`
in `$w0`, where the result must leave, so every copy became a move from a
register to itself, which the rewriter deletes. Only `%4`, the clamped
lower bound, needed a register of its own, `w8`. From here the printer
spells `SUBSWrr` into `$wzr` as `cmp`, and the listing at the top of the
chapter appears.

At `-O0` the same comparison came out as `subs w8, w0, w1`, writing a real
register. The `-O0` list has no `AArch64 Dead register definitions` pass,
so the unused difference kept a register, and the fast allocator gave it
one.

??? check "At `-O0` the first comparison assembles to `subs w8, w0, w1`, and at `-O2` to `cmp w0, w1`. Do the two programs compute the same result, and what did the `-O2` version save?"

    The same result. Both subtract `w1` from `w0` and set the same flags,
    and the `csel` that follows reads only the flags. The `-O0` version
    also writes the difference into `w8`, a value nothing reads; the next
    instruction overwrites `w8`. The `-O2` version sends the difference to
    the zero register, which saves the allocator a register for that value.
    In a function with many values alive at once, one register fewer can be
    the difference between keeping a value in a register and spilling it.

## Leaving SSA: a loop with two phis

The clamp has one block and no phis, so it cannot show the part of stage 4
that dismantles SSA form. A loop can:

--8<-- "includes/examples/backend/e1-llvm-codegen-pipeline/triangle.ll.md"

The loop carries two values around its back edge, the counter `i` and the
running sum `acc`, so its header starts with two phis. After selection they
are still there, as machine phis (`-stop-before=phi-node-elimination`,
lightly trimmed):

```text
  bb.1.loop:
    %0:gpr32common = PHI %5, %bb.0, %3, %bb.1
    %1:gpr32 = PHI %6, %bb.0, %2, %bb.1
```

Read the first line as: `%0` is `%5` when control arrives from block 0, the
entry, and `%3` when it arrives from block 1, the loop itself.

Two passes take this apart. **PHI elimination** replaces each phi with
copy instructions, the traditional method, which the documentation says
LLVM adopts[^cg]. The dump after the pass (`-stop-after=phi-node-elimination`)
shows where the copies go: for each phi, a fresh register receives a copy
at the end of each predecessor block, and the phi's result is copied from
that register at the top of the header. The **two-address pass** handles instructions whose
target form requires the destination to be one of the sources: it inserts a
copy so the three-address form becomes a true two-address one, and the
documentation notes that the code may no longer be in SSA form
afterwards[^cg]. AArch64 arithmetic is three-address, so on this function
the two-address pass has little to do; on x86-64 ([B2](b2-x86-64.md)) it
has work almost everywhere. Then the **register coalescer** removes every
copy it can by giving both sides one register, C4's coalescing
([C4](c4-graph-coloring.md#coalescing-deleting-copies)) in production.
Figure 3 follows the loop through all three states.

<figure class="vx-figure">
<svg viewBox="0 0 900 400" role="img" aria-label="Two phis turned into copies, and the copies coalesced away" aria-describedby="e1-phi-desc">
<title id="e1-phi-title">Two phis turned into copies, and the copies coalesced away</title>
<desc id="e1-phi-desc">Three columns show the triangle loop at three points. Each column has an entry block above a loop block with an arrow looping back into the loop block. Left, after selection: the entry sets %5 to 1 and %6 to 0, and the loop starts with %0 = PHI of %5 and %3 and %1 = PHI of %6 and %2, then computes %2 = %1 + %0 and %3 = %0 + 1. Middle, after phi elimination: the entry ends with %12 = COPY %5 and %13 = COPY %6, the loop starts with %1 = COPY %13 and %0 = COPY %12, and the loop ends with %12 = COPY %3 and %13 = COPY %2, the copies placed on each incoming edge. Right, after the register coalescer: the entry sets %12 to 1 and %13 to zero, and the loop is %13 = %13 + %12 and %12 = %12 + 1 followed by the compare and branch. All copies are gone, and %12 and %13 are each defined twice, so the code is no longer in SSA form.</desc>
<text class="vx-text" x="150" y="20" text-anchor="middle">after selection</text>
<text class="vx-text" x="450" y="20" text-anchor="middle">after phi elimination</text>
<text class="vx-text" x="750" y="20" text-anchor="middle">after the coalescer</text>
<rect class="vx-box" x="20" y="34" width="260" height="70" rx="4"/>
<text class="vx-text-muted" x="30" y="52">bb.0 (entry)</text>
<text class="vx-mono" x="30" y="72">%5 = 1</text>
<text class="vx-mono" x="30" y="92">%6 = 0</text>
<line class="vx-flow" x1="150" y1="104" x2="150" y2="128"/>
<polygon class="vx-arrowhead" points="145,128 150,136 155,128"/>
<rect class="vx-box-accent" x="20" y="136" width="260" height="150" rx="4"/>
<text class="vx-text-muted" x="30" y="154">bb.1 (loop)</text>
<text class="vx-mono" x="30" y="176">%0 = PHI %5, %3</text>
<text class="vx-mono" x="30" y="198">%1 = PHI %6, %2</text>
<text class="vx-mono" x="30" y="222">%2 = %1 + %0</text>
<text class="vx-mono" x="30" y="244">%3 = %0 + 1</text>
<text class="vx-mono" x="30" y="266">compare, branch</text>
<path class="vx-line" d="M280 266 C 310 266, 310 176, 288 176"/>
<polygon class="vx-arrowhead" points="288,171 280,176 288,181"/>
<g class="vx-seq" style="--vx-i: 0; --vx-n: 2">
<rect class="vx-box" x="320" y="34" width="260" height="100" rx="4"/>
<text class="vx-text-muted" x="330" y="52">bb.0 (entry)</text>
<text class="vx-mono" x="330" y="72">%5 = 1, %6 = 0</text>
<text class="vx-mono" x="330" y="96">%12 = COPY %5</text>
<text class="vx-mono" x="330" y="118">%13 = COPY %6</text>
<line class="vx-flow" x1="450" y1="134" x2="450" y2="150"/>
<polygon class="vx-arrowhead" points="445,150 450,158 455,150"/>
<rect class="vx-box-accent" x="320" y="158" width="260" height="190" rx="4"/>
<text class="vx-text-muted" x="330" y="176">bb.1 (loop)</text>
<text class="vx-mono" x="330" y="198">%1 = COPY %13</text>
<text class="vx-mono" x="330" y="220">%0 = COPY %12</text>
<text class="vx-mono" x="330" y="244">%2 = %1 + %0</text>
<text class="vx-mono" x="330" y="266">%3 = %0 + 1</text>
<text class="vx-mono" x="330" y="290">%12 = COPY %3</text>
<text class="vx-mono" x="330" y="312">%13 = COPY %2</text>
<text class="vx-mono" x="330" y="334">compare, branch</text>
<path class="vx-line" d="M580 334 C 610 334, 610 198, 588 198"/>
<polygon class="vx-arrowhead" points="588,193 580,198 588,203"/>
</g>
<g class="vx-seq" style="--vx-i: 1; --vx-n: 2">
<rect class="vx-box" x="620" y="34" width="260" height="70" rx="4"/>
<text class="vx-text-muted" x="630" y="52">bb.0 (entry)</text>
<text class="vx-mono" x="630" y="72">%12 = 1</text>
<text class="vx-mono" x="630" y="92">%13 = 0</text>
<line class="vx-flow" x1="750" y1="104" x2="750" y2="128"/>
<polygon class="vx-arrowhead" points="745,128 750,136 755,128"/>
<rect class="vx-box-strong" x="620" y="136" width="260" height="106" rx="4"/>
<text class="vx-text-muted" x="630" y="154">bb.1 (loop)</text>
<text class="vx-mono" x="630" y="176">%13 = %13 + %12</text>
<text class="vx-mono" x="630" y="198">%12 = %12 + 1</text>
<text class="vx-mono" x="630" y="222">compare, branch</text>
<path class="vx-line" d="M880 222 C 897 222, 897 176, 888 176"/>
<polygon class="vx-arrowhead" points="888,171 880,176 888,181"/>
<text class="vx-text-accent" x="750" y="270" text-anchor="middle">no copies left;</text>
<text class="vx-text-accent" x="750" y="290" text-anchor="middle">%12 and %13 each defined twice</text>
</g>
</svg>
<figcaption>Figure 3. The <code>triangle</code> loop at three points in stage 4, with the MIR simplified to arithmetic (the real dumps also carry a few extra copies and register classes). Phi elimination puts a copy at the end of each block that jumps to the header, one per phi, and a copy at the top of the header. The coalescer then merges each chain of copies into one register. The result is shorter and no longer in SSA form: <code>%12</code> and <code>%13</code> are each written in two places.</figcaption>
</figure>

After the coalescer (`-stop-after=register-coalescer`), the real MIR is
short enough to finish by hand:

```text
  bb.0.entry:
    liveins: $w0
    %4:gpr32 = COPY $w0
    %12:gpr32common = MOVi32imm 1
    %13:gpr32 = COPY $wzr

  bb.1.loop:
    %13:gpr32 = ADDWrr %13, %12
    %12:gpr32common = ADDWri %12, 1, 0
    dead $wzr = SUBSWrr %12, %4, implicit-def $nzcv
    Bcc 13, %bb.1, implicit killed $nzcv
    B %bb.2

  bb.2.exit:
    $w0 = COPY %13
    RET_ReallyLR implicit $w0
```

Before reading on, predict the loop body in the final assembly. Which
condition does `Bcc 13` test, given that 11 is `lt` and 12 is `gt` in the
same numbering, and which physical register would you expect `%13` to get,
given where its value ends up?

??? check "What does the loop body become in assembly, and why does `%13` get the register it gets?"

    Code 13 is `le`, the next code after `gt`[^a64cc]. The branch loops back
    while the new counter is at most `n`, which is the IR's `sgt` exit
    test turned round to point at the loop. The body `llc -O2` wrote was:

    ```text
    LBB0_1:
    	add	w0, w0, w9
    	add	w9, w9, #1
    	cmp	w9, w8
    	b.le	LBB0_1
    ```

    `%13`, the running sum, went to `w0`. Its value is copied into `$w0` at
    the exit, so giving it `w0` from the start makes that copy disappear,
    the same effect that removed the clamp's copies. Because `n` arrived
    in `w0`, the allocator first moved `n` out of the way, into `w8`.

## Contraction: where the combine step stops

Go back to SelectionDAG's combine passes and one decision they make:
whether a multiply that feeds an add becomes one **fused multiply-add**, an
instruction that computes `a * b + c` with one rounding at the end instead
of one after the multiply and another after the add. Turning a separate
multiply and add into a fused one is called **contraction**. It is not
free: the answers can differ. This C++ program finds inputs where they do,
and prints the results in hexadecimal floating point, so every bit shows:

--8<-- "includes/examples/backend/e1-llvm-codegen-pipeline/one_rounding_or_two.cpp.md"

The exact product of `a` with itself needs one bit more than a `float`
holds. Rounded on its own, that bit is lost and the sum is zero; kept until
the single rounding of `std::fma`, it becomes the answer, $2^{-24}$. Vortex
forbids exactly this change. Each `f32` and `f64` operation must be one
IEEE 754 operation, rounded to nearest-even, and an implementation must not
contract operations ([record 56](../decisions/numbers.md#d56)).

In LLVM IR the permission to contract is a **fast-math flag**, `contract`,
written on floating-point instructions[^langref]. Here are two versions of
the same function:

--8<-- "includes/examples/backend/e1-llvm-codegen-pipeline/contract_off.ll.md"

--8<-- "includes/examples/backend/e1-llvm-codegen-pipeline/contract_on.ll.md"

The Language Reference classes `contract` as a rewrite-based flag: when a
rewrite involves several instructions, every one of them must carry the
flag[^langref]. That is why `contract_on.ll` marks the `fmul` as well as the
`fadd`. Compiled for AArch64:

| File | `llc -O2` | `llc -O0` |
| --- | --- | --- |
| `contract_off.ll` | `fmul` then `fadd` | `fmul` then `fadd` |
| `contract_on.ll` | `fmadd s0, s0, s1, s2` | `fmul` then `fadd` |

The flag grants permission; the pipeline decides whether to use it. At
`-O2` the combine step fused the pair. At `-O0`, where GlobalISel runs with
few combines, it did not.

So is leaving out `contract` enough for Vortex? Not on its own. Two other
routes lead to `fmadd` without any flag in the IR:

- **The `llvm.fmuladd` intrinsic.** It computes `a * b + c` and leaves it
  unspecified whether the multiply is rounded before the add[^fmuladd].
  Apple clang 21 emits it for `a*b + c` in C by default, which is how
  clang's default contraction reaches the back end. `llc -O2` fused a call
  to it into `fmadd` at its default setting.
- **The `-fp-contract` option of `llc`.** `llc --help-hidden` lists three
  values: `fast` fuses whenever profitable, `on` fuses only "blessed"
  operations, and `off` fuses only when the result cannot change. With
  `-fp-contract=fast`, `contract_off.ll` came out as `fmadd` even though its
  IR has no flag. With `-fp-contract=off`, the `llvm.fmuladd` call came out
  as `fmul` and `fadd`.

The rule therefore lives in two places. The IR your compiler writes must
carry no `contract`, `reassoc` or `fast` flags and no calls to
`llvm.fmuladd` (or to `llvm.fma`, the intrinsic for code that requires
fusion[^fmuladd]),
and the command that runs `llc` or a C compiler must not turn contraction
on. [Record I1](../decisions/implementation.md#i1) asks the written back-end
decision to show that contraction is off, whichever back end you chose.

??? check "Your compiler never writes `contract`, and a later change adds `-fp-contract=fast` to the `llc` command in your driver to make the kernel faster. What changes in the output for the stage 10 kernel, and which test would notice?"

    The inner `sum + a * b` can now become `fmadd`, because
    `-fp-contract=fast` fuses without any flag in the IR, so the kernel's
    results can change in their last bits, and golden outputs that compare
    printed digits can fail on some inputs. A test that reads only the IR
    your compiler emits would still pass, because the IR did not change. A
    test on the final assembly (no `fmadd` in `multiply`) or on the exact
    command line catches it, which is why the exercise below checks both.

## The last leg: the MC layer

After the late passes, the **AsmPrinter** lowers each MachineInstr to an
**MCInst**, a much simpler object: an opcode and a list of operands, each
an immediate, a target register or a symbolic expression such as a label
plus an offset[^cg]. It was introduced as a representation separate from
MachineInstr[^mc]. MCInsts go to an **MCStreamer**, an
interface with one method per assembler directive plus one for
instructions. One implementation prints a `.s` file; another encodes the
instructions into an object file, acting as a complete assembler[^cg]. When
the encoder meets an operand whose value is not yet known, such as the
address of a symbol, it records a **fixup** (a note to patch those bytes
later, which may become a relocation) beside the bytes it writes[^mc].
[B3](b3-object-files.md) explains relocations, and
[E3](e3-llvm-allocator-scheduler-mc.md) takes the MC layer apart, together
with the greedy allocator and the machine scheduler that this chapter
treated as stations on a line.

## Reading the documentation with the tool open

The Target-Independent Code Generator page is the natural first stop for
this material, and it says so itself that it is incomplete: it opens with a
"work in progress" warning, and its sections on SSA-based machine code
optimizations and on prolog and epilog insertion say "To Be Written"[^cg].
Some of it has also gone stale. Its register allocation section offers
`llc -regalloc=linearscan` as an example[^cg], and `llc` 18.1.8 rejects it:

```text
$ llc -regalloc=linearscan clampi32.ll -o /dev/null
llc: for the --regalloc option: Cannot find option named 'linearscan'!
```

The history explains the gap. Linear scan was LLVM's default allocator from
2004, and LLVM 3.0 replaced it with the greedy allocator[^greedy]; the `llc`
manual today lists basic, fast, greedy and PBQP[^llc]. The same page
suggests `-debug-only=isel`, which a release build does not accept, as the
SelectionDAG section showed.

None of this makes the page wrong about the shape of the pipeline. It does
mean that a pass name or a flag in prose is a claim to check against the
tool you are running, with `-debug-pass=Structure`, `--help-hidden` and
`-stop-after`, the habit this chapter has practiced throughout. Braun's
tutorial is a better map of the machine passes as they exist[^braun]. When
you want to see how a target is built, "Writing an LLVM Backend" walks
through one, using SPARC as its running example[^wab], and
[E2](e2-describing-a-target.md) reads the target descriptions it relies on.

## For Vortex

!!! vortex "Exercise"

    **Map your back end onto LLVM's pipeline, and pin the contraction rule
    at both of its doors.** Whichever back end you chose in
    [stage 6](../compiler/guide/stage-6-first-machine-code.md#choosing-a-back-end),
    build three things.

    1. **A pipeline map.** A table in your compiler's documentation, not in
       code, with one row for each of the seven stages in this chapter.
       Each row names the pass or function in your compiler that does that
       job (or "none"), and the passes in `llc -O2 -debug-pass=Structure`
       that do it in LLVM. Take the LLVM column from a real run on the IR
       for the [stage 10](../compiler/guide/stage-10-matrix-multiplication.md#the-program-the-milestone-asks-for)
       `multiply` kernel: the IR your compiler emits if you target LLVM,
       or IR you write by hand for the same loops if you do not.
    2. **A stop flag for your own pipeline.** An option, modelled on
       `-stop-after`, that stops your compiler after a named pass of its own
       and prints that pass's output in your IR's text form, plus a way to
       list the pass names it accepts.
    3. **A contraction test at the IR door and the command-line door.** For
       the `multiply` kernel, the test fails if your compiler's output IR
       (or generated C, or assembly) contains any contraction permission or
       fused operation: for LLVM IR, a `contract`, `reassoc` or `fast` flag,
       or a call to `llvm.fmuladd` or `llvm.fma`. It also fails if the
       command your driver runs to finish the build would enable
       contraction (for example `-fp-contract=fast` for `llc`, or anything
       other than `-ffp-contract=off` for a C compiler).

    **Not yet.** Do not write your own SelectionDAG, GlobalISel or MIR
    reader; this exercise reads LLVM's pipeline, it does not rebuild it.
    Do not move your compiler onto LLVM if you chose another back end: the
    map works either way, and rows marked "none" are useful answers. Do
    not write MIR tests yet; [E4](e4-testing-backends.md) does that. Do
    not try to explain all 207 passes: explain the ones in your map.

    **Done when** the map has all seven rows, each LLVM entry is a name you
    found in your own run of `llc`, and you can say for each row marked
    "none" why your compiler does not need it yet. The stop flag works for
    every pass it lists, and a test shows that stopping after the last
    pass gives the same output as a normal run. The contraction test
    passes on your unmodified compiler and fails for each of three
    deliberate, temporary breakages: one `fadd` in the kernel emitted
    with a contraction flag (or its equivalent in your output), one
    multiply and add emitted as a single fused call, and `-fp-contract=fast`
    (or `-ffp-contract=fast`) added to the build command. Record which part
    of the test caught each one.

## Key ideas

!!! recap "You can now answer"

    - **What are the seven stages of LLVM's code generator?** Instruction selection, scheduling and formation, SSA-based machine optimizations, register allocation, prolog and epilog insertion, late optimizations, and code emission.
    - **Which selector does `llc` use for AArch64 at `-O0` and at `-O2`?** GlobalISel (IRTranslator, Legalizer, RegBankSelect, InstructionSelect) at `-O0`, with SelectionDAG kept as a fallback, and SelectionDAG at `-O2`; FastISel runs inside the SelectionDAG pass when GlobalISel is off at `-O0`.
    - **What does SelectionDAG do between building and selecting?** It combines, legalizes types, combines, legalizes operations and combines again, so that the selector only sees types and operations the target supports.
    - **How can you tell from a MIR dump roughly where it was taken?** Generic opcodes and types such as `s32` mean GlobalISel is still running; target opcodes on `%` registers mean selected but not allocated; `$` registers marked `renamable` mean the virtual register rewriter has run.
    - **Where does SSA form end, and what removes the copies it leaves?** At phi elimination and the two-address pass, which turn phis and tied operands into copies; the register coalescer then deletes the copies it can by giving both sides one register.
    - **What can make `llc` emit `fmadd` for a multiply and an add?** A `contract` flag on the instructions (used only when the optimizing combines run), a call to `llvm.fmuladd`, or `-fp-contract=fast` on the command line.
    - **Why check a claim from LLVM's code generator documentation against the tool?** The page is marked as a work in progress, some sections are unwritten, and at least one example, `-regalloc=linearscan`, no longer works; the pass list and `--help-hidden` describe the tool you are running.

## Where this comes back

!!! next "You will use this again in"

    - [E2. Describing a target](e2-describing-a-target.md): *register classes such as `gpr32`*, *where selection patterns come from*, *`implicit-def $nzcv`*
    - [E3. LLVM's allocator, scheduler and MC layer](e3-llvm-allocator-scheduler-mc.md): *the greedy allocator*, *the machine scheduler*, *MCInst and MCStreamer*, *`-run-pass`*
    - [E4. Testing back ends](e4-testing-backends.md): *MIR tests with `-run-pass`*, *testing properties rather than whole listings*
    - [G9. GPU compilers inside LLVM](../gpu/g9-gpu-compilers-in-llvm.md): *the same pipeline with passes added and removed*, *targets that skip register assignment*

## Sources and further reading

LLVM's code generator page is the reference for the stages and for
SelectionDAG; read it with Braun's slides beside it for the machine passes,
and with the GlobalISel pages for the newer selector. Bogner, Nandakumar and
Sanders's tutorial is the place to start if you ever port a target to
GlobalISel[^bogner].

[^cg]: LLVM Project, "The LLVM Target-Independent Code Generator": the seven stages, the SelectionDAG steps, legalization and the DAG combiner, `-debug-only=isel` and the `-view-*-dags` options, machine code in SSA form, the MC layer, the virtual register map, two-address instructions, SSA deconstruction, the built-in allocators and the `-regalloc` examples. <https://llvm.org/docs/CodeGenerator.html>
[^gisel]: LLVM Project, "Global Instruction Selection": the three problems GlobalISel addresses, the goal of replacing FastISel on AArch64, and the fallback to SelectionDAG. <https://llvm.org/docs/GlobalISel/index.html>
[^gisel-pipe]: LLVM Project, GlobalISel, "Core Pipeline": the four passes, the constraint each one leaves behind, and the optional combiners. <https://llvm.org/docs/GlobalISel/Pipeline.html>
[^gmir]: LLVM Project, GlobalISel, "Generic Machine IR": generic opcodes and generic virtual registers with low-level types. <https://llvm.org/docs/GlobalISel/GMIR.html>
[^gisel-leg]: LLVM Project, GlobalISel, "Legalizer": no separate type and operation legalization phases. <https://llvm.org/docs/GlobalISel/Legalizer.html>
[^gisel-rbs]: LLVM Project, GlobalISel, "RegBankSelect": register banks and the fast mode used at `-O0`. <https://llvm.org/docs/GlobalISel/RegBankSelect.html>
[^mir]: LLVM Project, "Machine IR (MIR) Format Reference Manual": the YAML format, its purpose for testing, `-run-pass`, `-stop-after` and `-stop-before`, and the register operand flags. <https://llvm.org/docs/MIRLangRef.html>
[^llc]: LLVM Project, "llc - LLVM static compiler": the `-O` levels and the `--regalloc` values, with fast as the default for unoptimized code and greedy for optimized code. <https://llvm.org/docs/CommandGuide/llc.html>
[^pm]: LLVM Project, "Using the New Pass Manager", section "Status of the New and Legacy Pass Managers": code generation still uses the legacy pass manager. <https://llvm.org/docs/NewPassManager.html>
[^braun]: Matthias Braun, "Welcome to the Back End: The LLVM Machine Representation", 2017 LLVM Developers' Meeting: the machine pass pipeline grouped into machine SSA, register allocation and late passes, `TargetPassConfig` hooks, `-stop-after` and `-run-pass`, and register operand flags. <https://llvm.org/devmtg/2017-10/slides/Braun-Welcome%20to%20the%20Back%20End.pdf>
[^langref]: LLVM Project, "LLVM Language Reference Manual", section "Fast-Math Flags": the `contract` flag and the rule that a rewrite-based flag must be present on every instruction involved. <https://llvm.org/docs/LangRef.html#fast-math-flags>
[^fmuladd]: LLVM Project, "LLVM Language Reference Manual", "'llvm.fmuladd.*' Intrinsic": whether rounding happens between the multiply and the add is unspecified. <https://llvm.org/docs/LangRef.html#llvm-fmuladd-intrinsic>
[^a64cc]: LLVM Project, `llvm/lib/Target/AArch64/Utils/AArch64BaseInfo.h`, `enum CondCode`: the AArch64 condition codes with their numbers, `LT = 0xb`, `GT = 0xc`, `LE = 0xd`. <https://github.com/llvm/llvm-project/blob/main/llvm/lib/Target/AArch64/Utils/AArch64BaseInfo.h>
[^mc]: Chris Lattner, "Intro to the LLVM MC Project", LLVM Project Blog, 9 April 2010: MCInst as a representation separate from MachineInstr, MCStreamer as an assembler API, and fixups produced by the instruction encoder. <https://blog.llvm.org/2010/04/intro-to-llvm-mc-project.html>
[^greedy]: Jakob Stoklund Olesen, "Greedy Register Allocation in LLVM 3.0", LLVM Project Blog, 18 September 2011: linear scan as the default allocator since 2004, replaced by greedy in LLVM 3.0. <https://blog.llvm.org/2011/09/greedy-register-allocation-in-llvm-30.html>
[^progman]: LLVM Project, "LLVM Programmer's Manual", section on the `LLVM_DEBUG()` macro and `-debug-only`: the debug macros are disabled in builds without assertions. <https://llvm.org/docs/ProgrammersManual.html>
[^wab]: LLVM Project, "Writing an LLVM Backend": a walk-through of the pieces of a target, with SPARC as the running example. <https://llvm.org/docs/WritingAnLLVMBackend.html>
[^bogner]: Justin Bogner, Aditya Nandakumar and Daniel Sanders, "Head First into GlobalISel", 2017 LLVM Developers' Meeting: the structure of a GlobalISel back end and how to port one in stages. <https://llvm.org/devmtg/2017-10/slides/Bogner-Nandakumar-Sanders-Head%20First%20into%20GlobalISel.pdf>

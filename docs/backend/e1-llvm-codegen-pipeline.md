# E1. The LLVM code generator pipeline

<p class="page-intro">LLVM turns your IR into machine code through a fixed sequence of named, inspectable passes. This chapter follows one small function through that sequence, so that "SelectionDAG", "GlobalISel" and "MIR" stop being words in a manual and become things you can print and read.</p>

<p class="vx-meta" markdown="1">Level: Advanced · Reading time: about 22 minutes · Builds on: Part C, the classical pipeline ([C1](c1-instruction-selection.md)-[C6](c6-scheduling.md))</p>

???+ remember "Before you start, remember"

    ??? question "In macro-expansion instruction selection, how many machine instructions does one IR operation become?"

        Exactly one, chosen from a fixed template that depends only on that
        operation, never on the code around it. LLVM's SelectionDAG and
        GlobalISel do something more flexible than this, but it is the
        baseline they both improve on.

        Introduced in [B1. The simplest back end that works](b1-simplest-backend.md).

    ??? question "What is the difference between a caller-saved and a callee-saved register?"

        A caller-saved register may be overwritten by any call, so the
        caller must save its value first if it still needs it afterward. A
        callee-saved register keeps its value across a call: the callee is
        the one that must save and restore it if it wants to use it.

        Introduced in [A4. Calling conventions and ABIs](a4-calling-conventions.md).

    ??? question "What does static single assignment form guarantee about a named value?"

        That it is assigned exactly once. A source variable that is
        reassigned becomes a series of separately named versions instead,
        and a phi node picks the right version where two paths meet.

        Introduced in [6. The first machine code](../compiler/guide/stage-6-first-machine-code.md#ssa-if-you-use-llvm).

    ??? question "What can an x86-64 arithmetic instruction read directly that an AArch64 one cannot?"

        A memory operand. AArch64 arithmetic instructions only read
        registers, so a value has to be loaded first; x86-64 can fold that
        load into the arithmetic instruction itself.

        Introduced in [A1. The machine model](a1-machine-model.md#loadstore-machines-and-register-memory-machines).

!!! goals "In this chapter"

    - Explain why LLVM has two independent frameworks for instruction selection, SelectionDAG and GlobalISel, and say which one `llc` picks at which optimization level.
    - Read `llc -debug-pass=Structure` output and connect at least three of its pass names to a job you already understand.
    - Read a MIR dump and tell, from the register names alone, whether it was taken before or after register allocation.
    - Predict whether `llc` will fuse a multiply and an add into one `fmadd`, and connect that decision to the rule Vortex's specification already makes.
    - Locate the stage in the pipeline where your own back end's choices, LLVM or native, would have to answer the same design questions LLVM answers here.

A **code generator pipeline** is the sequence of passes a compiler back end
runs, in a fixed order, to turn its input IR into machine code. LLVM's is one
of the most-used pipelines in existence, and it is also unusually willing to
show its work: every pass has a name, every intermediate state can be
dumped and re-read, and a single pass can be run in isolation on a saved
snapshot. This chapter treats that willingness as an invitation. Rather than
read the pipeline as a diagram, this chapter runs it, on one small function,
and reads what comes out at each stage.

## A clamp, followed through `llc`

Take a function small enough to hold in one screen: clamp an integer between
a low and a high bound.

```llvm title="examples/backend/e1-llvm-codegen-pipeline/clampi32.ll"
--8<-- "examples/backend/e1-llvm-codegen-pipeline/clampi32.ll"
```

Two comparisons, two selects. Nothing about this function is specific to
Vortex, and that is deliberate: the same compare-then-select shape is what a
bounds check on a fixed-shape array element compiles down to, once one
exists, so understanding how `llc` handles it here pays for itself later.

Ask `llc` to compile this file at its default optimization level and read
the assembly it writes for AArch64:

```text
_clampi32:
	cmp	w0, w1
	csel	w8, w1, w0, lt
	cmp	w8, w2
	csel	w0, w2, w8, gt
	ret
```

Four instructions, no branches: `csel` is AArch64's conditional-select
instruction, so what began as `icmp` plus `select` in the IR ends as a
compare that sets flags plus a select that reads them. This is the whole
pipeline's job stated as one sentence: turn IR operations that name what
should happen into instructions that a real AArch64 core can decode, using
the smallest and fastest sequence the code generator can find. Between the
IR at the top of this section and the assembly here sits everything else in
this chapter.

## Two ways in: SelectionDAG and GlobalISel

LLVM does not have one instruction selector. It has two, built on different
principles, and `llc` chooses between them depending on the optimization
level you ask for. Run `llc -debug-pass=Structure` twice on the same file,
once at each end of the optimization range, and the difference in the two
printed pass lists tells you which one ran.

**SelectionDAG** is the older and, by default, the one `llc -O2` uses. It
builds a small directed acyclic graph for each basic block, one node per
value, with edges recording which values feed which. The reference manual
lists its phases in order: build the DAG, combine, legalize types, combine
again, legalize operations, combine a third time, select, then schedule.[^t1]
Each **combine** pass looks for local rewrites, replacing a small
subgraph with a cheaper one, and it is where an `fmul` feeding an `fadd`
could, in principle, become one fused instruction (the next section shows
exactly when that happens and when it does not). **Legalize** rewrites any
operation or type the target cannot represent directly, for example a
64-bit multiply on a target with no such instruction, into something the
target does support. **Select** is the step usually meant by "instruction
selection": each DAG node is matched against the target's known patterns,
generated from its TableGen description ([E2](e2-describing-a-target.md)),
and rewritten into one or more target instructions.

**GlobalISel** is the newer framework, and the one `llc -O0` uses on this
machine. It skips building a per-block DAG and works directly on whole
functions already in MIR form, through four passes: `IRTranslator` turns
LLVM IR into generic MIR one to one, `Legalizer` rewrites unsupported
operations, `RegBankSelect` decides which register bank (roughly, which
kind of physical register file) each value belongs in, and
`InstructionSelect` turns generic MIR instructions into target-specific
ones.[^t3] It exists because SelectionDAG's per-block view makes some
things awkward, most visibly compile time at `-O0`, where GlobalISel's
straight-line passes are meant to be faster to run, at some cost in how
good the generated code is.[^t16]

Running `-debug-pass=Structure` on `clampi32.ll` (LLVM 18.1.8,
`aarch64-apple-darwin27.0.0`, observed locally on 24 September 2026) shows
both halves of this split. At `-O0` the pass list includes `IRTranslator`,
`Legalizer`, `RegBankSelect` and `InstructionSelect`, in that order, ending
with a **Fast Register Allocator**. At `-O2` none of those four names
appear; instead the list goes straight from the mid-pipeline analyses to a
single `AArch64 Instruction Selection` pass, and register allocation later
in the same list is the **Greedy Register Allocator**, not the fast
one.[^t13] SelectionDAG's own combine and legalize work does not print as
separate named passes in this listing; it happens inside the one
instruction-selection pass, driven by the DAG it builds internally.

Braun's walkthrough of LLVM's machine representation lays out the same
`-O2` pipeline in prose, and names several passes worth recognizing on
sight: `PHIElimination` removes the phi nodes SSA form used, replacing them
with copies along each predecessor edge; `TwoAddressInstruction` rewrites
instructions whose target only has destructive two-operand forms;
`RegisterCoalescer` removes copies where the two sides can share one
register; `MachineScheduler` reorders instructions for latency and
throughput before allocation; and after `RegAllocGreedy` runs,
`VirtRegRewriter` and `PrologEpilogInserter` turn the result into a function
with a real stack frame.[^t14]

??? check "Why does `-O0` reach for GlobalISel and the fast allocator, while `-O2` reaches for SelectionDAG and greedy?"

    Because the two choices trade the same thing for the same reason twice.
    GlobalISel and the fast allocator are both built to run in as little
    compile time as possible, at the cost of code quality: right for `-O0`,
    where a quick edit-compile-run cycle matters more than the speed of the
    result. SelectionDAG's combining and the greedy allocator's more
    thorough analysis both spend more compile time to find better code:
    right for `-O2`, where the result is what ships.

## MachineInstr and MIR: a shared language mid-pipeline

Both frameworks converge on the same representation once instruction
selection finishes: **MachineInstr**, one object per machine instruction,
still holding **virtual registers**, unlimited in number, each one a stand-in
for whichever physical register the allocator will eventually choose. A
function made of MachineInstr objects using virtual registers is, in the
sense this book has already used the term, in SSA form again: instruction
selection does not touch the fact that a value has one definition, it only
changes what that value's definition and uses look like.

**MIR** is a YAML-based text form of exactly this state, readable and
writable, that the reference manual describes as covering "the LLVM
target-dependent intermediate representation".[^t4] Three flags make it a
tool for reading the pipeline rather than only a debugging dump:
`-stop-after=<pass>` writes the function to a `.mir` file right after that
pass runs, `-stop-before=<pass>` writes it right before, and `-run-pass=<pass>`
reads a `.mir` file back in and runs exactly one named pass on it, ignoring
every other pass in the pipeline.[^t4] Together they turn "what does pass X
do" from a question you answer by reading source into one you answer by
running a command.

Stopping `clampi32.ll` right after `finalize-isel`, the last step of
`-O2` instruction selection, shows the body as MIR (LLVM 18.1.8,
`aarch64-apple-darwin27.0.0`, observed locally on 24 September 2026, trimmed
to the parts this section discusses):

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

Every register on the left of an assignment is a virtual register,
`%0` through `%6`, tagged with a register class (`gpr32`: a 32-bit
general-purpose register) rather than a physical register name. The
function's three arguments arrive in the physical registers AAPCS64
assigns them, `$w0`, `$w1`, `$w2` ([A4](a4-calling-conventions.md)), and are
immediately copied into virtual registers; the result is copied back into
`$w0` before returning, matching the same convention on the way out. The two
`icmp`-then-`select` pairs from the IR became `SUBSWrr` (subtract, setting
flags, result otherwise unused) followed by `CSELWr` with a numeric
condition code: 11 for "less than", 12 for "greater than", the same
encoding AArch64's condition-code field uses.[^m2] This is instruction
selection's answer, on this target: a compare-and-branch shape in the
source becomes a compare-and-select shape in machine instructions, with no
branch at all.

## Register allocation as one pass among several

`clampi32.mir`, the file written above, can be fed back into `llc` with
`-run-pass=greedy` to watch register allocation happen in isolation, with
every earlier pass skipped:

```text
    dead %3:gpr32 = SUBSWrr %0, %1, implicit-def $nzcv
    %4:gpr32 = CSELWr %1, %0, 11, implicit $nzcv
    dead %5:gpr32 = SUBSWrr %4, %2, implicit-def $nzcv
    %6:gpr32 = CSELWr %2, %4, 12, implicit $nzcv
```

The instructions are the same, still on virtual registers: the greedy pass
alone does not rewrite them to physical registers, that is a later pass,
`VirtRegRewriter`.[^t14] What changes is smaller, and a quick read of the
instruction list alone will not catch it: `%3` and `%5`, the results of the
two `SUBSWrr` instructions, are now marked `dead`.
Nothing ever reads them; only the flags those instructions set are used.
Register allocation's analysis noticed this and recorded it, which is a
reminder of what "one pass, one job" buys: a pass whose stated job is
allocation still needs, and produces, facts about liveness
([C2](c2-liveness.md)) along the way, and MIR lets you see that instead of
taking it on faith.

??? check "The finalize-isel MIR above uses `%0` to `%6`. What would you expect a dump taken after the whole allocator pipeline, not only the one greedy pass, to use instead?"

    Physical register names, `w0`, `w1`, `w2`, `w8` and so on, the same ones
    the final assembly used. `VirtRegRewriter` is the pass that performs
    that replacement, once the greedy pass has decided which physical
    register each virtual one maps to.[^t14]

## Contract flags and where the combine step stops

Go back to the DAG's combine passes, and to one specific question a
combine can decide: whether a multiply that feeds an add should become one
fused multiply-add instruction, one rounding instead of two. Two nearly
identical files answer it differently.

```llvm title="examples/backend/e1-llvm-codegen-pipeline/contract_off.ll"
--8<-- "examples/backend/e1-llvm-codegen-pipeline/contract_off.ll"
```

```llvm title="examples/backend/e1-llvm-codegen-pipeline/contract_on.ll"
--8<-- "examples/backend/e1-llvm-codegen-pipeline/contract_on.ll"
```

The only difference is one word, `contract`, a **fast-math flag** attached
to the `fadd`.[^langref] Compiling both at `-O2` (LLVM 18.1.8,
`aarch64-apple-darwin27.0.0`, observed locally on 24 September 2026) gives
two different answers:

```text
; contract_off.ll
	fmul	s0, s0, s1
	fadd	s0, s0, s2

; contract_on.ll
	fmadd	s0, s0, s1, s2
```

Without `contract`, the multiply and the add stay separate instructions,
each one IEEE 754 operation with its own rounding. With it, the combine
step is free to replace both with `fmadd`, AArch64's fused multiply-add,
computed with a single rounding at the end rather than two. Nothing else
about the two files differs: this one flag is the entire decision.

That is directly Vortex's rule, not a coincidence next to it. The
specification requires every `f32` and `f64` operation to be exactly one
IEEE 754 operation, with no fused multiply-add and no extra precision
([decision 56](../decisions/numbers.md#d56)), which
[stage 6](../compiler/guide/stage-6-first-machine-code.md#lowering-the-first-program)
already states as a rule for whichever back end you choose. If your lowering
pass targets LLVM IR, this section says exactly what keeping that rule
costs: nothing. `contract` is opt-in, attached explicitly by whoever builds
the IR; leaving it off on every `fadd` and `fsub` your compiler emits is
sufficient, because the combine step never fuses an instruction that was
not told it may. The rule lives correctly in the IR your lowering pass
writes, once, rather than in a setting you have to remember to keep
disabled at every later stage.

??? check "If your compiler's lowering pass never writes the `contract` flag, can `llc` still produce an `fmadd` for a plain `fmul` next to an `fadd`, for example by noticing the pattern on its own?"

    No, not for this decision. `llc` treats `contract` as permission it must
    be given, not a pattern it goes looking for; the two files in this
    section differ in nothing else, and only the one with the flag fuses.
    (A target could still emit a hardware fused multiply-add for other
    reasons, such as matching a specific intrinsic, but plain `fmul`
    followed by `fadd` is not one of them.)

## The whole trip, in one picture

<figure class="vx-figure">
<svg viewBox="0 0 900 380" role="img" aria-labelledby="e1-fig-title e1-fig-desc">
<title id="e1-fig-title">Two ways into one pipeline, from LLVM IR to machine code bytes</title>
<desc id="e1-fig-desc">LLVM IR splits into two possible paths. At -O2, it goes through SelectionDAG: build, combine, legalize, combine, select, schedule. At -O0, it goes through GlobalISel: IRTranslate, legalize, register-bank select, select. Both paths converge on MachineInstr in SSA form, using virtual registers. From there, register allocation, greedy or fast depending on the path taken, produces MachineInstr using physical registers, which later passes turn into MCInst and object code bytes.</desc>
<rect class="vx-box-strong" x="10" y="160" width="140" height="60" rx="4"/>
<text class="vx-text" x="80" y="195" text-anchor="middle">LLVM IR</text>
<path class="vx-flow" d="M150 190 L168 190 L168 65 L180 65"/>
<polygon class="vx-arrowhead" points="180,60 188,65 180,70"/>
<path class="vx-flow" d="M150 190 L168 190 L168 295 L180 295"/>
<polygon class="vx-arrowhead" points="180,290 188,295 180,300"/>
<g class="vx-seq" style="--vx-i: 0; --vx-n: 2">
<rect class="vx-box-accent" x="188" y="30" width="240" height="70" rx="4"/>
<text class="vx-text" x="308" y="55" text-anchor="middle">SelectionDAG (-O2 default)</text>
<text class="vx-text-muted" x="308" y="74" text-anchor="middle">build, combine, legalize,</text>
<text class="vx-text-muted" x="308" y="90" text-anchor="middle">combine, select, schedule</text>
</g>
<g class="vx-seq" style="--vx-i: 1; --vx-n: 2">
<rect class="vx-box-accent" x="188" y="260" width="240" height="70" rx="4"/>
<text class="vx-text" x="308" y="285" text-anchor="middle">GlobalISel (-O0 default)</text>
<text class="vx-text-muted" x="308" y="304" text-anchor="middle">IRTranslate, legalize,</text>
<text class="vx-text-muted" x="308" y="320" text-anchor="middle">regbank-select, select</text>
</g>
<path class="vx-flow" d="M428 65 L448 65 L448 190 L470 190"/>
<polygon class="vx-arrowhead" points="470,185 478,190 470,195"/>
<path class="vx-flow" d="M428 295 L448 295 L448 190 L470 190"/>
<polygon class="vx-arrowhead" points="470,185 478,190 470,195"/>
<rect class="vx-box" x="478" y="160" width="170" height="60" rx="4"/>
<text class="vx-text" x="563" y="185" text-anchor="middle">MachineInstr, SSA</text>
<text class="vx-text-muted" x="563" y="203" text-anchor="middle">virtual registers</text>
<line class="vx-flow" x1="648" y1="190" x2="686" y2="190"/>
<polygon class="vx-arrowhead" points="686,185 694,190 686,195"/>
<rect class="vx-box-accent" x="694" y="155" width="196" height="70" rx="4"/>
<text class="vx-text" x="792" y="178" text-anchor="middle">Register allocation</text>
<text class="vx-text-muted" x="792" y="196" text-anchor="middle">greedy or fast, then</text>
<text class="vx-text-muted" x="792" y="212" text-anchor="middle">MCInst, object code bytes</text>
<text class="vx-text-muted" x="308" y="345" text-anchor="middle">chosen by optimization level, not spelled out in your IR</text>
<circle class="vx-dot" r="6">
<animateMotion dur="9s" repeatCount="indefinite" path="M80 190 L168 190 L168 65 L308 65 L308 190 L563 190 L563 190 L792 190" keyPoints="0;0;0.85;1;1" keyTimes="0;0.06;0.8;0.92;1" calcMode="linear"/>
</circle>
</svg>
<figcaption>Figure 1. Instruction selection has two entry points, but they land in the same place. SelectionDAG (the <code>-O2</code> default) and GlobalISel (the <code>-O0</code> default) both produce MachineInstr in SSA form, using virtual registers; from there, one allocator or another assigns physical registers, and later passes finish the trip to MCInst and object code bytes.</figcaption>
</figure>

The last leg, MachineInstr with physical registers to MCInst to bytes on
disk, belongs to the **MC layer**: `MCInst`, a lower-level instruction
representation with no notion of a virtual register at all, and
`MCStreamer`, which either writes textual assembly or, for direct object
emission, encodes instructions straight to bytes, handling fixups (patching
an address once it is known) and relaxation (widening an instruction whose
short form no longer reaches its target) along the way.[^t12]
[E3](e3-llvm-allocator-scheduler-mc.md) covers that layer in depth.

## Reading the docs with the tool open beside them

The Target-Independent Code Generator page is the natural first stop for
this material, and it is worth one caution before you lean on it alone: it
carries its own "work in progress" notice, and at least one example in it
has gone stale. Its allocator walkthrough uses `-regalloc=linearscan`,
which LLVM 18.1.8 on this machine rejects outright:

```text
$ llc -regalloc=linearscan clampi32.ll -o /dev/null
llc: for the --regalloc option: Cannot find option named 'linearscan'!
```

(observed locally, LLVM 18.1.8, `aarch64-apple-darwin27.0.0`, 24 September
2026). None of this makes the page wrong about the shape of the pipeline,
and it says so itself.[^t1] It does mean a pass name or a flag worth
checking against the running tool, with `-debug-pass=Structure` or
`--help-hidden`, rather than trusted from prose alone. That habit, treating
the compiler as the primary source and the manual as a guide to it, is the
same one this whole chapter has been practicing. When you are ready to
write a target description of your own, Writing an LLVM Backend walks
through building one from nothing, using SPARC as its running example,
and [E2](e2-describing-a-target.md) builds on it directly.[^t2]

## For Vortex

!!! vortex "Exercise"

    **Trace one fragment of your matmul kernel through `llc`, and write one
    MIR test that pins a single pass.** Whether your back end eventually
    goes through LLVM or stays native (the choice [stage 6](../compiler/guide/stage-6-first-machine-code.md#choosing-a-back-end)
    asks you to make and write down), doing this once against LLVM's
    pipeline gives you a working answer to compare your own design
    against. Take the innermost multiply-add of the kernel
    `multiply(a: &[f32; M, K], b: &[f32; K, N], c: &mut [f32; M, N])`
    ([stage 10](../compiler/guide/stage-10-matrix-multiplication.md)),
    written as the plain scalar LLVM IR your lowering pass would emit for
    it: an `fmul` and an `fadd`, with no `contract` flag, matching the
    specification's rule. Run `llc -O2 -debug-pass=Structure` and
    `llc -O0 -debug-pass=Structure` on it, and write down, in your own
    notes rather than in code, three pass names from the `-O2` list you had
    not met before this chapter and what each does. Then run
    `llc -O2 -stop-after=finalize-isel` on the same fragment and read the
    MIR body: which of the kernel's values became `liveins`, and which
    instructions carry the multiply and the add. Finally, write one MIR
    test, following the MIR Language Reference,[^t4] that starts from a
    small hand-written `.mir` file and runs exactly one named pass over it
    with `-run-pass`, checked with FileCheck: pick a pass whose effect you
    can predict, such as the register coalescer removing a redundant copy,
    or dead-code elimination dropping a definition nothing reads.

    **Not yet.** Do not write your own SelectionDAG or GlobalISel path;
    that choice belongs to [stage 6](../compiler/guide/stage-6-first-machine-code.md#choosing-a-back-end),
    and this exercise is about reading LLVM's pipeline, not extending it.
    Do not try to explain every pass name in the `-O2` list; three you can
    explain accurately is worth more than twenty you cannot. Do not test
    your whole kernel's final assembly here: that belongs with
    [E4](e4-testing-backends.md)'s FileCheck-on-assembly approach. This
    test checks one pass's input against its output, nothing more.

    **Done when** you have a short written note mapping at least three
    `-O2`-only pass names to what they do, a `finalize-isel` MIR dump of
    one real fragment of your kernel's IR that you can read line by line
    and explain to someone else, and one MIR test with a `RUN` line and
    FileCheck directives that fails if you change which pass `-run-pass`
    names (proving the test exercises that specific pass, not something
    else that happens to produce similar output).

## Key ideas

!!! recap "You can now answer"

    - **What are the two frameworks `llc` can use to turn LLVM IR into machine instructions?** SelectionDAG, the `-O2` default, which builds a per-block DAG and combines, legalizes and selects on it; and GlobalISel, the `-O0` default, which works on whole functions already in MIR through IRTranslator, Legalizer, RegBankSelect and InstructionSelect.
    - **What is a MachineInstr, and what does it mean for one to be in SSA form?** One object per machine-level instruction, still using virtual registers, each one defined exactly once, before an allocator assigns physical registers.
    - **What is MIR, and why do `-stop-after`, `-stop-before` and `-run-pass` matter?** A readable, writable text form of MachineFunctions; those three flags let you capture the state before or after any pass and run exactly one pass on a saved snapshot, which turns "what does this pass do" into a question you answer by running a command.
    - **Why does `llc` fuse a multiply and an add into `fmadd` only when the add carries `contract`?** Because fusing changes the result's rounding, one instead of two; LLVM leaves that decision to whoever built the IR instead of deciding it itself during code generation.
    - **Which pass removes SSA's phi nodes, and roughly where does it sit in the `-O2` pipeline?** `PHIElimination`, which replaces each phi with copies along the predecessor edges, running after instruction selection and before register allocation.
    - **Why is it worth checking a claim from LLVM's own Code Generator documentation against the running tool?** The page says itself that it is a work in progress, and at least one of its examples, an option to `-regalloc`, no longer exists in current `llc`; `-debug-pass=Structure` and `--help-hidden` answer the same questions directly, from the tool you are using.

## Where this comes back

!!! next "You will use this again in"

    - [E2. Describing a target](e2-describing-a-target.md): *TableGen*, *where instruction-selection patterns and register classes come from*
    - [E3. LLVM's allocator, scheduler and MC layer](e3-llvm-allocator-scheduler-mc.md): *the greedy allocator in depth*, *the machine scheduler*, *MCInst, MCStreamer, fixups and relaxation*
    - [C3. Register allocation I: linear scan](c3-linear-scan.md): *a second allocator to compare against greedy*
    - [C6. Instruction scheduling](c6-scheduling.md): *what a machine scheduler pass is deciding, without LLVM's specific pass names*
    - [E4. Testing back ends](e4-testing-backends.md): *FileCheck on assembly and on MIR*, *`update_llc_test_checks.py`*

## Sources and further reading

[^t1]: LLVM Project, "The LLVM Target-Independent Code Generator". <https://llvm.org/docs/CodeGenerator.html>
[^t2]: LLVM Project, "Writing an LLVM Backend". <https://llvm.org/docs/WritingAnLLVMBackend.html>
[^t3]: LLVM Project, "GlobalISel". <https://llvm.org/docs/GlobalISel/index.html>
[^t4]: LLVM Project, "MIR Language Reference". <https://llvm.org/docs/MIRLangRef.html>
[^t12]: Lattner, "Intro to the LLVM MC Project", 9 April 2010. <https://blog.llvm.org/2010/04/intro-to-llvm-mc-project.html>
[^t13]: Olesen, "Greedy Register Allocation in LLVM 3.0", 18 September 2011. <https://blog.llvm.org/2011/09/greedy-register-allocation-in-llvm-30.html>
[^t14]: Braun, "Welcome to the Back End: The LLVM Machine Representation", 2017 LLVM Developers' Meeting. <https://llvm.org/devmtg/2017-10/slides/Braun-Welcome%20to%20the%20Back%20End.pdf>
[^t16]: Bogner, Nandakumar, Sanders, "Head First into GlobalISel", 2017 LLVM Developers' Meeting. <https://llvm.org/devmtg/2017-10/slides/Bogner-Nandakumar-Sanders-Head%20First%20into%20GlobalISel.pdf>
[^langref]: LLVM Project, "LLVM Language Reference Manual", section "Fast-Math Flags". <https://llvm.org/docs/LangRef.html#fast-math-flags>
[^m2]: Arm, A64 Instruction Set Architecture (DDI 0602). <https://developer.arm.com/documentation/ddi0602/latest>

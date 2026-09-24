# E3. LLVM's allocator, scheduler and MC layer

<p class="page-intro">How LLVM turns machine IR into a scheduled, register-allocated, byte-encoded program: the greedy allocator, the machine scheduler, and the MC layer that emits instructions as relocatable objects. Reading this pipeline closely is the fastest way to see which of your own back end's jobs are genuinely hard and which are bookkeeping.</p>

<p class="vx-meta" markdown="1">Level: Advanced · Reading time: about 30 minutes · Builds on: [E1. The LLVM code generator pipeline](e1-llvm-codegen-pipeline.md), [C3. Register allocation I: linear scan](c3-linear-scan.md), [C4. Register allocation II: graphs and SSA](c4-graph-coloring.md), [C5. Spilling, splitting and rematerialization](c5-spilling.md), [C6. Instruction scheduling](c6-scheduling.md), [B3. Object files and assemblers](b3-object-files.md)</p>

???+ remember "Before you start, remember"

    ??? question "What is a live range, and why does an allocator care about its length?"

        The span from where a value is computed to its last use. A shorter
        live range overlaps fewer other values, so it competes with fewer of
        them for the same physical registers.

        Introduced in [C2. Liveness](c2-liveness.md).

    ??? question "Why can two SSA values share one physical register even though SSA gives every value its own name?"

        SSA names are a compile-time fiction, not a hardware requirement.
        Two values can share a register whenever their live ranges never
        overlap: one has finished being used before the other is defined.

        Introduced in [C4. Register allocation II: graphs and SSA](c4-graph-coloring.md).

    ??? question "Why must Vortex's floating-point rules forbid the compiler from silently fusing a multiply and an add into one fused multiply-add?"

        A fused multiply-add rounds once instead of twice, which can change
        the result. Vortex requires every `+`, `-`, `*` and `/` to be one
        IEEE 754 operation, rounded to nearest, so any such fusion would
        make the compiler's output depend on an optimization the source
        program never asked for.
        [Specification, 4.4](../specification/types-and-values.md#44-floating-point-values).

    ??? question "What does an object file's symbol table let the linker do that the compiler alone cannot?"

        Join separately compiled pieces: a symbol marked defined tells the
        linker where a name lives, and a symbol marked undefined tells it
        which hole to fill with an address from somewhere else.

        Introduced in [6. The first machine code](../compiler/guide/stage-6-first-machine-code.md#from-object-file-to-executable).

!!! goals "In this chapter"

    - Name the concrete passes between instruction selection and code emission, and run them yourself with `llc -debug-pass=Structure`.
    - Explain the three outcomes the greedy register allocator can give a live range: a register, a split, or a spill.
    - Distinguish what the machine scheduler is free to reorder from what it must never reorder, and connect that limit to Vortex's floating-point rules.
    - Read MIR well enough to isolate and test one codegen pass in isolation.
    - Explain why even a call to a function defined in the same file becomes a relocation, not a resolved address, at the MC layer.

By the end of [E1](e1-llvm-codegen-pipeline.md), a function has become **MIR**:
machine instructions, still in SSA form, still using virtual registers that
have no fixed home yet. Chapters C3 to C6 built, by hand, on toy examples,
the three jobs that turn that MIR into a real program: choosing an order for
instructions, deciding where each value lives, and picking apart what goes
wrong when there are not enough registers to go around. This chapter reads
LLVM's own answers to those same three jobs, on the same target this whole
book series already trusts: AArch64, `llc` 18.1.8, run locally on this
machine (Apple clang 21, macOS 27, `apple-m1` core). Every pass name, every
line of assembly and every relocation shown below was produced by running
the commands yourself, not copied from documentation; the LLVM Code
Generator page itself carries a "work in progress" banner, and its own
worked allocator example uses a flag, `-regalloc=linearscan`, that this
build of `llc` rejects outright.[^t1] Braun's 2017 slide deck is a more
reliable map of the real pipeline, and this chapter follows its shape.[^t14]

## The pipeline, run for real

Ask `llc` what it plans to do before asking it to do it:

```text
$ llc -O2 -mtriple=arm64-apple-macos -debug-pass=Structure reduction_order.ll -o /dev/null
```

Buried inside a much longer list of target-independent passes (constant
folding, loop simplification, exception-handling lowering) is the sequence
this chapter is about, in this exact order:[^t1-local]

1. **AArch64 Instruction Selection**: LLVM IR becomes MachineInstr, still SSA.
2. **Live Variable Analysis**, **Two-Address instruction pass**, **Register Coalescer**: cleanup that removes instructions and copies a naive lowering introduced, before the expensive passes run.
3. **Machine Scheduler**: chooses an order for each basic block's instructions.
4. **Greedy Register Allocator**: assigns physical registers to virtual ones.
5. **Virtual Register Rewriter**: a mechanical pass that replaces every virtual register operand with the physical one the allocator chose.
6. **Prologue/Epilogue Insertion & Frame Finalization**: adds the function's entry and exit code, now that the allocator has decided which callee-saved registers were touched.

Steps 3 and 4 are this chapter's core: the **machine scheduler** decides
*when* each already-selected instruction runs, and the **greedy register
allocator** decides *where* each value lives while it runs. Step 6's object
file, and the MC layer that produces it, are the chapter's third piece.

## The greedy register allocator

Register allocation was covered on toy examples in linear scan
([C3](c3-linear-scan.md)), graph coloring ([C4](c4-graph-coloring.md)) and
spilling ([C5](c5-spilling.md)). LLVM's default allocator, `RegAllocGreedy`,
is none of those by name, but it borrows an idea from each: it processes
live ranges by priority, like a worklist allocator; it can evict a
lower-priority range from a register it already holds, the way graph
coloring's simplify/select rethinks assignments; and when it cannot find a
register at all, it does not spill outright: it first tries to **split** the
live range into smaller pieces and place only the expensive part in
memory.[^t13] For any one live range, the allocator ends in one of three
states: **assigned** to a physical register for its whole range, **split**
into two or more shorter ranges (each assigned or spilled independently), or
**spilled**, meaning the value is stored to a stack slot and reloaded at
each point it is used.

A live range that never has to compete for a register never sees any of
this machinery: it is assigned and nothing more happens. The next example makes four live
ranges compete for the same machine, on purpose, by giving each one work to
do across the whole function body.

--8<-- "includes/examples/backend/e3-llvm-allocator-scheduler-mc/four_accumulators.ll.md"

Four independent running sums (an unrolled reduction, the same shape
[P7](../optimize/p7-loop-transformations.md) studies for loops in general)
are combined only at the end, so each accumulator's live range spans
the entire function. Compiled for AArch64, `llc -O2` gives each one a
dedicated register, `s0` through `s3`, from the first load to the final
combine (observed locally, LLVM 18.1.8, `arm64-apple-macos`, `apple-m1`,
2026-09-24):

```text
	ldp	s0, s1, [x0]
	ldp	s2, s3, [x0, #8]
	ldp	s4, s5, [x0, #16]
	ldp	s6, s7, [x0, #24]
	fadd	s0, s0, s4
	fadd	s1, s1, s5
	fadd	s2, s2, s6
	fadd	s3, s3, s7
	; (two more groups of loads and fadds, same four registers)
	fadd	s0, s0, s1
	fadd	s1, s2, s3
	fadd	s0, s0, s1
```

`s0`, `s1`, `s2` and `s3` never change meaning across the whole function:
that is register allocation working exactly as C3 and C4 described it, on
real code. AArch64 has 32 floating-point registers, so four live ranges cost
nothing. Nothing in this chapter tells you the exact unroll factor at which
`RegAllocGreedy` starts to spill on this machine: that number depends on the
target, the version of LLVM and everything else alive in the function, and
this book will not invent it. The exercise below asks you to find it.

??? check "Why does the allocator give each accumulator a fixed register for the whole function, instead of reusing s0 for the second accumulator once the first one is momentarily idle?"

    Because it is never idle: every accumulator's live range runs from its
    first load to the final combine, so all four live ranges overlap for
    the entire function. There is no point at which two of them could share
    one register without one clobbering the other.

## The machine scheduler: choosing an order, not a value

Instruction scheduling ([C6](c6-scheduling.md)) reorders already-selected
instructions to hide latency: while one instruction's result is not ready
yet, the processor can be doing other, independent work. LLVM's pre-register-
allocation **MachineScheduler** builds a small dependency DAG for each basic
block (an SUnit per instruction, an edge for every true dependency) and
picks an order that respects every edge, using a scheduling model built from
target-specific latency and throughput data. It is completely free to
reorder two instructions with no edge between them; it is never free to
reorder two that do, because that would change what the program computes,
not only when it runs.

That distinction is exactly the line Vortex's floating-point rules draw.
Reordering *when* an addition executes never changes its result. Changing
*which* additions happen, or in what association, can: floating-point
addition is not associative, so `(a + b) + c` and `a + (b + c)` can round to
different `f32` values. Vortex's specification forbids the compiler from
reassociating floating-point operations for exactly this reason.[^spec-fp]
The scheduler never violates that rule, because it only ever changes
execution order among instructions that were already independent; an
associativity change would require inventing a new dependency graph, which
is a job for a different kind of pass (and one LLVM's default pipeline does
not run on plain IR without an explicit `contract` or `reassoc` flag).[^t18-local]

The next example makes the distinction concrete: two functions that add the
same four numbers, with two different dependency graphs.

--8<-- "includes/examples/backend/e3-llvm-allocator-scheduler-mc/reduction_order.ll.md"

`chain_sum` folds left to right: each `fadd` genuinely depends on the one
before it, so there is exactly one legal order, and `llc -O2` emits the
three additions in that one order, no matter what. `tree_sum` groups the
four numbers into two independent pairs, combined last: the two partial
sums have no dependency on each other, and the scheduler is free to run them
in either order. On this machine, it chooses to compute the second pair
first (observed locally, same build and date as above):

```text
_tree_sum:
	fadd	s2, s2, s3
	fadd	s0, s0, s1
	fadd	s0, s0, s2
```

Nothing here changes what `tree_sum` computes: it still adds `%a+%b` and
`%c+%d` before adding those two partial sums, exactly as the IR said. What
changed is only the order those two additions run in, and the scheduler
chose that order because it had a real choice, which `chain_sum` never
gave it.

??? check "chain_sum and tree_sum both compute a sum of four numbers, but Vortex's floating-point rules treat them as two different operations, not one. Why?"

    Because they can round differently. `chain_sum` computes
    `((a + b) + c) + d`; `tree_sum` computes `(a + b) + (c + d)`. IEEE 754
    addition is not associative, so for some inputs these two evaluation
    orders produce different `f32` results. A compiler is never allowed to
    turn one into the other on its own, which is a stronger and different
    guarantee from "the scheduler may not reorder two dependent
    instructions."

## MIR: the pipeline made visible and testable

Every stage above operates on **MIR**, machine IR: a textual, YAML-based
serialization of a MachineFunction that `llc` can print and re-read.[^t4]
Two flags turn the whole pipeline into something you can stop, inspect and
resume one pass at a time: `-stop-after=<pass>` writes MIR after the named
pass finishes, and `-run-pass=<pass>` reads MIR back in and runs exactly one
pass on it. `llc -mtriple=arm64-apple-macos -stop-after=finalize-isel
four_accumulators.ll -o accs.mir` captures the function immediately after
instruction selection, before the scheduler or allocator have touched it;
`llc -run-pass=greedy accs.mir -o -` then runs only the greedy allocator on
that saved state. This is the same technique LLVM's own test suite uses to
test one codegen pass at a time, independent of everything upstream of
it,[^t10] and it is the mechanism the exercise below asks you to borrow.

## The MC layer: instructions become bytes, calls become relocations

Once the allocator, the rewriter and the prologue/epilogue pass have run,
every instruction refers only to physical registers and concrete stack
offsets. The **MC layer** (`MCInst`, the in-memory form of one encoded
instruction, and `MCStreamer`, an interface with two implementations: one
that prints assembly text, one that emits object-file bytes) turns that
final MachineInstr stream into either form, from the same code.[^t12] Doing
this at the target-independent MC layer, rather than the target-independent
codegen layer that came before it, is what lets `llc -filetype=asm` and
`llc -filetype=obj` produce the same program in two different containers
from one pipeline.

A **fixup** is what the MC layer records whenever an instruction's encoding
depends on an address it does not know yet: a branch to a label later in the
function, or a call to a symbol defined in another file entirely. B3
develops this idea by hand, in a hand-written assembler, for straight-line
forward branches.[^b3-see] LLVM's MC layer generalizes it: every call
becomes a fixup at the point it is encoded, and the object file's relocation
table is where an unresolved fixup ends up once assembly finishes, to be
patched by the linker.

The next example asks a question whose answer is worth guessing before
reading on: does a call to a function defined in the same file need a
relocation at all?

--8<-- "includes/examples/backend/e3-llvm-allocator-scheduler-mc/calls_and_fixups.ll.md"

`caller` calls `internal_helper`, defined two functions down in the same
module; `internal_helper` calls `external_sink`, only declared. Compile both
straight to an object file and disassemble it (observed locally, same build
and date as above):

```text
$ llc -O2 -mtriple=arm64-apple-macos -filetype=obj calls_and_fixups.ll -o cf.o
$ llvm-objdump -dr cf.o
0000000000000000 <ltmp0>:
       0: a9bf7bfd     stp   x29, x30, [sp, #-0x10]!
       4: 94000000     bl    0x4 <ltmp0+0x4>
                0000000000000004:  ARM64_RELOC_BRANCH26  _external_sink
       8: a8c17bfd     ldp   x29, x30, [sp], #0x10
       c: d65f03c0     ret

0000000000000010 <_caller>:
      10: a9bf7bfd     stp   x29, x30, [sp, #-0x10]!
      14: 94000000     bl    0x14 <_caller+0x4>
                0000000000000014:  ARM64_RELOC_BRANCH26  _internal_helper
      18: a8c17bfd     ldp   x29, x30, [sp], #0x10
      1c: d65f03c0     ret
```

Both calls carry an `ARM64_RELOC_BRANCH26` relocation, including the one to
`internal_helper`, defined a few bytes away in the same object. LLVM's
MC layer does not special-case "the target happens to be nearby, in this
same translation unit": it emits every symbol reference the same way,
because it compiles one function at a time and never assumes it knows the
final layout of the object it is building until the linker has placed every
section. This is also why branch range limits matter at all: AArch64's
unconditional branch-and-link reaches only ±128 MiB from the instruction
that uses it, `B.cond` only ±1 MiB, and `TBZ` only ±32 KiB, all computed
from the field width the relocation's addend must fit inside; a linker that
cannot satisfy one of those limits inserts a veneer, a short trampoline,
which may clobber `x16`/`x17` under the platform's reserved-register
rules.[^m5][^m8]

??? check "Why does even a call to a function defined earlier in the same file get a relocation, instead of the assembler computing the branch offset itself?"

    Because MC compiles and lays out code incrementally and does not commit
    to a final address for any symbol until the whole object's sections are
    assembled; a later pass (function splitting, section reordering, or
    linking against other objects) could still move things around.
    Deferring every cross-symbol reference to a relocation, resolved once,
    is simpler and more uniform than tracking which ones happen to be safe
    to resolve early.

<figure class="vx-figure">
<svg viewBox="0 0 920 300" role="img" aria-labelledby="e3-belt-title e3-belt-desc">
<title id="e3-belt-title">The MIR pipeline from instruction selection to object bytes</title>
<desc id="e3-belt-desc">Eight stages in a row, each a box connected by an arrow to the next: LLVM IR, Instruction Selection, SSA cleanup (two-address and coalescing), Machine Scheduler, Greedy Register Allocator, Virtual Register Rewriter, Prologue and Epilogue Insertion, and the MC layer producing bytes. The Machine Scheduler and Greedy Register Allocator boxes are highlighted as this chapter's core; the MC layer box is highlighted as the chapter's third topic.</desc>
<g class="vx-seq" style="--vx-i: 0; --vx-n: 8">
<rect class="vx-box-strong" x="10" y="90" width="92" height="60" rx="4"/>
<text class="vx-text" x="56" y="115" text-anchor="middle">LLVM IR</text>
<text class="vx-text-muted" x="56" y="132" text-anchor="middle">SSA values</text>
</g>
<g class="vx-seq" style="--vx-i: 1; --vx-n: 8">
<rect class="vx-box" x="122" y="90" width="98" height="60" rx="4"/>
<text class="vx-text" x="171" y="112" text-anchor="middle" font-size="12">Instruction</text>
<text class="vx-text" x="171" y="127" text-anchor="middle" font-size="12">selection</text>
<text class="vx-text-muted" x="171" y="143" text-anchor="middle">MI, SSA</text>
</g>
<g class="vx-seq" style="--vx-i: 2; --vx-n: 8">
<rect class="vx-box" x="240" y="90" width="98" height="60" rx="4"/>
<text class="vx-text" x="289" y="112" text-anchor="middle" font-size="12">Two-address,</text>
<text class="vx-text" x="289" y="127" text-anchor="middle" font-size="12">coalesce</text>
<text class="vx-text-muted" x="289" y="143" text-anchor="middle">SSA cleanup</text>
</g>
<g class="vx-seq" style="--vx-i: 3; --vx-n: 8">
<rect class="vx-box-accent" x="358" y="84" width="98" height="72" rx="4"/>
<text class="vx-text-accent" x="407" y="108" text-anchor="middle" font-size="12">Machine</text>
<text class="vx-text-accent" x="407" y="123" text-anchor="middle" font-size="12">Scheduler</text>
<text class="vx-text-muted" x="407" y="145" text-anchor="middle" font-size="11">orders MI</text>
</g>
<g class="vx-seq" style="--vx-i: 4; --vx-n: 8">
<rect class="vx-box-accent" x="476" y="84" width="98" height="72" rx="4"/>
<text class="vx-text-accent" x="525" y="102" text-anchor="middle" font-size="12">Greedy</text>
<text class="vx-text-accent" x="525" y="117" text-anchor="middle" font-size="12">register</text>
<text class="vx-text-accent" x="525" y="132" text-anchor="middle" font-size="12">allocator</text>
<text class="vx-text-muted" x="525" y="150" text-anchor="middle" font-size="11">vreg &#8594; preg</text>
</g>
<g class="vx-seq" style="--vx-i: 5; --vx-n: 8">
<rect class="vx-box" x="594" y="90" width="98" height="60" rx="4"/>
<text class="vx-text" x="643" y="112" text-anchor="middle" font-size="12">Virtual reg.</text>
<text class="vx-text" x="643" y="127" text-anchor="middle" font-size="12">rewriter</text>
<text class="vx-text-muted" x="643" y="143" text-anchor="middle">mechanical</text>
</g>
<g class="vx-seq" style="--vx-i: 6; --vx-n: 8">
<rect class="vx-box" x="712" y="90" width="98" height="60" rx="4"/>
<text class="vx-text" x="761" y="108" text-anchor="middle" font-size="11">Prologue /</text>
<text class="vx-text" x="761" y="122" text-anchor="middle" font-size="11">epilogue</text>
<text class="vx-text-muted" x="761" y="138" text-anchor="middle" font-size="10">frame finalized</text>
</g>
<g class="vx-seq" style="--vx-i: 7; --vx-n: 8">
<rect class="vx-box-accent" x="830" y="90" width="82" height="60" rx="4"/>
<text class="vx-text-accent" x="871" y="112" text-anchor="middle" font-size="12">MC layer</text>
<text class="vx-text-muted" x="871" y="129" text-anchor="middle" font-size="10">MCInst,</text>
<text class="vx-text-muted" x="871" y="143" text-anchor="middle" font-size="10">fixups</text>
</g>
<g class="vx-line">
<line x1="102" y1="120" x2="118" y2="120"/>
<line x1="220" y1="120" x2="236" y2="120"/>
<line x1="338" y1="120" x2="354" y2="120"/>
<line x1="456" y1="120" x2="472" y2="120"/>
<line x1="574" y1="120" x2="590" y2="120"/>
<line x1="692" y1="120" x2="708" y2="120"/>
<line x1="810" y1="120" x2="826" y2="120"/>
</g>
<g class="vx-arrowhead">
<polygon points="118,120 110,116 110,124"/>
<polygon points="236,120 228,116 228,124"/>
<polygon points="354,120 346,116 346,124"/>
<polygon points="472,120 464,116 464,124"/>
<polygon points="590,120 582,116 582,124"/>
<polygon points="708,120 700,116 700,124"/>
<polygon points="826,120 818,116 818,124"/>
</g>
<text class="vx-text-muted" x="460" y="225" text-anchor="middle">Pass names and order observed locally: `llc -O2 -mtriple=arm64-apple-macos -debug-pass=Structure`, LLVM 18.1.8, 2026-09-24.</text>
<text class="vx-text-muted" x="460" y="245" text-anchor="middle">Highlighted stages: this chapter's three subjects, in pipeline order.</text>
</svg>
<figcaption>Figure 1. The MIR pipeline from LLVM IR to object bytes, as observed on this machine. Every basic block passes through the scheduler once, before allocation; every function passes through the allocator once, before the rewriter makes its choice permanent. The MC layer at the far right is a separate library, reused by the assembler text printer, the object-file writer, and (E2 covers TableGen's role in generating the tables it consults) the JIT.</figcaption>
</figure>

## Watching a schedule without running it: llvm-mca

`llc`'s machine scheduler decides an order once, at compile time, and moves
on. A separate tool, `llvm-mca`, takes a finished sequence of instructions
and a `-mcpu` scheduling model and simulates how a specific microarchitecture
would execute it: how many cycles the whole block takes, how full its
issue ports run, whether a value's user is stalled waiting for it.[^t7] It
answers a different question from the scheduler pass: not "in what order
should these instructions run" but "given this order, how well does this
model of this microarchitecture do." Run against `four_accumulators`'
compiled body with `-mcpu=apple-m1`, `llvm-mca` produces a report of modeled
cycles and per-instruction port pressure; this is a **model**, built from
LLVM's scheduling description of the target, not a measurement taken on
real hardware, and the tool says so in its own documentation.[^t7] [P5](../optimize/p5-microarchitecture.md)
returns to `llvm-mca` and to the gap between a modeled schedule and a
measured one.

## For Vortex

!!! vortex "Exercise"

    **Build.** Nothing in your own compiler yet: this chapter's job is to
    read LLVM's pipeline closely enough to know what your own C3 to C6 work
    is standing in for. Instead, build an experiment. Take the dot-product
    or matmul kernel you lowered to IR in earlier stages, or write a small
    Vortex-shaped one by hand as plain LLVM IR (no Vortex compiler code
    involved), and unroll its reduction by increasing factors: 4, 8, 16, 32
    accumulators. For each one, compile it with `llc -O2` for your own
    machine's target and record, from the emitted assembly alone, how many
    of the accumulators still live in a register for the whole function and
    at what factor spill code (loads and stores through the stack pointer)
    first appears.

    **Do not build yet.** Your own register allocator; C3 to C5 are where
    that begins, once you have decided which allocation strategy your back
    end will use. Any attempt to force LLVM to spill by a specific,
    predicted amount; the goal here is to observe the real threshold on
    your own machine, not to hit a number stated in advance.

    **The test that proves it works.** A short table: unroll factor, number
    of registers the allocator kept live throughout, whether any spill code
    appeared. State the machine, the `llc` version and the date, the same
    way this chapter's own numbers are stated. If your target has more or
    fewer allocatable floating-point registers than AArch64's 32, say how
    many, and where you read that count.

## Key ideas

!!! recap

    - **What three outcomes can the greedy register allocator choose for one live range?** Assign it a physical register for its whole span, split it into shorter pieces handled separately, or spill it to a stack slot.
    - **What is the machine scheduler allowed to reorder, and what is it never allowed to reorder?** It may reorder any two instructions with no data dependency between them; it may never reorder two that do, because that would change what the program computes, not only the order it runs in.
    - **Why does the scheduler never violate Vortex's floating-point rules, even though it changes instruction order?** Because reordering independent instructions never changes which values feed which operation or how many roundings occur; only reassociation or contraction would, and the scheduler does neither.
    - **What is MIR, and what does `-stop-after` / `-run-pass` let you do with it?** A textual, re-readable serialization of a MachineFunction; together the two flags let you capture the function at one point in the pipeline and run exactly one later pass on that saved state, in isolation.
    - **Why does a call to a function defined in the same file still need a relocation?** Because the MC layer commits to a symbol's final address only once the whole object's layout is fixed, and it treats every symbol reference the same way regardless of where it happens to be defined.
    - **What is the difference between what `llc`'s machine scheduler does and what `llvm-mca` does?** The scheduler pass chooses an instruction order once, at compile time; `llvm-mca` takes a finished order and models how a specific microarchitecture would run it, without compiling or running anything.

## Where this comes back

!!! next "You will use this again in"

    - [E4. Testing back ends](e4-testing-backends.md): *MIR tests that isolate one pass*, *`llvm-mc` as an encoding oracle*
    - [D2. JIT compilation](d2-jit.md): *the same MC layer, now writing bytes into memory instead of into a file*
    - [D3. Reading real back ends](d3-real-backends.md): *regalloc2's Ion allocator, a different answer to the same allocate/split/spill decision*
    - [P5. The microarchitecture shelf](../optimize/p5-microarchitecture.md): *`llvm-mca`'s scheduling model, and where a model and a measurement can disagree*

## Sources and further reading

This chapter's pass list, assembly and relocations were all produced by
running `llc`, `opt` and `llvm-objdump` locally (LLVM 18.1.8, Apple clang
21, macOS 27, `apple-m1`), not transcribed from a secondary source. LLVM's
own Code Generator page is the starting reference for the pipeline's shape,
but it is explicitly unfinished in places this chapter had to work around;
Braun's slide deck and Olesen's allocator write-up are more current
descriptions of the same passes.[^t1][^t14][^t13]

[^t1]: LLVM Project, "The LLVM Target-Independent Code Generator" (carries a "Work In Progress" notice; its register-allocator example passes `-regalloc=linearscan`, which `llc` 18.1.8 rejects with "Cannot find option named 'linearscan'", checked locally). <https://llvm.org/docs/CodeGenerator.html>
[^t1-local]: Pass list and order observed locally: `llc -O2 -mtriple=arm64-apple-macos -debug-pass=Structure`, LLVM 18.1.8, `apple-m1`, 2026-09-24.
[^t14]: Matthias Braun, "Welcome to the Back End: The LLVM Machine Representation", LLVM Developers' Meeting 2017. <https://llvm.org/devmtg/2017-10/slides/Braun-Welcome%20to%20the%20Back%20End.pdf>
[^t13]: Jakob Olesen, "Greedy Register Allocation in LLVM 3.0", LLVM Blog, 18 September 2011. <https://blog.llvm.org/2011/09/greedy-register-allocation-in-llvm-30.html>
[^t4]: LLVM Project, "MIR Language Reference Manual". <https://llvm.org/docs/MIRLangRef.html>
[^t10]: LLVM Project, "LLVM Testing Infrastructure Guide". <https://llvm.org/docs/TestingGuide.html>
[^t12]: Chris Lattner, "Intro to the LLVM MC Project", LLVM Blog, 9 April 2010. <https://blog.llvm.org/2010/04/intro-to-llvm-mc-project.html>
[^t7]: LLVM Project, `llvm-mca` command guide. <https://llvm.org/docs/CommandGuide/llvm-mca.html>
[^t18-local]: Observed locally: `opt`/`llc` do not fuse a plain `fmul`/`fadd` pair into `fmadd` on AArch64 without a `contract` fast-math flag on the IR, LLVM 18.1.8, 2026-09-24.
[^m5]: Arm, "ELF for the Arm 64-bit Architecture (AAELF64)", 2025Q4 release, relocation types `R_AARCH64_JUMP26`/`CALL26`, `CONDBR19`, `TSTBR14`. <https://github.com/ARM-software/abi-aa/blob/main/aaelf64/aaelf64.rst>
[^m8]: Apple, "Writing ARM64 code for Apple platforms" (`x16`/`x17` as the platform's reserved scratch registers for veneers and the linker). <https://developer.apple.com/documentation/xcode/writing-arm64-code-for-apple-platforms>
[^spec-fp]: [Vortex specification, 4.4, "Floating-point values"](../specification/types-and-values.md#44-floating-point-values): "An implementation must not contract operations ... reassociate or reorder them, evaluate them in a wider format, or flush subnormal inputs or results to zero."
[^b3-see]: [B3. Object files and assemblers](b3-object-files.md), on forward-branch fixups in a hand-written assembler.

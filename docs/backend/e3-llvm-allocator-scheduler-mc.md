# E3. LLVM's allocator, scheduler and MC layer

<p class="page-intro">This chapter follows three of LLVM's back-end passes on real code: the greedy register allocator, the machine scheduler, and the MC layer that turns final instructions into text or bytes. Each one does a job your own back end does in a simpler way, so reading LLVM's version shows you what a production answer adds and gives you a reference to measure your own against.</p>

<p class="vx-meta" markdown="1">Level: Advanced · Reading time: about 50 minutes · Builds on: [E1. The LLVM code generator pipeline](e1-llvm-codegen-pipeline.md), [C3. Register allocation I: linear scan](c3-linear-scan.md), [C4. Register allocation II: graphs and SSA](c4-graph-coloring.md), [C5. Spilling, splitting and rematerialization](c5-spilling.md), [C6. Instruction scheduling](c6-scheduling.md), [B3. Object files and assemblers](b3-object-files.md)</p>

???+ remember "Before you start, remember"

    ??? question "What is a live range, and why does an allocator care about its length?"

        The stretch of the program from where a value is computed to its
        last use. A longer live range overlaps more other values, so it
        competes with more of them for the same registers.

        Introduced in [C2. Liveness](c2-liveness.md).

    ??? question "Which floating-point registers must a function called on AArch64 leave as it found them?"

        The low 64 bits of `v8` to `v15`, the registers `d8` to `d15`. Every
        other SIMD and floating-point register may be overwritten by the
        callee, so a value that must survive a call either lives in `d8` to
        `d15` or is saved to memory around the call.

        Introduced in [A4. Calling conventions and ABIs](a4-calling-conventions.md).

    ??? question "Besides true dependences, which edges does a scheduler's dependence graph need?"

        Edges for anti-dependences (a read that must happen before a later
        write to the same register), output dependences (two writes to the
        same register) and memory ordering (a store and another access that
        may touch the same address).

        Introduced in [C6. Instruction scheduling](c6-scheduling.md).

    ??? question "What is the difference between a fixup and a relocation?"

        A fixup is the assembler's own note that some bits of an instruction
        wait for an address. When the assembler can compute the address, it
        patches the bits itself; when it cannot, the note becomes a
        relocation in the object file, for the linker to apply.

        Introduced in [B3. Object files and assemblers](b3-object-files.md).

    ??? question "What may a Vortex compiler never do to `f32` and `f64` arithmetic?"

        Contract operations (fuse a multiply and an add into one rounding),
        reassociate or reorder them, evaluate them in a wider format, or
        flush subnormal values to zero. Each operation is one IEEE 754
        operation, rounded to nearest with ties to even.

        [Specification, 4.4](../specification/types-and-values.md#44-floating-point-values). Decision: [record 56](../decisions/numbers.md#d56).

!!! goals "In this chapter"

    - Walk LLVM's greedy register allocator through assignment, eviction, splitting and spilling on a small example by hand.
    - Explain from a real listing why the greedy allocator keeps a loop free of stack traffic where the fast allocator does not.
    - Predict what the machine scheduler may reorder, show that its choices depend on the scheduling model, and state why no choice it makes can change a floating-point result.
    - Trace one instruction from `MachineInstr` through `MCInst` to bytes, and decide which fixups become relocations in Mach-O and in ELF objects.
    - Recognize branch relaxation on AArch64 and tell it apart from the relaxation an x86 assembler performs.

Here is the loop from one function this chapter compiles, as `llc -O2`
printed it for an Apple M1-class core:

```text
LBB0_1:                                 ; %loop
	mov	x0, x20
	bl	_tick
	add	w20, w20, #1
	cmp	w20, w19
	b.lt	LBB0_1
```

Ten `f32` values are live across every one of those calls, and a call may
destroy most floating-point registers. Yet the loop touches no memory. Three
passes are responsible for listings like this one. The **register
allocator** decided where each value lives, the **machine scheduler**
decided the order of the instructions around it, and the **MC layer** (LLVM's
library for machine code) turned the result into this text or into bytes.
[C3](c3-linear-scan.md) to [C6](c6-scheduling.md) built simple versions of
the first two, and [B3](b3-object-files.md) built a simple assembler. This
chapter reads LLVM's versions.

All listings were produced on the owner's machine: an Apple M4 Pro, macOS 27,
`llc`, `llvm-mc` and `llvm-mca` 18.1.8, on 2026-09-24, with
`-mtriple=arm64-apple-macos`. Where a listing depends on the processor, the
text says which `-mcpu` produced it. LLVM 18 does not know the M4, so
`apple-m1` is the closest model it has.

## Where the three jobs sit

`llc -O2 -debug-pass=Structure` prints the passes `llc` will run, in order.
For AArch64 on this machine it printed more than two hundred lines. Figure 1 keeps the
passes this chapter is about and the ones around them.

<figure class="vx-figure">
<svg viewBox="0 0 760 250" role="img" aria-label="The llc -O2 pass order from instruction selection to the assembly printer, with the scheduler, the allocator, the post-RA scheduler and the MC layer highlighted" aria-describedby="e3-pipe-desc">
<title id="e3-pipe-title">The code generator passes around this chapter's three subjects</title>
<desc id="e3-pipe-desc">Two rows of boxes joined by arrows. First row: LLVM IR; instruction selection; SSA machine passes such as LICM, CSE and sinking; PHI elimination, two-address conversion and the register coalescer; and the machine scheduler, highlighted, which runs before register allocation. A line leads from the end of the first row to the start of the second. Second row: the greedy register allocator, highlighted; the virtual register rewriter; prologue and epilogue insertion; the post-RA machine scheduler, highlighted; and branch relaxation followed by the assembly printer, which lowers MachineInstr to MCInst for the MC layer, highlighted.</desc>
<g class="vx-seq" style="--vx-i: 0; --vx-n: 10">
<rect class="vx-box-strong" x="10" y="20" width="130" height="64" rx="4"/>
<text class="vx-text" x="75" y="47" text-anchor="middle">LLVM IR</text>
<text class="vx-text-muted" x="75" y="66" text-anchor="middle">SSA values</text>
</g>
<g class="vx-seq" style="--vx-i: 1; --vx-n: 10">
<rect class="vx-box" x="160" y="20" width="130" height="64" rx="4"/>
<text class="vx-text" x="225" y="42" text-anchor="middle" font-size="12">Instruction</text>
<text class="vx-text" x="225" y="57" text-anchor="middle" font-size="12">selection</text>
<text class="vx-text-muted" x="225" y="74" text-anchor="middle">MachineInstr, SSA</text>
</g>
<g class="vx-seq" style="--vx-i: 2; --vx-n: 10">
<rect class="vx-box" x="310" y="20" width="130" height="64" rx="4"/>
<text class="vx-text" x="375" y="42" text-anchor="middle" font-size="12">SSA machine</text>
<text class="vx-text" x="375" y="57" text-anchor="middle" font-size="12">passes</text>
<text class="vx-text-muted" x="375" y="74" text-anchor="middle">LICM, CSE, sinking</text>
</g>
<g class="vx-seq" style="--vx-i: 3; --vx-n: 10">
<rect class="vx-box" x="460" y="20" width="130" height="64" rx="4"/>
<text class="vx-text" x="525" y="42" text-anchor="middle" font-size="12">PHI elimination,</text>
<text class="vx-text" x="525" y="57" text-anchor="middle" font-size="12">two-address</text>
<text class="vx-text-muted" x="525" y="74" text-anchor="middle">register coalescer</text>
</g>
<g class="vx-seq" style="--vx-i: 4; --vx-n: 10">
<rect class="vx-box-accent" x="610" y="20" width="140" height="64" rx="4"/>
<text class="vx-text-accent" x="680" y="42" text-anchor="middle">Machine</text>
<text class="vx-text-accent" x="680" y="57" text-anchor="middle">scheduler</text>
<text class="vx-text-muted" x="680" y="74" text-anchor="middle">before allocation</text>
</g>
<g class="vx-seq" style="--vx-i: 5; --vx-n: 10">
<rect class="vx-box-accent" x="10" y="160" width="130" height="64" rx="4"/>
<text class="vx-text-accent" x="75" y="182" text-anchor="middle">Greedy</text>
<text class="vx-text-accent" x="75" y="197" text-anchor="middle">allocator</text>
<text class="vx-text-muted" x="75" y="214" text-anchor="middle">vreg to register</text>
</g>
<g class="vx-seq" style="--vx-i: 6; --vx-n: 10">
<rect class="vx-box" x="160" y="160" width="130" height="64" rx="4"/>
<text class="vx-text" x="225" y="182" text-anchor="middle" font-size="12">Virtual register</text>
<text class="vx-text" x="225" y="197" text-anchor="middle" font-size="12">rewriter</text>
<text class="vx-text-muted" x="225" y="214" text-anchor="middle">applies the choice</text>
</g>
<g class="vx-seq" style="--vx-i: 7; --vx-n: 10">
<rect class="vx-box" x="310" y="160" width="130" height="64" rx="4"/>
<text class="vx-text" x="375" y="182" text-anchor="middle" font-size="12">Prologue and</text>
<text class="vx-text" x="375" y="197" text-anchor="middle" font-size="12">epilogue</text>
<text class="vx-text-muted" x="375" y="214" text-anchor="middle">callee-saved saves</text>
</g>
<g class="vx-seq" style="--vx-i: 8; --vx-n: 10">
<rect class="vx-box-accent" x="460" y="160" width="130" height="64" rx="4"/>
<text class="vx-text-accent" x="525" y="182" text-anchor="middle">Post-RA</text>
<text class="vx-text-accent" x="525" y="197" text-anchor="middle">scheduler</text>
<text class="vx-text-muted" x="525" y="214" text-anchor="middle">real registers</text>
</g>
<g class="vx-seq" style="--vx-i: 9; --vx-n: 10">
<rect class="vx-box-accent" x="610" y="160" width="140" height="64" rx="4"/>
<text class="vx-text-accent" x="680" y="182" text-anchor="middle">Branch relaxation,</text>
<text class="vx-text-accent" x="680" y="197" text-anchor="middle">asm printer</text>
<text class="vx-text-muted" x="680" y="214" text-anchor="middle">to MCInst, MC layer</text>
</g>
<g class="vx-line">
<line x1="140" y1="52" x2="156" y2="52"/>
<line x1="290" y1="52" x2="306" y2="52"/>
<line x1="440" y1="52" x2="456" y2="52"/>
<line x1="590" y1="52" x2="606" y2="52"/>
<polyline points="680,84 680,122 75,122 75,156"/>
<line x1="140" y1="192" x2="156" y2="192"/>
<line x1="290" y1="192" x2="306" y2="192"/>
<line x1="440" y1="192" x2="456" y2="192"/>
<line x1="590" y1="192" x2="606" y2="192"/>
</g>
<g class="vx-arrowhead">
<polygon points="156,52 148,48 148,56"/>
<polygon points="306,52 298,48 298,56"/>
<polygon points="456,52 448,48 448,56"/>
<polygon points="606,52 598,48 598,56"/>
<polygon points="75,160 71,152 79,152"/>
<polygon points="156,192 148,188 148,196"/>
<polygon points="306,192 298,188 298,196"/>
<polygon points="456,192 448,188 448,196"/>
<polygon points="606,192 598,188 598,196"/>
</g>
</svg>
<figcaption>Figure 1. The part of the <code>llc -O2</code> pipeline this chapter reads, in the order <code>-debug-pass=Structure</code> printed it for AArch64 (LLVM 18.1.8). Many passes between these boxes are left out. The scheduler runs twice: once on virtual registers before allocation and once on physical registers after it. At <code>-O0</code> the same command shows GlobalISel for selection, the fast allocator instead of greedy, and no machine scheduler.</figcaption>
</figure>

The order matters. The pre-allocation scheduler works on virtual registers,
so it may reorder freely but must watch how many values it keeps alive at
once. The allocator then sees that order as fixed. The **virtual register
rewriter** replaces each virtual register with the physical one the
allocator chose, and only after that does prologue and epilogue insertion
know which callee-saved registers the function touched and must save.

Braun's tutorial on LLVM's machine representation lays out the same
stages.[^braun] LLVM's own Code Generator guide describes the pipeline too,
but read it with care: it carries a "work in progress" warning, several of
its sections still say "To Be Written", and its example
`llc -regalloc=linearscan` fails on `llc` 18.1.8 with "Cannot find option
named 'linearscan'".[^cg]

The guide's list of allocators is still accurate. **Fast** works one basic
block at a time and is the default for debug builds. **Basic** assigns live
ranges one at a time in priority order and serves as a baseline. **Greedy**
is the default, and **PBQP** solves allocation as a numerical optimization
problem.[^cg] `-regalloc=fast`, `basic`, `greedy` and `pbqp` all work in
`llc` 18.1.8.

## The greedy allocator

LLVM used linear scan ([C3](c3-linear-scan.md)) from 2004 until release
3.0. Jakob Olesen, who wrote its replacement, gives the reason: when every
register was taken, linear scan spilled a whole live range, and splitting it
instead would have required the scan to go back over ranges it had already
passed.[^olesen] A later rewriter pass cleaned up the resulting mess, and by
his account it took about half of linear scan's compile time.[^olesen] He
reported that the greedy allocator's code was 1 to 2% smaller and up to 10%
faster than linear scan's, measured in 2011 for LLVM 3.0.[^olesen]

Greedy keeps linear scan's live intervals but drops the fixed visiting
order. It keeps a **priority queue** of live ranges not yet assigned, and it
takes the largest first.[^olesen] Large ranges then get the pick of the
registers, and small ranges fit into the gaps between them. For each
physical register, a **live interval union** records the ranges already
assigned to it, so the allocator can ask "does this range overlap anything
in `d9`?" without building an interference graph ([C4](c4-graph-coloring.md)).
LLVM keeps these unions per register unit in an analysis called the
`LiveRegMatrix`, which appears in the pass list shortly before the
allocator.[^matrix]

### Spill weight: what a range is worth

When two ranges want the same register, greedy compares their **spill
weight**, an estimate of how much it would cost to keep the range in memory
instead. LLVM 18 computes it as the expected number of times the range's
definitions and uses execute per call of the function, divided by the
range's length plus a constant:[^weights]

$$
\text{weight} = \frac{\sum \text{def and use frequencies}}{K + \text{size}}
$$

The frequencies come from block frequencies, so a use inside a loop counts
many times over, and the constant $K$ keeps a short range from getting an
enormous weight by accident.[^weights] A long range used twice has a low
weight. A short range used ten times per trip around a hot loop has a high
one.

### Assign, evict, split, spill

Greedy tries four things for a range, in this order, and records how far
each range has got in a field LLVM calls its **stage**: `RS_Assign`,
`RS_Split`, `RS_Split2`, `RS_Spill`, `RS_Memory` and `RS_Done`.[^stages]

1. **Assign.** Take a register the range does not overlap in any live
   interval union.
2. **Evict.** Take a register whose occupants all have a lower spill weight,
   unassign them, and put them back in the queue, where they get a second
   chance at some other register.[^olesen]
3. **Split.** Cut the range into pieces around the regions where registers
   are scarce, and queue the pieces. A piece that covers a busy loop now has
   a higher weight, since it is shorter but has the same uses, so it may win
   a register that the whole range could not.[^olesen]
4. **Spill.** Only when the splitter decides that splitting will not help:
   store the value to a stack slot and reload it before each use.[^olesen]

Olesen's summary of the effect is that a range "may spill outside the loop
where it was idle anyway".[^olesen] Where a split piece is stored and
reloaded is chosen between basic blocks by a separate analysis, **spill
placement**, which decides for each group of CFG edges whether the value
crosses it in a register or in a stack slot.[^spillplace]

### The walk, by hand

The example has two registers, R1 and R2, and three live ranges over
positions 0 to 18. A loop covers positions 6 to 14. Assume the loop runs ten
times per call, and ignore the constant $K$.

| Range | Span | Defined and used at | Frequency sum | Size | Weight |
| --- | --- | --- | --- | --- | --- |
| A | 0 to 18 | 0; 2, 4, 16, 18 | 5 | 18 | 0.28 |
| B | 2 to 16 | 2; 7, 9, 13 (in loop); 16 | 1 + 30 + 1 = 32 | 14 | 2.3 |
| C | 8 to 12 | 8, 12 (both in loop) | 20 | 4 | 5 |

Step through the allocator's choices:

<div class="vx-stepper" markdown="1">
<div class="vx-step" markdown="1">

**Step 1. The queue orders by size: A (18), B (14), C (4).**

A overlaps nothing assigned yet. It takes R1.

| R1 | R2 | Queue |
| --- | --- | --- |
| A | free | B, C |

</div>
<div class="vx-step" markdown="1">

**Step 2. B.**

R1 holds A over all of B's span. R2 is free, so B takes R2.

| R1 | R2 | Queue |
| --- | --- | --- |
| A | B | C |

</div>
<div class="vx-step" markdown="1">

**Step 3. C: nothing free, so evict.**

C overlaps A in R1 and B in R2. Both have a lower weight than C (0.28 and
2.3 against 5), so C may evict either. Evicting A costs less, so A leaves R1
and goes back in the queue, and C takes R1.

| R1 | R2 | Queue |
| --- | --- | --- |
| C | B | A |

</div>
<div class="vx-step" markdown="1">

**Step 4. A again: nothing free, nothing to evict, so split.**

In R1, C (5) is heavier than A; in R2, B (2.3) is heavier too. A cannot
evict either. But A has no uses inside the loop, where both registers are
busy. Greedy cuts A at the loop's edges into A1 (0 to 6), A2 (6 to 14) and
A3 (14 to 18), and queues the pieces.

| R1 | R2 | Queue |
| --- | --- | --- |
| C | B | A1, A2, A3 |

</div>
<div class="vx-step" markdown="1">

**Step 5. The pieces.**

A1 ends before C starts, so it takes R1. A3 starts after C ends, so it takes
R1 too. A2 overlaps C and B, has no uses at all, and cannot be split
further to any benefit, so it is spilled: A is stored once as the loop is
entered and reloaded once after it.

| R1 | R2 | Stack slot |
| --- | --- | --- |
| A1, C, A3 | B | A2 |

</div>
</div>

Figure 2 shows the result. Compare it with spilling A whole: one store after the definition and a reload before each
of the four uses, where the split version needs one store and one reload.
Neither version touches memory inside the loop in this example, because A
is never used there. When a spilled value is used inside a loop, the
difference is a reload on every trip.

<figure class="vx-figure">
<svg viewBox="0 0 760 260" role="img" aria-label="Final assignment of the worked example: A split around the loop, C and the outer parts of A in R1, B in R2, and the middle of A in a stack slot" aria-describedby="e3-greedy-desc">
<title id="e3-greedy-title">The greedy allocator's result for ranges A, B and C</title>
<desc id="e3-greedy-desc">A horizontal time axis from position 0 to 18, with a shaded band from 6 to 14 marking the loop. Three rows. Row R1 holds A from 0 to 6, C from 8 to 12 inside the loop, and A again from 14 to 18. Row R2 holds B from 2 to 16. The stack slot row holds the middle piece of A, dashed, from 6 to 14. A store arrow runs down from R1 to the stack slot at position 6, and a reload arrow runs up at position 14. Dots on A's pieces mark its uses at 2, 4, 16 and 18, all outside the loop.</desc>
<rect class="vx-box" x="278" y="16" width="264" height="190" rx="2"/>
<text class="vx-text-muted" x="410" y="32" text-anchor="middle">loop: positions 6 to 14, ten trips</text>
<text class="vx-text" x="14" y="72">R1</text>
<text class="vx-text" x="14" y="122">R2</text>
<text class="vx-text" x="14" y="176">stack</text>
<rect class="vx-box-strong" x="80" y="52" width="198" height="30" rx="3"/>
<text class="vx-mono" x="170" y="72" text-anchor="middle">A1</text>
<rect class="vx-box-accent" x="344" y="52" width="132" height="30" rx="3"/>
<text class="vx-mono" x="410" y="72" text-anchor="middle">C</text>
<rect class="vx-box-strong" x="542" y="52" width="132" height="30" rx="3"/>
<text class="vx-mono" x="590" y="72" text-anchor="middle">A3</text>
<rect class="vx-box" x="146" y="102" width="462" height="30" rx="3"/>
<text class="vx-mono" x="377" y="122" text-anchor="middle">B</text>
<rect class="vx-box-bad" x="278" y="156" width="264" height="30" rx="3"/>
<text class="vx-mono" x="410" y="176" text-anchor="middle">A2, in its stack slot</text>
<circle class="vx-dot" cx="146" cy="67" r="4"/>
<circle class="vx-dot" cx="212" cy="67" r="4"/>
<circle class="vx-dot" cx="608" cy="67" r="4"/>
<circle class="vx-dot" cx="668" cy="67" r="4"/>
<g class="vx-line">
<line x1="270" y1="82" x2="270" y2="150"/>
<line x1="550" y1="156" x2="550" y2="88"/>
<line x1="80" y1="226" x2="740" y2="226"/>
</g>
<g class="vx-arrowhead">
<polygon points="270,154 266,146 274,146"/>
<polygon points="550,84 546,92 554,92"/>
</g>
<text class="vx-text-muted" x="264" y="120" text-anchor="end">store</text>
<text class="vx-text-muted" x="558" y="146">reload</text>
<text class="vx-text-muted" x="80" y="244" text-anchor="middle">0</text>
<text class="vx-text-muted" x="278" y="244" text-anchor="middle">6</text>
<text class="vx-text-muted" x="542" y="244" text-anchor="middle">14</text>
<text class="vx-text-muted" x="674" y="244" text-anchor="middle">18</text>
</svg>
<figcaption>Figure 2. The worked example after step 5. A is cut at the loop's edges. Its outer pieces share R1 with C, which won R1 by eviction, and its middle piece lives in a stack slot, so the only memory traffic is one store before the loop and one reload after it. Dots mark A's uses. The numbers are invented for the exercise; the next section shows the same behavior in real output.</figcaption>
</figure>

??? check "In step 3, C could have evicted B instead of A. Why is A the better victim, and what would evicting B have cost?"

    B has a higher weight: its uses sit inside the loop, so storing it
    would mean memory traffic on every trip. A's weight is the lowest of
    the three because its few uses are spread over a long span. Evicting B
    would have pushed the expensive range back into the queue, and
    whatever it ended up with (a split piece in the loop, or a spill) would
    have cost more than moving A, whose uses are all outside the loop.

## Greedy on a real function

The example below keeps ten `f32` values live across a loop that calls an
outside function on every trip, and adds them up after the loop.

--8<-- "includes/examples/backend/e3-llvm-allocator-scheduler-mc/keep_across_calls.ll.md"

The procedure call standard says a callee must preserve only the low 64 bits
of `v8` to `v15`, the registers `d8` to `d15`; everything else in the
floating-point register file may be destroyed by `tick`.[^aapcs64] So at
most eight of the ten values can stay in registers across the calls. With
`llc -O2 -mcpu=apple-m1`, the greedy allocator produces this (the prologue
and epilogue are shortened):

```text
	stp	d15, d14, [sp, #16]             ; 16-byte Folded Spill
	...                                     ; d13 to d8, x20, x19, x29, x30
	ldp	s1, s9, [x0]
	ldp	s10, s11, [x0, #8]
	ldp	s12, s13, [x0, #16]
	ldp	s14, s15, [x0, #24]
	ldp	s8, s0, [x0, #32]
	stp	s0, s1, [sp, #8]                ; 8-byte Folded Spill
LBB0_1:                                 ; %loop
	mov	x0, x20
	bl	_tick
	add	w20, w20, #1
	cmp	w20, w19
	b.lt	LBB0_1
	ldp	s1, s0, [sp, #8]                ; 8-byte Folded Reload
	fadd	s0, s0, s9
	...                                     ; eight more fadd
```

Eight values went to `s8` to `s15`, the low halves of the callee-saved
registers, and the prologue saves `d8` to `d15` once for the whole call.
Two values could not stay in registers: one `stp` stores both right before
the loop, and one `ldp` reloads both after it. The loop counter and the
bound went to `w20` and `x19`, callee-saved general registers. That is the
worked example's result on a real function: the two spilled values are cut
around the loop and kept in memory only where nothing uses them.

Add two more values, making twelve, and predict the listing before you run
it. On the owner's machine, `llc` added a second `stp` before the loop and
a second `ldp` after it, and the loop body stayed the same five
instructions: values unused inside the loop add nothing inside the loop.

Now the same file with `-regalloc=fast`, everything else unchanged:

```text
LBB0_1:                                 ; %loop
	ldr	w0, [sp, #20]                   ; 4-byte Folded Reload
	bl	_tick
	ldr	w9, [sp, #4]                    ; 4-byte Folded Reload
	ldr	w8, [sp, #20]                   ; 4-byte Folded Reload
	add	w8, w8, #1
	str	w8, [sp, #20]                   ; 4-byte Folded Spill
	cmp	w8, w9
	b.lt	LBB0_1
```

The fast allocator works one basic block at a time.[^cg] A value that is
live out of a block goes to its stack slot, and the next block reloads it.
So the loop counter and the bound travel through memory on every trip: three
loads and one store per iteration. Before the loop, the same listing stores
all ten `f32` values with ten separate `str` instructions, and the
prologue saves no callee-saved register except the frame pointer and the
link register.

Two details trip people up when they count spills in LLVM's output. The
`Folded Spill` and `Folded Reload` comments mark the prologue's saves of
callee-saved registers as well as the allocator's own spill code, as the
first line of the greedy listing shows, so count the body separately from
the prologue and epilogue. And a spill is a count of instructions, not of
values: here one `stp` spills two values.

## The machine scheduler

LLVM's **MachineScheduler** pass cuts each basic block into **scheduling
regions** and schedules each region on its own. A call ends a region:
nothing moves across it.[^misched-cpp] For each region it builds a
dependence graph with one node per instruction and four kinds of edge: data
(a true dependence), anti, output, and "order" for everything else, such as
memory ordering.[^sdag] Then it runs list scheduling, the algorithm of
[C6](c6-scheduling.md), and can fill the order from the top and from the
bottom of the region at once.[^misched-h]

The interesting part is how it picks among ready instructions. The default
strategy, `GenericScheduler`, compares two candidates by a fixed list of
reasons, and LLVM's header lists them "by decreasing priority": physical
register constraints, then two register-pressure checks, then stalls,
clustering and a third pressure check, then resource use, and only then
latency along the critical path.[^misched-h] **Register pressure** is the number of values live at the
same point. Ranking it ahead of latency means that when the two conflict,
the scheduler prefers an order that keeps pressure within what the
registers can hold, even if long operations overlap less.
[C6](c6-scheduling.md#scheduling-against-register-pressure) explains why the
two goals pull in opposite directions.

The heuristics read their numbers from the **scheduling model** of the
processor named by `-mcpu`: latencies, the units each instruction occupies,
how many instructions issue per cycle. [E2](e2-describing-a-target.md#scheduling-models-one-instruction-several-machines)
shows how a target describes one. A different model can give a different
order for the same code.

After allocation a second pass, the **post-RA machine scheduler**, schedules
again on physical registers, top-down only, with a strategy of its own,
`PostGenericScheduler`.[^misched-h] Allocation is done by then, so a
register reused by two values adds anti and output edges that limit what
it can move.

### Same sum, two models, two orders

--8<-- "includes/examples/backend/e3-llvm-allocator-scheduler-mc/reduction_order.ll.md"

In `chain_sum` each `fadd` reads the one before it, so there is one legal
order, and every model produces it. `tree_sum` has two independent `fadd`s.
With no `-mcpu`, which on `llc` 18.1.8 gave the same code as
`-mcpu=generic`, the pre-RA scheduler moved `c + d` first:

```text
_tree_sum:
	fadd	s2, s2, s3
	fadd	s0, s0, s1
	fadd	s0, s0, s2
```

With `-mcpu=apple-m1` it kept the order written in the IR:

```text
_tree_sum:
	fadd	s0, s0, s1
	fadd	s1, s2, s3
	fadd	s0, s0, s1
```

MIR shows which pass made the change. `llc -stop-after=machine-scheduler`
writes the function as MIR right after that pass,[^mir] and in the
default-model output the `FADDSrr` for `c + d` already comes first. The
post-RA scheduler did not move it. The same dump shows each `FADDSrr` marked
`nofpexcept` and reading `implicit $fpcr`, the floating-point control
register that holds the rounding mode. That implicit operand puts an edge
in the dependence graph between each addition and any instruction that
writes the rounding mode, so no scheduler can move one past the other.

Both orders compute exactly the same bits, and so does any order a scheduler
could choose. Every `fadd` still reads the same two operands, so every
rounding happens on the same values. Reordering independent operations is
not what [record 56](../decisions/numbers.md#d56) forbids. What it forbids
is changing which values meet: turning `(a + b) + (c + d)` into
`((a + b) + c) + d` can change the rounded result, and a scheduler cannot do
that, because it never rewrites an instruction's operands.

The middle end can, with permission. The checker runs this example through
`opt -passes='default<O3>'`, and the expected output shows `chain_sum` and
`tree_sum` unchanged, while `tree_sum_reassoc`, whose `fadd`s carry the
`reassoc` and `nsz` flags, comes back rebuilt as a chain. Those
**fast-math flags** allow rewrites that are otherwise unsafe.[^fmf] A Vortex
compiler must never emit them for `f32` or `f64` arithmetic.[^spec]

### When the order changes the register count

--8<-- "includes/examples/backend/e3-llvm-allocator-scheduler-mc/four_accumulators.ll.md"

Four running sums over sixteen values. With the default model, the
scheduler issues four `ldp` pairs first, eight values in `s0` to `s7`, and
then the first four additions:

```text
	ldp	s0, s1, [x0]
	ldp	s2, s3, [x0, #8]
	ldp	s4, s5, [x0, #16]
	ldp	s6, s7, [x0, #24]
	fadd	s0, s0, s4
	fadd	s1, s1, s5
	fadd	s2, s2, s6
	fadd	s3, s3, s7
```

With `-mcpu=apple-m1` it interleaves loads and additions, so each new pair
of values is consumed before the next arrives, and six registers suffice:

```text
	ldp	s0, s1, [x0]
	ldp	s2, s3, [x0, #8]
	ldp	s4, s5, [x0, #16]
	fadd	s0, s0, s4
	fadd	s1, s1, s5
	ldp	s4, s5, [x0, #24]
	fadd	s2, s2, s4
	fadd	s3, s3, s5
```

Neither order spills; AArch64 has 32 floating-point registers. But the
difference is the one that matters in a larger kernel: issuing loads early
hides their latency and raises the number of live values, and interleaving
does the reverse. The scheduler makes that trade before the allocator runs,
from numbers in a model.

## Asking a model: `llvm-mca`

The scheduler consults a model once and moves on. `llvm-mca` lets you ask
the same kind of model directly: given a sequence of instructions and a
`-mcpu`, it simulates how that processor's model would dispatch, execute and
retire them.[^mca] Figure 3 puts the three additions of `chain_sum` and of
`tree_sum` side by side, from `llvm-mca -mcpu=apple-m1 -iterations=1
-timeline`.

<figure class="vx-figure">
<svg viewBox="0 0 760 250" role="img" aria-label="llvm-mca timelines for three dependent additions and for a tree of three additions on the apple-m1 model" aria-describedby="e3-mca-desc">
<title id="e3-mca-title">Execution cycles of the chain and the tree</title>
<desc id="e3-mca-desc">A cycle axis from 0 to 13. Upper group, chain_sum: the first fadd executes in cycles 1 to 4, the second waits and executes in cycles 5 to 8, the third executes in cycles 9 to 12. Lower group, tree_sum: the first two fadds both execute in cycles 1 to 4, and the third executes in cycles 5 to 8. The chain finishes executing four cycles later than the tree.</desc>
<text class="vx-text" x="10" y="24">chain_sum</text>
<text class="vx-mono" x="10" y="54">fadd s0, s0, s1</text>
<text class="vx-mono" x="10" y="84">fadd s0, s0, s2</text>
<text class="vx-mono" x="10" y="114">fadd s0, s0, s3</text>
<g class="vx-seq" style="--vx-i: 0; --vx-n: 3">
<rect class="vx-box-accent" x="240" y="40" width="160" height="20" rx="2"/>
</g>
<g class="vx-seq" style="--vx-i: 1; --vx-n: 3">
<rect class="vx-box-accent" x="400" y="70" width="160" height="20" rx="2"/>
</g>
<g class="vx-seq" style="--vx-i: 2; --vx-n: 3">
<rect class="vx-box-accent" x="560" y="100" width="160" height="20" rx="2"/>
</g>
<text class="vx-text" x="10" y="144">tree_sum</text>
<text class="vx-mono" x="10" y="174">fadd s4, s0, s1</text>
<text class="vx-mono" x="10" y="194">fadd s5, s2, s3</text>
<text class="vx-mono" x="10" y="214">fadd s0, s4, s5</text>
<rect class="vx-box-strong" x="240" y="161" width="160" height="16" rx="2"/>
<rect class="vx-box-strong" x="240" y="181" width="160" height="16" rx="2"/>
<rect class="vx-box-strong" x="400" y="201" width="160" height="16" rx="2"/>
<line class="vx-line" x1="200" y1="232" x2="740" y2="232"/>
<text class="vx-text-muted" x="200" y="248" text-anchor="middle">0</text>
<text class="vx-text-muted" x="240" y="248" text-anchor="middle">1</text>
<text class="vx-text-muted" x="400" y="248" text-anchor="middle">5</text>
<text class="vx-text-muted" x="560" y="248" text-anchor="middle">9</text>
<text class="vx-text-muted" x="720" y="248" text-anchor="middle">13</text>
</svg>
<figcaption>Figure 3. Cycles in which each <code>fadd</code> executes in <code>llvm-mca</code>'s <code>apple-m1</code> model (LLVM 18.1.8), which gives <code>fadd</code> a latency of 4 cycles. The chain's additions wait for each other; the tree's first two run together. The whole block took 15 simulated cycles for the chain and 11 for the tree, counting dispatch and retirement. This is a model's prediction, not a measurement of any chip.</figcaption>
</figure>

The figure makes the cost of `chain_sum`'s shape visible, and it also shows
what no back-end pass can do about it. The scheduler can only choose among
orders the dependence graph allows, and the chain allows one. Getting the
tree's overlap needs a different graph, which here means a different sum.

Keep the tool's limits in mind. Its documentation says the quality of its
analysis depends on the quality of LLVM's scheduling models, and that it
does not model the instruction fetch and decode stages or branch
prediction.[^mca] [E2](e2-describing-a-target.md#seeing-the-model-with-llvm-mca)
shows where its numbers come from, and
[P5](../optimize/p5-microarchitecture.md) compares models with measurements.

??? check "The tree is four cycles shorter in the model. Why may a Vortex compiler still not turn `chain_sum` into `tree_sum`, and what could make the kernel's loop faster without breaking that rule?"

    Because the two groupings can round differently, and record 56 forbids
    reassociation. Legal speedups keep every addition's operands the same:
    overlapping independent work that already exists, such as different
    output elements of a matrix product, whose sums do not depend on each
    other. That changes which loop is innermost, a middle-end
    transformation ([P7](../optimize/p7-loop-transformations.md)), not a
    scheduling decision.

## The MC layer

After the last machine pass, the assembly printer lowers each
`MachineInstr` into an **MCInst**: a target opcode and a list of operands,
each an immediate, a register, or a symbolic expression such as a label.[^cg]
An `MCInst` knows nothing about functions, basic blocks or virtual
registers. It is the form that LLVM's instruction printer, instruction
encoder, assembly parser and disassembler all share.[^cg]

From there every instruction and every directive goes to an **MCStreamer**,
an interface with one method per assembler directive and one for
instructions.[^cg] It has two main implementations. `MCAsmStreamer` prints
text, and `MCObjectStreamer` implements a full assembler that writes an
object file.[^cg] The standalone assembler, `llvm-mc`, parses a `.s` file
and drives the same interface, so text printed by the compiler and parsed
back produces the same streamer calls as the compiler's direct
path.[^mc-blog] Inside the object streamer, a target's **MCCodeEmitter**
turns each `MCInst` into bytes and a list of **fixups**.[^cg]

### One instruction, followed down

The next example has a conditional branch and three calls whose targets the
assembler knows to different degrees.

--8<-- "includes/examples/backend/e3-llvm-allocator-scheduler-mc/calls_and_fixups.ll.md"

The last MIR, printed with `-stop-after=branch-relaxation`, holds `caller`'s
first instruction as `CBZW renamable $w0, %bb.2`. `llc -show-mc-encoding
-asm-show-inst` prints what the MC layer made of it and of the first call:

```text
	cbz	w0, LBB2_2                      ; encoding: [0bAAA00000,A,A,0x34]
                                        ;   fixup A - offset: 0, value: LBB2_2, kind: fixup_aarch64_pcrel_branch19
                                        ; <MCInst #1892 CBZW
                                        ;  <MCOperand Reg:204>
                                        ;  <MCOperand Expr:(LBB2_2)>>
	...
	mov	x19, x0                         ; encoding: [0xf3,0x03,0x00,0xaa]
                                        ; <MCInst #4936 ORRXrs
	...
	bl	_local_helper                   ; encoding: [A,A,A,0b100101AA]
                                        ;   fixup A - offset: 0, value: _local_helper, kind: fixup_aarch64_pcrel_call26
```

Read the encoding bytes in memory order: AArch64 stores the low byte of each
32-bit word first, so `0x34` is the top byte. The `A` bits are the ones the
encoder could not fill: the branch offset, which depends on where `LBB2_2`
ends up. The fixup records where those bits are, which symbol they wait
for, and a **fixup kind** that says how to compute and insert them:
`pcrel_branch19` is a 19-bit offset from the branch itself, counted in
4-byte words.

Two more things show in the `MCInst` lines. The opcode numbers and register
numbers are indexes into tables that TableGen generates
([E2](e2-describing-a-target.md)); they are specific to this build of LLVM.
And `mov x19, x0` is an `ORRXrs`, an OR with the zero register: the printer
chose the `mov` alias for display, but the instruction is an OR.

Figure 4 traces that path from the `MachineInstr` to the two possible fates
of a fixup.

<figure class="vx-figure">
<svg viewBox="0 0 760 320" role="img" aria-label="The path of a cbz instruction through the MC layer, from MachineInstr to MCInst, then to text or to bytes with a fixup, which is either resolved or becomes a relocation" aria-describedby="e3-mc-desc">
<title id="e3-mc-title">From MachineInstr to text or bytes</title>
<desc id="e3-mc-desc">A MachineInstr box, CBZW renamable w0 to bb.2, feeds an arrow labelled asm printer to an MCInst box, CBZW with a register operand and an expression operand LBB2_2. From the MCInst, one arrow goes right to the instruction printer, which gives the text cbz w0, LBB2_2 through MCAsmStreamer. Another arrow goes down to the MCCodeEmitter, which gives the word 0x34000000 with a fixup of kind pcrel_branch19. From there two arrows lead down. Left: the target is in the same section and cannot move, so the assembler patches the word to 0x34000120. Right: the target may be elsewhere or may move, so the fixup becomes a relocation in the object file for the linker.</desc>
<rect class="vx-box" x="10" y="20" width="220" height="64" rx="4"/>
<text class="vx-text" x="120" y="44" text-anchor="middle">MachineInstr</text>
<text class="vx-mono" x="120" y="68" text-anchor="middle">CBZW $w0, %bb.2</text>
<rect class="vx-box-accent" x="270" y="20" width="220" height="64" rx="4"/>
<text class="vx-text-accent" x="380" y="44" text-anchor="middle">MCInst</text>
<text class="vx-mono" x="380" y="68" text-anchor="middle">CBZW Reg, Expr(LBB2_2)</text>
<rect class="vx-box" x="530" y="20" width="220" height="64" rx="4"/>
<text class="vx-text" x="640" y="44" text-anchor="middle">Instruction printer</text>
<text class="vx-mono" x="640" y="68" text-anchor="middle">cbz w0, LBB2_2</text>
<rect class="vx-box" x="270" y="130" width="220" height="64" rx="4"/>
<text class="vx-text" x="380" y="154" text-anchor="middle">MCCodeEmitter</text>
<text class="vx-mono" x="380" y="178" text-anchor="middle">0x34000000 + fixup</text>
<rect class="vx-box-strong" x="60" y="240" width="300" height="64" rx="4"/>
<text class="vx-text" x="210" y="264" text-anchor="middle">Target fixed in this section</text>
<text class="vx-mono" x="210" y="288" text-anchor="middle">patched: 0x34000120</text>
<rect class="vx-box-bad" x="400" y="240" width="300" height="64" rx="4"/>
<text class="vx-text" x="550" y="264" text-anchor="middle">Distance not fixed yet</text>
<text class="vx-mono" x="550" y="288" text-anchor="middle">relocation for the linker</text>
<g class="vx-line">
<line x1="230" y1="52" x2="266" y2="52"/>
<line x1="490" y1="52" x2="526" y2="52"/>
<line x1="380" y1="84" x2="380" y2="126"/>
<line x1="330" y1="194" x2="230" y2="236"/>
<line x1="430" y1="194" x2="530" y2="236"/>
</g>
<g class="vx-arrowhead">
<polygon points="266,52 258,48 258,56"/>
<polygon points="526,52 518,48 518,56"/>
<polygon points="380,126 376,118 384,118"/>
<polygon points="230,236 234,227 240,233"/>
<polygon points="530,236 520,233 526,227"/>
</g>
<text class="vx-text-muted" x="248" y="108" text-anchor="middle">asm printer</text>
<text class="vx-text-muted" x="508" y="108" text-anchor="middle">MCAsmStreamer: text</text>
<text class="vx-text-muted" x="392" y="112">MCObjectStreamer: bytes</text>
<text class="vx-text-muted" x="380" y="218" text-anchor="middle">kind: pcrel_branch19</text>
</svg>
<figcaption>Figure 4. The <code>cbz</code> from <code>caller</code> on its way through the MC layer. One <code>MCInst</code> feeds both the text path and the byte path. On the byte path the offset bits start empty, and the fixup that describes them ends in one of two places: patched by the assembler inside the object streamer, or written out as a relocation.</figcaption>
</figure>

### Resolving a fixup by hand

Here is the start of `caller` in the Mach-O object, from
`llc -filetype=obj` and `llvm-objdump -dr`:

```text
0000000000000020 <_caller>:
      20: 34000120     	cbz	w0, 0x44 <_caller+0x24>
      24: a9be4ff4     	stp	x20, x19, [sp, #-0x20]!
      28: a9017bfd     	stp	x29, x30, [sp, #0x10]
      2c: aa0003f3     	mov	x19, x0
      30: 94000000     	bl	0x30 <_caller+0x10>
		0000000000000030:  ARM64_RELOC_BRANCH26	_local_helper
      34: aa1303e0     	mov	x0, x19
      38: 94000000     	bl	0x38 <_caller+0x18>
		0000000000000038:  ARM64_RELOC_BRANCH26	_shared_helper
      3c: a9417bfd     	ldp	x29, x30, [sp, #0x10]
      40: a8c24ff4     	ldp	x20, x19, [sp], #0x20
      44: d65f03c0     	ret
```

The `cbz` sits at `0x20` and its target, `LBB2_2`, turned out to be the
`ret` at `0x44`. The distance is `0x44 - 0x20 = 0x24` bytes, which is 9
words. The encoding put the `A` bits above the low five bits (which hold
the register, `w0`, number 0), so the field is bits 5 to 23:
$9 \times 2^5 = 288 =$ `0x120`, and `0x34000000 | 0x120 = 0x34000120`, the
word in the listing. The assembler resolved this fixup itself, and no
relocation remains.

Now finish one yourself. In the ELF object for the same file, `caller`'s
`cbz` is also at `0x20`, but the ELF prologue is shorter, and the `ret` it
jumps to is at `0x3c`. The distance is `0x1c` bytes. What word does the
assembler write?

??? check "Work out the ELF `cbz` word, then say whether it needs a relocation."

    `0x1c` bytes is 7 words, and $7 \times 2^5 = 224 =$ `0xe0`, so the word
    is `0x340000e0`, which is what `llvm-objdump` shows. No relocation:
    the target is a label in the same function, in the same section, and
    nothing can move one relative to the other after assembly.

## Which fixups become relocations

The two calls in the Mach-O listing are different. `bl` has the word
`0x94000000` with an all-zero offset, and an `ARM64_RELOC_BRANCH26`
relocation follows it. Both callees are defined in the same file, yet the
assembler left both for the linker. The ELF object for the same IR
(`-mtriple=aarch64-linux-gnu`) decides differently:

```text
      2c: 97fffff5     	bl	0x0 <local_helper>
      30: aa1303e0     	mov	x0, x19
      34: 94000000     	bl	0x34 <caller+0x14>
		0000000000000034:  R_AARCH64_CALL26	shared_helper
```

| Call target | Mach-O object | ELF object |
| --- | --- | --- |
| Label in the same function (`cbz`) | resolved | resolved |
| `local_helper`, internal linkage, defined in the file | relocation | resolved (`0x97fffff5`) |
| `shared_helper`, global, defined in the file | relocation | relocation |
| `external_sink`, only declared | relocation | relocation |

Each row follows from what the linker is allowed to do later. `llc` ended
this Mach-O file with `.subsections_via_symbols`, and the Mach-O header flag
that directive sets tells the linker it may divide sections into pieces at
symbols for dead-code stripping.[^loader] Each function is then a separate
piece the linker may drop, so even the distance to an internal function is
not known until link time.

[B4](b4-linking-and-loading.md#which-definition-wins-and-which-code-survives)
covers the directive. On ELF, a global symbol with default visibility may be
**preempted**, replaced by another definition of the same name when the
program is linked or loaded, so the assembler cannot bind a call to the copy
it happens to see; [B4](b4-linking-and-loading.md#position-independent-code-and-executables)
explains why. An internal function cannot be preempted and stays at a fixed
distance within the section, so the ELF assembler patched the call itself. Its word
checks out by hand: the distance from `0x2c` back to `0x0` is -11 words,
which in a 26-bit field is `0x3fffff5`, and `0x94000000 | 0x3fffff5` is
`0x97fffff5`.

The rule for your own back end follows directly: a fixup may be resolved at
assembly time only when both ends are in the same piece of the object that
nothing later can split, move or replace.

## Relaxation: when the distance changes the instruction

On x86-64 many branches have a short form with an 8-bit offset and a long
form with a 32-bit one. The assembler cannot pick until it knows the
distance, and the distance depends on the sizes of the instructions in
between, some of them branches themselves. Choosing the forms is
**relaxation**, one of the jobs the MC assembler performs.[^mc-blog]
[B3](b3-object-files.md#relaxation-when-a-jumps-size-depends-on-its-distance)
works through it, including why it must be repeated until nothing changes.

AArch64 instructions are all four bytes, so there is no shorter or longer
form to choose, only a range.

The conditional forms have short ones. AAELF64
limits a `TBZ` offset to ±32 KiB, the 19-bit field of a conditional branch
to ±1 MiB, and `B` and `BL` to ±128 MiB.[^aaelf64] `CBZ` has the same 19-bit
field, which is why its fixup above has the same kind as a `b.eq`. LLVM deals with a
conditional branch that cannot reach before the MC layer, in the **branch
relaxation** pass near the end of Figure 1, which counts the conditional
branches it rewrites.[^brelax] Its limits come from the target, and
AArch64 exposes them as hidden debugging options: `-aarch64-cbz-offset-bits`
defaults to 19 bits and `-aarch64-tbz-offset-bits` to 14.[^aii] Shrinking one
forces the rewrite on a small function:

```text
$ llc -O2 -mcpu=apple-m1 -aarch64-cbz-offset-bits=3 calls_and_fixups.ll -o -
_caller:
	cbnz	w0, LBB2_1
	b	LBB2_2
LBB2_1:                                 ; %work
	...
LBB2_2:                                 ; %done
	ret
```

With three bits of offset, the `cbz` cannot reach nine words ahead, so the
pass inverted the condition and jumped over an unconditional `b`, whose
range is ±128 MiB. Without the option, the same function keeps its single
`cbz`.

A `BL` that cannot reach its target is the linker's problem. The linker
inserts a **veneer**, a short stub that reaches farther, AAPCS64 allows a
veneer to change `x16`, `x17` and the flags, and requires code to assume
that one may be inserted at any branch the linker can redirect this
way.[^aapcs64]
[B4](b4-linking-and-loading.md#when-a-call-cannot-reach-thunks-and-code-models)
covers veneers and thunks.

??? check "Why does LLVM relax AArch64 branches in a machine pass instead of in the MC assembler, as it does for x86?"

    Because on AArch64 fixing an out-of-range branch is not a choice of
    encoding size; it needs a new instruction, a new label and an inverted
    condition, which is a change to the control-flow graph. The machine
    pass can make that change while it still has basic blocks and knows
    each instruction's size. On x86 the fix is a longer encoding of the
    same instruction, which the assembler can choose on its own.

## For Vortex

!!! vortex "Exercise"

    **Build a report that compares your back end's allocation with LLVM's,
    and make it a regression test.** This is test tooling beside your
    compiler, not a change to it. Pick three programs:

    1. the [stage 10](../compiler/guide/stage-10-matrix-multiplication.md#the-program-the-milestone-asks-for)
       matmul kernel;
    2. a program you write in which at least ten `f32` values stay live
       across a loop that calls `print` on every trip, the shape of this
       chapter's `keep_across_calls`;
    3. a program with array accesses guarded by bounds checks, so the
       runtime-error call from [stage 9](../compiler/guide/stage-9-runtime-safety.md#stopping-with-a-clear-runtime-error)
       sits inside a loop.

    Compile each twice: with your native back end, and as LLVM IR through
    `llc -O2 -mcpu=apple-m1` (the IR from your stage 6 LLVM path if you have
    one; otherwise write it by hand once and keep it in the test folder,
    with plain `fadd` and `fmul` and no fast-math flags). For every
    function, the report counts from each assembly listing: stack loads and
    stores inside loop bodies; other stack loads and stores, not counting
    the prologue and epilogue; and callee-saved registers the prologue
    saves. For the kernel's innermost loop body alone, it also runs
    `llvm-mca -mcpu=apple-m1` on both versions and records the modeled
    cycles per iteration.

    **Not yet.** Do not change your allocator to copy greedy's eviction or
    splitting; C5 decides what your allocator does, and this report only
    tells you where it stands. Do not count spills by searching for
    LLVM's `Folded Spill` comments, which also mark callee-saved saves, and
    which your own listings will not have: find prologue and epilogue by
    position and count memory instructions against `sp` or `x29`. Do not
    time anything; the `llvm-mca` numbers are a model's.

    **Done when** three things hold. The report runs in your test suite on
    macOS arm64 and prints the table below. Each program's output is
    identical, bit for bit, through both paths. And the test compares your
    back end's counts with a baseline file checked into the repository and
    fails when any count rises: prove it by lowering the register cap from
    [C5](c5-spilling.md)'s debug option until the kernel's inner loop
    must spill, and watching the test fail and name the function.

    | Program | Function | Stack accesses in loops (yours / `llc`) | Other stack accesses | Callee-saved saved | `llvm-mca` cycles per iteration |
    | --- | --- | --- | --- | --- | --- |
    | | | | | | |

    Record the machine, the `llc` version and the date with the table.

## Key ideas

!!! recap "You can now answer"

    - **In what order does the greedy allocator try to place a live range?** Assign a free register, evict lighter ranges, split the range, and spill it only when splitting will not help.
    - **What is a spill weight in LLVM 18?** The expected executions of the range's definitions and uses, weighted by block frequency, divided by its length plus a constant: busy short ranges weigh most.
    - **Why did the greedy loop in `keep_across_calls` touch no memory while the fast allocator's did?** Greedy kept eight values in callee-saved registers and split the other two around the loop; fast works per block and sends values live across blocks through their stack slots.
    - **What may the machine scheduler change, and what decides its choice?** The order of instructions within a region, never their operands; its heuristics put register pressure ahead of latency and read their numbers from the `-mcpu` scheduling model.
    - **Why can't any scheduler choice change a Vortex floating-point result?** Every operation still reads the same operands, so every rounding happens on the same values; only regrouping, which needs `reassoc`, could change a result.
    - **What is an `MCInst`, and where does a fixup come from?** A target opcode with immediate, register and expression operands; the code emitter produces a fixup for each expression whose value is not known yet.
    - **When does an AArch64 fixup become a relocation?** When the distance to the target is not fixed at assembly time: a symbol in another file, a Mach-O function the linker may strip, or a preemptible ELF global.

## Where this comes back

!!! next "You will use this again in"

    - [E4. Testing back ends](e4-testing-backends.md): *MIR tests that run one pass*, *encoding tests against `llvm-mc`*
    - [D3. Reading real back ends](d3-real-backends.md): *eviction and splitting in regalloc2*, *a different answer to the same assign, evict, split, spill question*
    - [P5. The microarchitecture shelf](../optimize/p5-microarchitecture.md): *scheduling models*, *where a model and a measurement disagree*
    - [G9. GPU compilers inside LLVM](../gpu/g9-gpu-compilers-in-llvm.md): *a target that skips register allocation*, *two register files to allocate*

## Sources and further reading

Every listing on this page was produced with LLVM 18.1.8 on an Apple M4 Pro
running macOS 27, on 2026-09-24; the commands are given beside each one.
Olesen's post is the best short account of why the greedy allocator works
the way it does, and the LLVM headers cited below are the most exact
description of LLVM 18's behavior.

[^olesen]: Jakob Stoklund Olesen, "Greedy Register Allocation in LLVM 3.0", LLVM Project Blog, 18 September 2011. <https://blog.llvm.org/2011/09/greedy-register-allocation-in-llvm-30.html>
[^cg]: LLVM Project, "The LLVM Target-Independent Code Generator": "Built in register allocators", "The MC Layer" and "Code Emission". <https://llvm.org/docs/CodeGenerator.html>
[^braun]: Matthias Braun, "Welcome to the Back End: The LLVM Machine Representation", LLVM Developers' Meeting, 2017. <https://llvm.org/devmtg/2017-10/slides/Braun-Welcome%20to%20the%20Back%20End.pdf>
[^matrix]: LLVM Project, `llvm/include/llvm/CodeGen/LiveRegMatrix.h`, LLVM 18.1.8, file comment. <https://github.com/llvm/llvm-project/blob/llvmorg-18.1.8/llvm/include/llvm/CodeGen/LiveRegMatrix.h>
[^weights]: LLVM Project, `llvm/include/llvm/CodeGen/CalcSpillWeights.h`, LLVM 18.1.8, `normalizeSpillWeight`. <https://github.com/llvm/llvm-project/blob/llvmorg-18.1.8/llvm/include/llvm/CodeGen/CalcSpillWeights.h>
[^stages]: LLVM Project, `llvm/lib/CodeGen/RegAllocEvictionAdvisor.h`, LLVM 18.1.8, `enum LiveRangeStage`. <https://github.com/llvm/llvm-project/blob/llvmorg-18.1.8/llvm/lib/CodeGen/RegAllocEvictionAdvisor.h>
[^spillplace]: LLVM Project, `llvm/lib/CodeGen/SpillPlacement.h`, LLVM 18.1.8, file comment. <https://github.com/llvm/llvm-project/blob/llvmorg-18.1.8/llvm/lib/CodeGen/SpillPlacement.h>
[^aapcs64]: Arm, "Procedure Call Standard for the Arm 64-bit Architecture (AAPCS64)", 2025Q4: "SIMD and Floating-Point registers" (v8 to v15 callee-saved, low 64 bits only) and the veneer rules for IP0 and IP1. <https://github.com/ARM-software/abi-aa/blob/main/aapcs64/aapcs64.rst>
[^misched-cpp]: LLVM Project, `llvm/lib/CodeGen/MachineScheduler.cpp`, LLVM 18.1.8, `isSchedBoundary` and `scheduleRegions`. <https://github.com/llvm/llvm-project/blob/llvmorg-18.1.8/llvm/lib/CodeGen/MachineScheduler.cpp>
[^sdag]: LLVM Project, `llvm/include/llvm/CodeGen/ScheduleDAG.h`, LLVM 18.1.8, `SDep::Kind`. <https://github.com/llvm/llvm-project/blob/llvmorg-18.1.8/llvm/include/llvm/CodeGen/ScheduleDAG.h>
[^misched-h]: LLVM Project, `llvm/include/llvm/CodeGen/MachineScheduler.h`, LLVM 18.1.8: file comment, `GenericSchedulerBase::CandReason`, `GenericScheduler` and `PostGenericScheduler`. <https://github.com/llvm/llvm-project/blob/llvmorg-18.1.8/llvm/include/llvm/CodeGen/MachineScheduler.h>
[^mir]: LLVM Project, "Machine IR (MIR) Format Reference Manual", on `-stop-after`, `-stop-before` and `-run-pass`. <https://llvm.org/docs/MIRLangRef.html>
[^fmf]: LLVM Project, "LLVM Language Reference Manual", "Fast-Math Flags". <https://llvm.org/docs/LangRef.html#fast-math-flags>
[^spec]: [Vortex specification, 4.4 "Floating-point values"](../specification/types-and-values.md#44-floating-point-values), and [decision record 56](../decisions/numbers.md#d56).
[^mca]: LLVM Project, "llvm-mca - LLVM Machine Code Analyzer", command guide: description and "Instruction Dispatch" and timeline sections. <https://llvm.org/docs/CommandGuide/llvm-mca.html>
[^mc-blog]: Chris Lattner, "Intro to the LLVM MC Project", LLVM Project Blog, 9 April 2010. <https://blog.llvm.org/2010/04/intro-to-llvm-mc-project.html>
[^loader]: Apple, XNU `EXTERNAL_HEADERS/mach-o/loader.h`, `MH_SUBSECTIONS_VIA_SYMBOLS`. <https://github.com/apple-oss-distributions/xnu/blob/main/EXTERNAL_HEADERS/mach-o/loader.h>
[^aaelf64]: Arm, "ELF for the Arm 64-bit Architecture (AAELF64)", 2025Q4, relocations `R_AARCH64_TSTBR14`, `R_AARCH64_CONDBR19`, `R_AARCH64_JUMP26` and `R_AARCH64_CALL26`. <https://github.com/ARM-software/abi-aa/blob/main/aaelf64/aaelf64.rst>
[^brelax]: LLVM Project, `llvm/lib/CodeGen/BranchRelaxation.cpp`, LLVM 18.1.8. <https://github.com/llvm/llvm-project/blob/llvmorg-18.1.8/llvm/lib/CodeGen/BranchRelaxation.cpp>
[^aii]: LLVM Project, `llvm/lib/Target/AArch64/AArch64InstrInfo.cpp`, LLVM 18.1.8, options `aarch64-tbz-offset-bits`, `aarch64-cbz-offset-bits` and `aarch64-bcc-offset-bits`. <https://github.com/llvm/llvm-project/blob/llvmorg-18.1.8/llvm/lib/Target/AArch64/AArch64InstrInfo.cpp>

# A1. The machine model

<p class="page-intro">What a processor is, from a compiler's point of view: a set of registers, a flat memory, an instruction set that names what can happen to them, and an encoding that turns each instruction into bits. Everything a back end does, from picking an instruction to writing an object file, is a decision made inside this model.</p>

<p class="vx-meta" markdown="1">Level: Foundations · Reading time: about 18 minutes · Builds on: [Build v0.1, stage 6](../compiler/guide/stage-6-first-machine-code.md), [Build v0.1, stage 7](../compiler/guide/stage-7-functions-and-control-flow.md), [Build v0.1, stage 8](../compiler/guide/stage-8-data-in-memory.md)</p>

???+ remember "Before you start, remember"

    ??? question "What is an address?"

        The number of a byte in memory. If you know where a value starts,
        its address is the number of its first byte.

        Introduced in [8. Data in memory](../compiler/guide/stage-8-data-in-memory.md#memory-addresses-and-layout).

    ??? question "How does Vortex store a two-dimensional array such as `[f32; 2, 3]`?"

        In row-major order: one whole row after another, so neighbours in a
        row are neighbours in memory, four bytes apart for an `f32`.

        Introduced in [8. Data in memory](../compiler/guide/stage-8-data-in-memory.md#arrays-in-memory).

    ??? question "What is the difference between the front end and the back end of a compiler?"

        The front end reads and understands the source language (stages 1 to
        5); the back end produces code for one particular machine.

        Introduced in [6. The first machine code](../compiler/guide/stage-6-first-machine-code.md#words-for-this-stage).

    ??? question "What does the linker do that the compiler cannot?"

        It fills in the holes an object file leaves open, such as a call to
        `print`, by joining the object file with the code that defines that
        name.

        Introduced in [6. The first machine code](../compiler/guide/stage-6-first-machine-code.md#from-object-file-to-executable).

    ??? question "What does a `&mut` reference let a function do?"

        Reach the caller's own storage and write through it, without making
        a copy. Vortex's matrix multiply takes its output this way.

        Introduced in [8. Data in memory](../compiler/guide/stage-8-data-in-memory.md#references-and-passing-without-copying).

!!! goals "In this chapter"

    - Explain why an instruction set architecture is a contract, separate from any one chip's microarchitecture.
    - Describe a register file and contrast a load/store machine with a register-memory machine.
    - Compute the address an addressing mode produces, for a handful of common forms.
    - Hand-encode one instruction on AArch64 and one on x86-64, straight from the field layout in their manuals.
    - Recognize which of today's choices reappear as decisions in later chapters: instruction selection, calling conventions, object files.

## One line, two different machines

Take a single line of AArch64 assembly:

```text
add x0, x1, x2
```

It says: read the 64-bit registers `x1` and `x2`, add them, and write the
result into `x0`. On the processor in an Apple M4 Pro this line becomes the
32-bit word `0x8b020020`, stored as the four bytes `20 00 02 8b`. On a
RISC-V chip, on a different Arm chip from ten years ago, on any processor
that implements the AArch64 architecture, that same line becomes exactly
those same four bytes, and running them does exactly the same thing to `x0`,
`x1` and `x2`.[^m2]

That last sentence is the whole idea of an **instruction set architecture**
(ISA): a contract between software and hardware that fixes which registers
exist, how memory is addressed, which instructions exist, and what bits
encode each one. A compiler's back end targets the ISA, not any particular
chip. Whether one chip runs `add x0, x1, x2` in one clock cycle and another
takes three, whether one has four adders working at once and another has
one, is a question about **microarchitecture**, the way a specific
implementation honors the ISA's contract. Two chips can implement AArch64
correctly and still run the same program at different speeds; the
manual does not promise a speed, only a meaning. (Measuring that speed is
the subject of [P4](../optimize/p4-counters-and-tools.md) and
[P5](../optimize/p5-microarchitecture.md); this chapter stays with the
contract.)

This chapter builds the vocabulary the rest of the back end books use for
that contract: what a register file looks like, how a machine reaches
memory, how an address is computed, and how an instruction becomes bits.
[A2](a2-aarch64-assembly.md) then teaches you to read and write the AArch64
assembly text this chapter's bytes come from, and
[B1](b1-simplest-backend.md) starts turning Vortex's own IR into that text.

??? check "Two different Arm chips both implement AArch64 correctly. Why might `add x0, x1, x2` run faster on one than on the other?"

    The ISA fixes what the instruction means, not how fast any particular
    circuit computes it. Pipeline depth, the number of execution units, and
    how far ahead the chip can look for independent work are all
    microarchitecture, and microarchitecture is free to differ between two
    chips that both honor the same contract.

## Registers: names for the fastest storage a processor has

A **register** is a small, fixed-size storage slot built into the processor
itself, fast enough that most instructions read and write it in the same
cycle they execute in. A **register file** is the whole named set of them
that one ISA exposes to software. The two ISAs in this chapter give
different answers to "how many, and what for":

- AArch64 has 31 general-purpose 64-bit registers, `x0` through `x30`, plus
  the stack pointer `sp`, and a separate bank of 32 registers for
  floating-point and vector work, `v0` through `v31`.[^m4] Writing to the
  low 32 bits of `x0` under the name `w0` is how integer arithmetic on
  narrower values works; the register is the same storage either way.
- x86-64 has 16 general-purpose 64-bit registers, `rax` through `r15`
  (eight of them, `r8` to `r15`, are new names added when the ISA grew from
  32 bits to 64), and a separate bank of vector registers, `xmm0` through
  `xmm15` in the baseline instruction set.[^m9]

Neither count is arbitrary in the way it might look. A register name has to
fit inside an instruction's encoding, which this chapter gets to shortly,
and every extra register that must be saved across a function call adds to
the calling convention that [A4](a4-calling-conventions.md) writes down. For
now, the fact to keep is narrower: a **register file** is a small, named,
fixed set, and an instruction almost always names its operands from that set
rather than from memory directly.

"Almost always" is doing real work in that sentence, and it splits ISAs into
two families.

## Load/store machines and register-memory machines

Suppose the back end needs to add 1 to `a[i]`, one element of an array of
`i32`. On AArch64, that takes three instructions: load the element into a
register, add 1 to the register, store the register back.[^m2] Every
arithmetic instruction on AArch64 reads and writes only registers; the only
instructions that touch memory at all are the ones whose entire job is to
move a value between memory and a register, `ldr` and `str`. An ISA built
this way is a **load/store architecture**.

x86-64 allows more directly. One `add` instruction there can name a memory
location as one of its own operands, computing the address, reading the old
value, adding, and writing the result back to that same address, all as a
single instruction.[^m9] An ISA that lets ordinary arithmetic instructions
read (and sometimes write) memory operands, not only registers, is a
**register-memory architecture**.

This is not a claim about which machine does less work at the transistor
level: a chip built around a register-memory ISA still has to compute the
address, fetch the value, and add it, and modern x86-64 chips break such an
instruction into smaller internal steps to do exactly that. The difference
that matters to a compiler is at the instruction-count level, the level a
back end works at when it counts how many instructions a piece of source
code becomes. Fewer instructions is not automatically faster, but it is one
fewer decision an instruction selector has to make, and
[C1](c1-instruction-selection.md) returns to exactly this trade-off when it
chooses which instructions to emit for a given piece of IR.

??? check "Why can `a[i] += 1` become a single x86-64 instruction but never a single AArch64 instruction?"

    Because AArch64 is a load/store architecture: every arithmetic
    instruction operates on registers only, and reaching memory always
    takes a separate `ldr` or `str`. x86-64 is a register-memory
    architecture, so one of `add`'s own operands is allowed to name a
    memory location directly.

## Memory as one long row of bytes, and how an address is computed

Whichever family an ISA belongs to, it agrees on what memory looks like from
software: one long row of numbered bytes, the same view [stage
8](../compiler/guide/stage-8-data-in-memory.md#memory-addresses-and-layout)
already gave the front end. A load or store instruction still has to say
*which* byte, and it almost never does that with a bare number baked into
the instruction. Instead it names an **addressing mode**: the rule an
instruction uses to compute a memory address from registers and constants,
such as "a base register plus an index scaled by the element size".

Take Vortex's matrix multiply. Reading `a[i][k]`, one element of a
`[f32; rows, cols]` array stored row-major, means computing
`address(a) + (i * cols + k) * 4`, a base address plus an offset built from
two multiplications and an addition.[^stage8] A back end that had to emit
separate multiply and add instructions for every array access in the matmul
kernel would spend most of its instruction count on arithmetic that never
touches the actual numbers being multiplied. Addressing modes exist to fold
part of that computation into the load or store instruction itself, for
free, at the cost of only being able to fold the shapes the ISA's designers
decided were common enough to build in.

AArch64's `ldr` accepts several such shapes.[^m2] Four of them, all loading
one `f32` from an array `a` whose address is in `x0`:

<figure class="vx-figure">
<svg viewBox="0 0 760 400" role="img" aria-labelledby="a1-addr-title a1-addr-desc">
<title id="a1-addr-title">Four addressing modes computing an address into the same array</title>
<desc id="a1-addr-desc">A six-element array of f32 is shown four times, once per addressing mode. Immediate offset reaches element 4 directly. Scaled register offset reaches element i, wherever the index register points. Pre-index moves the base register to element 1 before loading from it. Post-index loads from element 0, then moves the base register to element 1 afterward.</desc>
<g class="vx-seq" style="--vx-i: 0; --vx-n: 4">
<text class="vx-text" x="20" y="34">Immediate offset</text>
<text class="vx-mono" x="330" y="34" text-anchor="middle">ldr s0, [x0, #16]</text>
<text class="vx-text-muted" x="700" y="34" text-anchor="end">x0 + 16</text>
<rect x="200" y="46" width="36" height="30" rx="3" class="vx-box"/>
<rect x="240" y="46" width="36" height="30" rx="3" class="vx-box"/>
<rect x="280" y="46" width="36" height="30" rx="3" class="vx-box"/>
<rect x="320" y="46" width="36" height="30" rx="3" class="vx-cell-on"/>
<rect x="360" y="46" width="36" height="30" rx="3" class="vx-box"/>
<rect x="400" y="46" width="36" height="30" rx="3" class="vx-box"/>
<text class="vx-text-muted" x="218" y="94" text-anchor="middle">a[0]</text>
<text class="vx-text-muted" x="338" y="94" text-anchor="middle">a[4]</text>
<text class="vx-text-muted" x="200" y="40" font-size="11">x0</text>
<line class="vx-line" x1="218" y1="36" x2="218" y2="46"/>
</g>
<g class="vx-seq" style="--vx-i: 1; --vx-n: 4">
<text class="vx-text" x="20" y="124">Scaled register offset</text>
<text class="vx-mono" x="330" y="124" text-anchor="middle">ldr s0, [x0, x1, lsl #2]</text>
<text class="vx-text-muted" x="700" y="124" text-anchor="end">x0 + (x1 &#215; 4)</text>
<rect x="200" y="136" width="36" height="30" rx="3" class="vx-box"/>
<rect x="240" y="136" width="36" height="30" rx="3" class="vx-box"/>
<rect x="280" y="136" width="36" height="30" rx="3" class="vx-cell-on"/>
<rect x="320" y="136" width="36" height="30" rx="3" class="vx-box"/>
<rect x="360" y="136" width="36" height="30" rx="3" class="vx-box"/>
<rect x="400" y="136" width="36" height="30" rx="3" class="vx-box"/>
<text class="vx-text-muted" x="218" y="184" text-anchor="middle">a[0]</text>
<text class="vx-text-muted" x="298" y="184" text-anchor="middle">a[i]</text>
<text class="vx-text-muted" x="200" y="130" font-size="11">x0</text>
<line class="vx-line" x1="218" y1="126" x2="218" y2="136"/>
</g>
<g class="vx-seq" style="--vx-i: 2; --vx-n: 4">
<text class="vx-text" x="20" y="214">Pre-index</text>
<text class="vx-mono" x="330" y="214" text-anchor="middle">ldr s0, [x0, #4]!</text>
<text class="vx-text-muted" x="700" y="214" text-anchor="end">x0 &#8592; x0 + 4, then load</text>
<rect x="200" y="226" width="36" height="30" rx="3" class="vx-box"/>
<rect x="240" y="226" width="36" height="30" rx="3" class="vx-cell-on"/>
<rect x="280" y="226" width="36" height="30" rx="3" class="vx-box"/>
<rect x="320" y="226" width="36" height="30" rx="3" class="vx-box"/>
<rect x="360" y="226" width="36" height="30" rx="3" class="vx-box"/>
<rect x="400" y="226" width="36" height="30" rx="3" class="vx-box"/>
<text class="vx-text-muted" x="258" y="274" text-anchor="middle">a[1]</text>
<text class="vx-text-muted" x="240" y="220" font-size="11">x0 (after)</text>
<line class="vx-line" x1="258" y1="216" x2="258" y2="226"/>
</g>
<g class="vx-seq" style="--vx-i: 3; --vx-n: 4">
<text class="vx-text" x="20" y="304">Post-index</text>
<text class="vx-mono" x="330" y="304" text-anchor="middle">ldr s0, [x0], #4</text>
<text class="vx-text-muted" x="700" y="304" text-anchor="end">load, then x0 &#8592; x0 + 4</text>
<rect x="200" y="316" width="36" height="30" rx="3" class="vx-cell-on"/>
<rect x="240" y="316" width="36" height="30" rx="3" class="vx-box"/>
<rect x="280" y="316" width="36" height="30" rx="3" class="vx-box"/>
<rect x="320" y="316" width="36" height="30" rx="3" class="vx-box"/>
<rect x="360" y="316" width="36" height="30" rx="3" class="vx-box"/>
<rect x="400" y="316" width="36" height="30" rx="3" class="vx-box"/>
<text class="vx-text-muted" x="218" y="364" text-anchor="middle">a[0]</text>
<text class="vx-text-muted" x="200" y="310" font-size="11">x0 (before)</text>
<line class="vx-line" x1="218" y1="306" x2="218" y2="316"/>
<line class="vx-line" x1="258" y1="316" x2="258" y2="346"/>
<text class="vx-text-muted" x="258" y="364" text-anchor="middle">x0 after</text>
</g>
</svg>
<figcaption>Figure 1. Four addressing modes, all loading one <code>f32</code> element of the same array through the base register <code>x0</code>. Immediate offset and scaled register offset leave <code>x0</code> unchanged; pre-index and post-index each move it by one element, before or after the load. The instructions and the array are the ones checked in <code>aarch64-addressing-modes.s</code>, below.</figcaption>
</figure>

The scaled-register form is the one that matters most for kernels: the shift
amount `#2` multiplies the index by 4, the size of an `f32`, so the address
computation for `a[i]` costs nothing beyond the load itself. That shift is
not a free choice of any amount. A 4-byte load only accepts a shift of `#0`
or `#2`; an 8-byte load only `#0` or `#3`.[^m2] The scale always has to be a
power of two, because it is encoded as a left shift, not as a general
multiply, and Vortex's element sizes (`f32` is 4 bytes, `f64` is 8) are
themselves always powers of two, so this limit never costs anything in
practice. [A2](a2-aarch64-assembly.md#loads-stores-and-addresses) gives
the complete table of forms, their limits, and the unscaled variants a
compiler falls back to when an offset does not fit.

x86-64 builds a comparable address, base plus scaled index plus a
displacement, out of a **SIB byte** (scale, index, base) that this chapter's
next section decodes directly.[^m26] The shapes look different on the page,
but the job is the same one: compute an address without spending a separate
instruction on the arithmetic.

??? check "The example encodes `add x0, x1, x2`. What bytes would `add x3, x4, x5` encode to, on the same instruction form?"

    `83 00 05 8b`. Only the three register fields change: `Rd` becomes 3
    (`0b00011`), `Rn` becomes 4, `Rm` becomes 5. Every other bit, including
    the `01011` pattern that names this instruction class, stays the same
    as in `add x0, x1, x2`.

## Encoding: an instruction is a fixed pattern of bits

Everything so far has stayed at the level of words: register names,
addressing-mode shapes. An assembler's actual job is to turn those words
into the bytes a processor fetches and decodes, one instruction at a time.
That mapping, from an instruction's mnemonic and operands to the bits that
represent it, is its **encoding**.

AArch64 makes this almost mechanical: every instruction, no exceptions, is
one 32-bit word.[^m2] A processor's decoder looks at a handful of fixed
bits, usually near the top of the word, to tell which instruction class it
is looking at, then reads the rest of the bits as that class's fields. The
`add` (shifted register) form used throughout this chapter lays its 32 bits
out like this:

<figure class="vx-figure">
<svg viewBox="0 0 760 280" role="img" aria-labelledby="a1-enc-title a1-enc-desc">
<title id="a1-enc-title">Two ways to encode a similar instruction</title>
<desc id="a1-enc-desc">The AArch64 word for add x0, x1, x2 is one fixed 32-bit pattern, its eight fields shown left to right: sf op S, a fixed 01011 pattern, shift kind, a fixed 0, Rm, imm6, Rn and Rd. Below it, the five bytes of the x86-64 instruction addss with a memory operand: a prefix byte, two opcode bytes, a ModR/M byte and a SIB byte.</desc>
<g class="vx-seq" style="--vx-i: 0; --vx-n: 8">
<rect x="30" y="30" width="66" height="34" rx="3" class="vx-box-strong"/>
<text class="vx-mono" x="63" y="51" text-anchor="middle" font-size="11">sf op S</text>
</g>
<g class="vx-seq" style="--vx-i: 1; --vx-n: 8">
<rect x="96" y="30" width="109" height="34" rx="3" class="vx-box-strong"/>
<text class="vx-mono" x="150" y="51" text-anchor="middle" font-size="11">01011</text>
</g>
<g class="vx-seq" style="--vx-i: 2; --vx-n: 8">
<rect x="205" y="30" width="44" height="34" rx="3" class="vx-box"/>
<text class="vx-mono" x="227" y="51" text-anchor="middle" font-size="10">shift</text>
</g>
<g class="vx-seq" style="--vx-i: 3; --vx-n: 8">
<rect x="249" y="30" width="22" height="34" rx="3" class="vx-box"/>
<text class="vx-mono" x="260" y="51" text-anchor="middle" font-size="10">0</text>
</g>
<g class="vx-seq" style="--vx-i: 4; --vx-n: 8">
<rect x="271" y="30" width="109" height="34" rx="3" class="vx-box-accent"/>
<text class="vx-mono" x="325" y="51" text-anchor="middle" font-size="11">Rm</text>
</g>
<g class="vx-seq" style="--vx-i: 5; --vx-n: 8">
<rect x="380" y="30" width="131" height="34" rx="3" class="vx-box"/>
<text class="vx-mono" x="445" y="51" text-anchor="middle" font-size="11">imm6</text>
</g>
<g class="vx-seq" style="--vx-i: 6; --vx-n: 8">
<rect x="511" y="30" width="109" height="34" rx="3" class="vx-box-accent"/>
<text class="vx-mono" x="565" y="51" text-anchor="middle" font-size="11">Rn</text>
</g>
<g class="vx-seq" style="--vx-i: 7; --vx-n: 8">
<rect x="620" y="30" width="109" height="34" rx="3" class="vx-box-accent"/>
<text class="vx-mono" x="674" y="51" text-anchor="middle" font-size="11">Rd</text>
</g>
<text class="vx-text-muted" x="30" y="82" font-size="11">bit 31</text>
<text class="vx-text-muted" x="700" y="82" text-anchor="end" font-size="11">bit 0</text>
<text class="vx-text" x="30" y="104">add x0, x1, x2  &#8594;  word 0x8b020020  &#8594;  bytes 20 00 02 8b</text>
<line class="vx-line" x1="30" y1="120" x2="730" y2="120"/>
<g class="vx-seq" style="--vx-i: 0; --vx-n: 5">
<rect x="30" y="150" width="140" height="34" rx="3" class="vx-box-strong"/>
<text class="vx-mono" x="100" y="171" text-anchor="middle" font-size="11">F3</text>
</g>
<g class="vx-seq" style="--vx-i: 1; --vx-n: 5">
<rect x="170" y="150" width="140" height="34" rx="3" class="vx-box-strong"/>
<text class="vx-mono" x="240" y="171" text-anchor="middle" font-size="11">0F</text>
</g>
<g class="vx-seq" style="--vx-i: 2; --vx-n: 5">
<rect x="310" y="150" width="140" height="34" rx="3" class="vx-box-strong"/>
<text class="vx-mono" x="380" y="171" text-anchor="middle" font-size="11">58</text>
</g>
<g class="vx-seq" style="--vx-i: 3; --vx-n: 5">
<rect x="450" y="150" width="140" height="34" rx="3" class="vx-box-accent"/>
<text class="vx-mono" x="520" y="171" text-anchor="middle" font-size="11">04</text>
</g>
<g class="vx-seq" style="--vx-i: 4; --vx-n: 5">
<rect x="590" y="150" width="140" height="34" rx="3" class="vx-box-accent"/>
<text class="vx-mono" x="660" y="171" text-anchor="middle" font-size="11">8F</text>
</g>
<text class="vx-text-muted" x="30" y="202" font-size="11">prefix</text>
<text class="vx-text-muted" x="170" y="202" font-size="11">opcode (1/2)</text>
<text class="vx-text-muted" x="310" y="202" font-size="11">opcode (2/2)</text>
<text class="vx-text-muted" x="450" y="202" font-size="11">ModR/M</text>
<text class="vx-text-muted" x="590" y="202" font-size="11">SIB</text>
<text class="vx-text" x="30" y="230">addss (%rdi,%rcx,4), %xmm0  &#8594;  bytes f3 0f 58 04 8f</text>
<text class="vx-text-muted" x="30" y="256" font-size="12">Fixed for this instruction class</text>
<rect x="230" y="246" width="16" height="14" rx="2" class="vx-box-strong"/>
<text class="vx-text-muted" x="410" y="256" font-size="12">Depends on the operands</text>
<rect x="608" y="246" width="16" height="14" rx="2" class="vx-box-accent"/>
</svg>
<figcaption>Figure 2. The same job, add a scaled index into a register, encoded two different ways. AArch64 (top) packs eight fields into one fixed-width 32-bit word: a decoder reads the same bit positions for every instruction of this class. x86-64 (bottom) builds the instruction from a variable number of bytes, from one (a bare register-to-register move) up to fifteen; this one needs a prefix, a two-byte opcode, and a ModR/M and SIB byte to name a memory operand. Both encodings are checked against llvm-mc and objdump output in <code>aarch64-add-encoding.cpp</code> and <code>x86-64-addss-encoding.cpp</code>, below.</figcaption>
</figure>

Reading the AArch64 fields left to right: bit 31 (`sf`) picks the 64-bit
register file over the 32-bit one; bits 30 and 29 (`op`, `S`) say "add,
without setting the condition flags"; the five bits `01011` are the fixed
pattern that tells the decoder "this is an add or subtract with a shifted
register operand", the same five bits for every instruction of this class
regardless of which registers it names; two bits pick the shift kind, here
`00` for none; one bit is always `0` in this form; and the last three
fields, five bits each, are `Rm`, `Rn` and `Rd`, the three registers, with
`imm6` (a shift amount, `0` here) sitting between `Rm` and `Rn`.[^m2] Five
bits is enough to name any of the 31 general-purpose registers (`0` through
`30`); that is not a coincidence, it is why AArch64 has 31 of them rather
than, say, 64.

--8<-- "includes/examples/backend/a1-machine-model/aarch64-add-encoding.cpp.md"

x86-64 encodes the same idea, add a scaled memory operand into a register,
completely differently. `addss (%rdi,%rcx,4), %xmm0` needs five bytes: a
mandatory prefix `F3` that marks this as a scalar single-precision
operation, a two-byte opcode `0F 58` that names `ADDSS` itself, a **ModR/M**
byte that would normally name a destination register and a second operand
but here says "the second operand is a memory location, computed by a SIB
byte that follows", and that **SIB** (scale-index-base) byte, which packs
the scale (`4`, stored as the two-bit code for "multiply by four"), the
index register (`rcx`) and the base register (`rdi`) into one
byte.[^m9][^m26] Other x86-64 instructions are as short as one byte (a bare
register-to-register move) or as long as fifteen, with optional legacy
prefixes and, for newer instructions, a REX, VEX or EVEX prefix in place of
the plain one shown here.[^m26] There is no single decoder table the way
there is for AArch64; a decoder has to read prefix bytes to know how many
more prefix bytes might follow, then the opcode, and only then does it know
whether a ModR/M byte comes next, and only then whether a SIB byte follows
that.

--8<-- "includes/examples/backend/a1-machine-model/x86-64-addss-encoding.cpp.md"

The AArch64 program is not a special case wired to one instruction. Give it
different register numbers and it produces a different, still-correct word:
`add x5, x6, x7` becomes `c5 00 07 8b`, checked the same way as the first
pair. The x86-64 program does the same for a different base, index and
destination register. Both were checked against an independent oracle
before being trusted: `llvm-mc -show-encoding` for the AArch64 bytes, and
`objdump` on a real object file assembled by `clang --target=x86_64-linux-gnu`
for the x86-64 bytes, since the LLVM tools installed for this book target
AArch64 only.[^t6]

--8<-- "includes/examples/backend/a1-machine-model/aarch64-addressing-modes.s.md"

## Why the machine model shapes a back end's decisions

None of this chapter's vocabulary is trivia. Every later back-end chapter
makes a decision that the machine model already constrains:

- **Instruction selection** ([C1](c1-instruction-selection.md)) chooses,
  for each piece of IR, which instruction and which addressing mode covers
  it; a selector that does not know AArch64's scaled-register form will
  emit a separate multiply for every `a[i]` in Vortex's matmul kernel
  instead of folding it into the load.
- **Calling conventions** ([A4](a4-calling-conventions.md)) are written
  against a specific register file: which registers a callee must save,
  which the caller must, and which carry arguments and results are all
  questions about the same 31 (or 16) named registers this chapter
  introduced, not about memory in general.
- **Object files and encoding** ([B3](b3-object-files.md)) are where a
  compiler that writes machine code directly, rather than handing text to
  an external assembler, does exactly the byte-level work this chapter's
  examples do by hand, for every instruction the back end can emit, not
  only one.

The two ISAs also make different bets, and it is worth stating them
plainly rather than leaving them implicit. AArch64 spends more bits per
instruction (every one is 4 bytes, even a plain register move) in exchange
for a decoder that can be a small, mostly table-driven circuit and for
addressing modes that fold common array-indexing arithmetic in for
free.[^m2] x86-64 spends decades of encoding complexity in exchange for
density: short, common instructions stay short, and every extension since
the original 16-bit 8086 has been layered on as new prefixes and opcode
bytes rather than a clean break.[^m26] Neither bet is free, and neither is
"correct"; they are different answers to a design problem a back-end writer
eventually asks for any new target: how much of the address computation and
operand encoding should the ISA do, and how much should it leave to
software.

??? check "AArch64's `add` (shifted register) form uses five bits each for Rd, Rn and Rm. Why five, and not, say, four or six?"

    Five bits can name 32 distinct values, and AArch64 has 31
    general-purpose registers, `x0` through `x30`. Four bits could only
    reach 16 registers; six would waste a bit naming values the register
    file does not have. The field width and the register count are chosen
    together.

## For Vortex

!!! vortex "Exercise"

    **Build.** In your own compiler, outside this repository, pick one
    AArch64 instruction form you will need for the back end you are about
    to start: either the `add` (shifted register) form this chapter
    encoded, or the `ldr`/`str` immediate-offset form you will need the
    moment you generate code for an array. Write an encoder: a function
    that takes the operand fields (which registers, which immediate) and
    returns the 32-bit word.

    **Do not build yet.** A general table covering every instruction form
    your back end will eventually need; that is
    [B3](b3-object-files.md)'s job, once you know which forms your
    instruction selector emits. A disassembler. Any wiring of the
    encoder into your compiler's pipeline; [B1](b1-simplest-backend.md)
    decides how code gets emitted, and it may start by writing
    assembly text rather than encoded bytes at all.

    **The test that proves it works.** Generate every legal combination of
    the form's operand fields (for `add`, every register triple with each
    of `Rd`, `Rn`, `Rm` from 0 to 30) and compare your encoder's output,
    byte for byte, against `llvm-mc -show-encoding` (or an assembled object
    file read back with `llvm-objdump`) as an independent oracle. Every
    combination must match exactly; a mismatch on even one combination
    means a field is in the wrong position or the wrong width. Once that
    passes, write down, in your own notes rather than in code, why the
    addressing mode you will use for `a[i]` on a Vortex array supports
    scale factors of 1, 2, 4 and 8 and no others, and confirm that every
    element size your fixed-shape `[f32; ...]` and `[f64; ...]` arrays can
    have is one of those four.

## Key ideas

!!! recap

    - **What is an ISA?** A contract between software and hardware: which
      registers exist, how memory is addressed, which instructions exist,
      and what bits encode each one.
    - **What is microarchitecture?** How one specific chip implements an
      ISA's contract; it can change a program's speed but never its
      meaning.
    - **What is the difference between a load/store machine and a
      register-memory machine?** A load/store machine (AArch64) does
      arithmetic only on registers and reaches memory only through
      explicit load and store instructions; a register-memory machine
      (x86-64) lets some instructions read (or write) one operand straight
      from memory.
    - **What is an addressing mode?** The rule an instruction uses to
      compute a memory address from registers and constants, such as a
      base register plus an index scaled by the element size.
    - **Why does AArch64's scaled-register addressing mode only accept
      power-of-two scales?** Because the scale is encoded as a left shift,
      not a general multiply, and every element size it needs to support
      (1, 2, 4 or 8 bytes) is itself a power of two.
    - **Why is every AArch64 instruction 4 bytes while x86-64 instructions
      vary from 1 to 15?** AArch64 trades code size for a simple,
      table-driven decoder; x86-64 keeps every instruction it has ever
      added by layering new prefixes and opcode bytes onto the original
      8086 encoding.
    - **Why does an addressing mode matter for a matmul kernel's
      performance work later?** Folding an array index's multiply into the
      load instruction, instead of emitting a separate multiply, is one
      fewer instruction per element access, in a loop that touches
      millions of elements.

## Where this comes back

!!! next "You will use this again in"

    - [A2. Reading and writing AArch64 assembly](a2-aarch64-assembly.md): *addressing modes*, *register names*, *load/store architecture*
    - [A4. Calling conventions and ABIs](a4-calling-conventions.md): *register file*, *callee-saved registers*
    - [B1. The simplest back end that works](b1-simplest-backend.md): *instruction*, *target*, *assembly text*
    - [B3. Object files and assemblers](b3-object-files.md): *instruction encoding*, *fixed-width vs variable-width*
    - [C1. Instruction selection](c1-instruction-selection.md): *addressing modes*, *load/store vs register-memory*

## Sources and further reading

This chapter's two hand-encoded instructions, and every addressing-mode
example, are checked against Arm's own architecture manual and against
locally run tools (`llvm-mc`, `clang --target=x86_64-linux-gnu` with
`objdump`), not transcribed from a secondary source. Arm's own gentler
introduction to the same material is worth reading before the reference
manual itself.[^m3] For a textbook-length treatment of the same
machine-level view from the C side, CS:APP's chapters on machine-level
programming are a standard next stop, independent of any particular
compiler.[^csapp]

[^m2]: Arm, "A64 Instruction Set Architecture for A-profile architecture" (DDI 0602), "Index by Encoding", and the individual pages for ADD (shifted register) and LDR (immediate/register). <https://developer.arm.com/documentation/ddi0602/latest>
[^m3]: Arm, "Learn the architecture: A64 instruction set architecture" (102374). <https://developer.arm.com/documentation/102374/latest>
[^m4]: Arm, "Procedure Call Standard for the Arm 64-bit Architecture (AAPCS64)", release 2025Q4, section 5, "Machine Registers". <https://github.com/ARM-software/abi-aa/blob/main/aapcs64/aapcs64.rst>
[^m9]: Intel, "Intel 64 and IA-32 Architectures Software Developer's Manual", volume 2 (Instruction Format and Instruction Set Reference). <https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html>
[^m26]: OSDev Wiki, "X86-64 Instruction Encoding". <https://wiki.osdev.org/X86-64_Instruction_Encoding>
[^t6]: LLVM Project, `llvm-mc` command guide. <https://llvm.org/docs/CommandGuide/llvm-mc.html>
[^stage8]: [Build v0.1, stage 8, "Arrays in memory"](../compiler/guide/stage-8-data-in-memory.md#arrays-in-memory); the row-major offset formula follows [decision 43](../decisions/arrays.md#d43).
[^csapp]: Randal E. Bryant and David R. O'Hallaron, *Computer Systems: A Programmer's Perspective*, 3rd edition. <https://csapp.cs.cmu.edu/>

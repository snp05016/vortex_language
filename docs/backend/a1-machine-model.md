# A1. The machine model

<p class="page-intro">What a processor is from a compiler's point of view: a small set of registers, memory as a row of numbered bytes, instructions that change them, and a fixed rule for turning each instruction into bits. Every later back-end decision, from choosing an instruction to writing an object file, is made inside this model, and Vortex's array accesses are where it first shows.</p>

<p class="vx-meta" markdown="1">Level: Foundations · Reading time: about 35 minutes · Builds on: [6. The first machine code](../compiler/guide/stage-6-first-machine-code.md), [7. Functions and control flow](../compiler/guide/stage-7-functions-and-control-flow.md), [8. Data in memory](../compiler/guide/stage-8-data-in-memory.md)</p>

???+ remember "Before you start, remember"

    ??? question "What is a register, and how does it differ from memory?"

        A small, fast storage slot inside the processor. Instructions
        mostly work on values held in registers; memory is much larger and
        sits outside.

        Introduced in [6. The first machine code](../compiler/guide/stage-6-first-machine-code.md#words-for-this-stage).

    ??? question "What does an assembler do?"

        It turns assembly language, the text form of machine instructions,
        into machine code: the bytes the processor runs.

        Introduced in [6. The first machine code](../compiler/guide/stage-6-first-machine-code.md#words-for-this-stage).

    ??? question "How does a running program see memory?"

        As one long row of bytes, each with a number, its address. A value
        occupies a run of bytes, and its address is the number of the first.

        Introduced in [8. Data in memory](../compiler/guide/stage-8-data-in-memory.md#memory-addresses-and-layout).

    ??? question "At what byte offset does element `[1, 2]` of a `[f32; 2, 3]` start?"

        20. Vortex stores arrays row-major, so the element is
        `(1 × 3 + 2) × 4` bytes from the start: one whole row of three
        four-byte elements, then two more.

        Introduced in [8. Data in memory](../compiler/guide/stage-8-data-in-memory.md#arrays-in-memory). Decision: [record 43](../decisions/arrays.md#d43).

    ??? question "What does a function receive when it is called with `&mut c`?"

        Access to the caller's own storage, not a copy. In machine terms,
        the function receives an address.

        Introduced in [8. Data in memory](../compiler/guide/stage-8-data-in-memory.md#references-and-passing-without-copying). Decision: [record 40](../decisions/references.md#d40).

!!! goals "In this chapter"

    - Explain why an instruction set architecture is a contract about meaning, separate from the microarchitecture that decides speed.
    - Describe the state an instruction can change, and compare the register files of AArch64 and x86-64.
    - Tell a load/store machine from a register-memory machine by reading the code a compiler writes for one array update on each.
    - Compute the address an addressing mode produces, and say which parts of a Vortex array index an addressing mode can absorb.
    - Encode and decode one instruction by hand on each machine, and check the result against an assembler.

A compiler's front end ends with a program it understands. The back end has to
say the same thing to a machine, and a machine understands little: a few
dozen named storage slots, a numbered row of bytes, and a list of operations,
each written as a pattern of bits. This chapter describes that machine the way
a back end sees it, using the two instruction sets the back-end book targets:
**AArch64**, the 64-bit Arm architecture of the Apple M4 Pro, and
**x86-64**, the 64-bit extension of Intel's x86 family.

Nothing here is about writing a back end yet. It is the vocabulary every later
chapter uses, and each idea is shown on something a compiler emits.

## One line, two different machines

Take one line of AArch64 assembly:

```text
add x0, x1, x2
```

It says: read the 64-bit registers `x1` and `x2`, add them, and write the
sum into `x0`. The assembler turns this line into the 32-bit number
`0x8b020020`, stored in memory as the four bytes `20 00 02 8b`. Assembling
the line on the M4 Pro with `llvm-mc` 18 gives those bytes, and an object
file written by the system assembler holds them in that order.[^llvm-mc]
Every processor that implements AArch64, whoever designed it, runs those four
bytes and leaves the same sum in `x0`.

That promise is an **instruction set architecture** (ISA): a contract between
software and hardware that fixes which registers exist, how memory is
addressed, which instructions exist, what each one computes, and which bits
encode it. Arm publishes the AArch64 contract as a reference manual, and every
instruction has a page with its encoding and a precise description of its
effect.[^a64-add-reg] A compiler's back end targets that contract, not a
particular chip.

What the contract leaves open is speed. Whether one chip finishes the `add`
sooner than another, how many additions it can start at once, how far ahead it
looks for independent work: all of that is **microarchitecture**, the design
of one particular implementation of the ISA. Two chips can implement AArch64
correctly and run the same program at different speeds, because the manual
promises a meaning, not a timing. Measuring speed is the subject of
[P4](../optimize/p4-counters-and-tools.md) and
[P5](../optimize/p5-microarchitecture.md); this chapter stays with the
contract.

One naming trap belongs here. The "64" in A64, the name of the instruction
set, refers to the 64-bit execution state, AArch64, not to the size of an
instruction: Arm describes A64 as a fixed-length 32-bit instruction
set.[^arm-isets] Registers are 64 bits wide; instructions are 32.

??? check "Two different Arm chips both implement AArch64 correctly. Can a program compute different results on them? Can it take different amounts of time?"

    Different times, yes: speed is microarchitecture, and the ISA does not
    promise any. Different results, no, for any behavior the manual
    defines: the meaning of each instruction is exactly what the contract
    fixes.

## The state an instruction changes

A back end reasons about the machine as a collection of state and a rule for
changing it. The **architectural state** is everything the ISA says a program
can see: the registers, the address of the next instruction, a few status
bits, and memory. An instruction is a small, exact rule: read some of that
state, compute, write some of it back.

On AArch64 the pieces are these. Thirty-one general-purpose registers,
`x0` to `x30`, each 64 bits wide, which can also be named `w0` to `w30` to
work on their low 32 bits; a stack pointer, `sp`, usable by a restricted set
of instructions; thirty-two registers `v0` to `v31` for floating-point and
vector work; and one status register, NZCV, whose four **condition flags**
record facts about the result of a comparison.[^aapcs64] The **program
counter** (PC) holds the address of the current instruction. It is not a
general-purpose register and cannot take part in arithmetic.[^arm-other]
Memory is the numbered row of bytes that
[stage 8](../compiler/guide/stage-8-data-in-memory.md#memory-addresses-and-layout)
described.

The processor runs a program by repeating one cycle, and the manual describes
each instruction as if it ran alone, one after another. **Fetch** the four
bytes at the address in PC. **Decode** them: work out which instruction they
are and which registers and constants they name. **Execute**: read the
operands, compute, write the results. Then move PC to the next instruction,
four bytes further on, unless the instruction was a branch that chose another
address. Figure 1 draws the state and the cycle.

<figure class="vx-figure">
<svg viewBox="0 0 760 380" role="img" aria-label="The architectural state of an AArch64 program and the cycle that changes it" aria-describedby="a1-state-desc">
<title id="a1-state-title">The architectural state of an AArch64 program and the cycle that changes it</title>
<desc id="a1-state-desc">On the left, the state a program can see: the program counter PC, the general-purpose registers x0 to x30, the stack pointer sp, the NZCV flags, the registers v0 to v31, and memory drawn as a row of bytes with addresses. On the right, a four-step cycle: fetch four bytes at PC, decode them, execute by reading and writing registers and memory, and advance PC by 4 or to a branch target. A dashed line across the bottom separates this contract from the microarchitecture below it: pipelines, caches and several instructions in flight, which may change speed but never results.</desc>
<text class="vx-text" x="24" y="26">what the ISA promises: the architectural state</text>
<rect class="vx-box-strong" x="24" y="40" width="120" height="34" rx="4"/>
<text class="vx-mono" x="84" y="62" text-anchor="middle">PC</text>
<rect class="vx-box" x="24" y="84" width="120" height="34" rx="4"/>
<text class="vx-mono" x="84" y="106" text-anchor="middle">x0 ... x30</text>
<rect class="vx-box" x="154" y="84" width="70" height="34" rx="4"/>
<text class="vx-mono" x="189" y="106" text-anchor="middle">sp</text>
<rect class="vx-box" x="234" y="84" width="90" height="34" rx="4"/>
<text class="vx-mono" x="279" y="106" text-anchor="middle">NZCV</text>
<rect class="vx-box" x="24" y="128" width="120" height="34" rx="4"/>
<text class="vx-mono" x="84" y="150" text-anchor="middle">v0 ... v31</text>
<text class="vx-text-muted" x="154" y="150">registers inside the processor</text>
<rect class="vx-box" x="24" y="190" width="40" height="30"/>
<rect class="vx-box" x="64" y="190" width="40" height="30"/>
<rect class="vx-box" x="104" y="190" width="40" height="30"/>
<rect class="vx-box" x="144" y="190" width="40" height="30"/>
<rect class="vx-box" x="184" y="190" width="40" height="30"/>
<rect class="vx-box" x="224" y="190" width="40" height="30"/>
<rect class="vx-box" x="264" y="190" width="40" height="30"/>
<text class="vx-mono" x="44" y="210" text-anchor="middle">20</text>
<text class="vx-mono" x="84" y="210" text-anchor="middle">00</text>
<text class="vx-mono" x="124" y="210" text-anchor="middle">02</text>
<text class="vx-mono" x="164" y="210" text-anchor="middle">8b</text>
<text class="vx-text-muted" x="24" y="238">memory: numbered bytes, here one instruction</text>
<text class="vx-text-muted" x="24" y="256">PC holds the address of the first of them</text>
<g class="vx-seq" style="--vx-i: 0; --vx-n: 4">
<rect class="vx-box-accent" x="370" y="40" width="360" height="40" rx="4"/>
<text class="vx-text" x="384" y="65">1. fetch 4 bytes at the address in PC</text>
</g>
<g class="vx-seq" style="--vx-i: 1; --vx-n: 4">
<rect class="vx-box-accent" x="370" y="92" width="360" height="40" rx="4"/>
<text class="vx-text" x="384" y="117">2. decode: find the instruction and operands</text>
</g>
<g class="vx-seq" style="--vx-i: 2; --vx-n: 4">
<rect class="vx-box-accent" x="370" y="144" width="360" height="40" rx="4"/>
<text class="vx-text" x="384" y="169">3. execute: compute and write the results</text>
</g>
<g class="vx-seq" style="--vx-i: 3; --vx-n: 4">
<rect class="vx-box-accent" x="370" y="196" width="360" height="40" rx="4"/>
<text class="vx-text" x="384" y="221">4. PC = PC + 4, or a branch target</text>
</g>
<path class="vx-flow" d="M735 216 C 755 216, 755 60, 735 60"/>
<polygon class="vx-arrowhead" points="741,54 731,60 741,66"/>
<line class="vx-line" x1="144" y1="57" x2="366" y2="57"/>
<polygon class="vx-arrowhead" points="360,51 370,57 360,63"/>
<line class="vx-line" x1="24" y1="290" x2="736" y2="290" stroke-dasharray="6 5"/>
<text class="vx-text-accent" x="24" y="282">the contract: what every AArch64 chip must produce</text>
<text class="vx-text-muted" x="24" y="318">microarchitecture: pipelines, caches, several instructions in flight at once.</text>
<text class="vx-text-muted" x="24" y="340">It may change how long a program takes, never what it computes.</text>
</svg>
<figcaption>Figure 1. The model a back end programs against. The left side is the state a program can see and change; the right side is the cycle that changes it, one instruction at a time. Real chips overlap many instructions below the dashed line, but the results must match the one-at-a-time description above it.</figcaption>
</figure>

The one-at-a-time description is what makes compilers possible. A modern core
has many instructions in flight at once ([P5](../optimize/p5-microarchitecture.md)
shows how), but it must produce the results the sequential description gives.
So a back end can reason about one instruction at a time, as a function from
state to state, and leave the overlap to the hardware. When the back end
later cares about speed, in instruction scheduling ([C6](c6-scheduling.md)),
it does so without changing that meaning.

x86-64 has the same kinds of state under different names: an instruction
pointer, `rip`, in place of PC; a flags register; sixteen general-purpose
registers; and sixteen `xmm` registers for floating point and
vectors.[^osdev] The next section compares the registers.

## Registers: names for the fastest storage a processor has

A **register file** is the set of registers an ISA lets instructions name.
Registers are the fastest storage in the machine and the only operands most
instructions accept, so their number and their roles shape every back end.

| | AArch64 | x86-64 |
| --- | --- | --- |
| General-purpose registers | 31: `x0` to `x30`[^aapcs64] | 16: `rax`, `rcx`, `rdx`, `rbx`, `rsp`, `rbp`, `rsi`, `rdi`, `r8` to `r15`[^osdev] |
| Their 32-bit views | `w0` to `w30` | `eax`, `ecx`, ..., `r8d` to `r15d` |
| Stack pointer | `sp`, separate from the 31 | `rsp`, one of the 16 |
| Floating point and vectors | 32: `v0` to `v31`[^aapcs64] | 16: `xmm0` to `xmm15`[^osdev] |
| Bits that name a general register in an instruction | 5 | 3, plus a fourth from a prefix byte for `r8` to `r15`[^osdev] |

The last row connects the register file to the encoding, which the section on
encoding takes apart. An AArch64 instruction names a general register with a
five-bit field, so it can name 32 things. The register fields of x86-64 are
three bits wide, enough for eight registers; the other eight, `r8` to `r15`,
are reached through a fourth bit carried in a prefix byte called
**REX**.[^osdev] So the register count is not a free design parameter:
it is limited by how many bits each instruction can spend naming one.

Why a compiler cares: every value a program computes must live somewhere.
Values that fit in registers are cheap to reach; values that do not must be
stored to memory and loaded back. How many registers there are, and which of
them a function may overwrite, decides how often that happens. The calling
convention ([A4](a4-calling-conventions.md)) assigns the roles, and register
allocation ([C3](c3-linear-scan.md)) decides which value lives in which
register. On the matmul kernel, with three nested loops and several addresses
live at once, 31 registers and 16 registers are different problems.

The two widths of each register, `x0` and `w0` for instance, are one piece of
storage seen at two sizes. [A2](a2-aarch64-assembly.md#registers-by-name)
covers the rule for what happens to the upper half when a program writes the
lower one, and the zero register, `xzr`, which this chapter meets again in the
encoding.

## Load/store machines and register-memory machines

Here is an update of one array element, the kind of line a Vortex loop body
is full of, written in C++ so that clang can compile it for both machines:

--8<-- "includes/examples/backend/a1-machine-model/array-access.cpp.md"

Compiled with Apple clang 21 at `-O2`, on an Apple M4 Pro with macOS 27 in
September 2026, `bump` becomes this for AArch64 (`--target=arm64-apple-macos`,
with the directives removed):

```gas
__Z4bumpPil:
	ldr	w8, [x0, x1, lsl #2]
	add	w8, w8, #1
	str	w8, [x0, x1, lsl #2]
	ret
```

and this for x86-64 (`--target=x86_64-linux-gnu`, the two functions compiled
on their own because the Mac has no Linux C++ headers):

```gas
_Z4bumpPil:
	incl	(%rdi,%rsi,4)
	retq
```

The AArch64 version takes three instructions for the update. `ldr` (load
register) copies the element from memory into `w8`, `add` adds 1 to the
register, and `str` (store register) copies the register back. The `add`
cannot reach memory: every arithmetic instruction in AArch64 reads and writes
registers only, and the only instructions that touch memory are the ones whose
whole job is to move data between memory and registers. An ISA built this way
is a **load/store architecture**.

The x86-64 version takes one. `incl (%rdi,%rsi,4)` adds 1 to the 32-bit value
in memory at `rdi + rsi × 4` and writes the result back to the same place. The
listing uses **AT&T syntax**, the default of the GNU and LLVM tools, which
marks registers with `%` and writes a memory operand as
`(base,index,scale)`; [B2](b2-x86-64.md) teaches it in full. An ISA whose
ordinary arithmetic instructions may name a memory operand, not only
registers, is a **register-memory architecture**.

The processor still reads, adds and writes in the x86-64 version. A
register-memory instruction names all three steps at once; it does not make
them free. What differs is the work left to the compiler. On x86-64 an
instruction selector can often fold a load into the arithmetic that uses it,
and it has to decide when to. On AArch64 the question never arises, which is
one reason a first back end for AArch64 has fewer choices to make.
[C1](c1-instruction-selection.md#the-same-tree-on-x86-64)
treats the folding as a decision about how to cover a tree of operations with
instructions.

Two more differences show in these listings. The AArch64 `add` has three
operands, two sources and a separate destination, so neither input is
overwritten. The x86-64 arithmetic forms mostly write their result over one
of their inputs; B2 returns to what that costs. And the byte counts differ:
the AArch64 function is four instructions of four bytes each, 16 bytes; the
x86-64 one is 4 bytes, three for `incl` and one for `retq`, as `objdump`
shows for the object file. One small function proves nothing general about
code size, but it shows where the difference comes from, which the encoding
section explains.

??? check "An AArch64 compiler wants to compute `a[i] = a[i] + a[j]` for an `i32` array. What is the smallest number of instructions that touch memory, and why can it not be fewer?"

    Three: two loads, `a[i]` and `a[j]`, and one store. On a load/store
    machine the `add` can only work on registers, so each value it reads
    must be loaded first, and the result must be stored by a separate
    instruction. On x86-64, one load of `a[j]` into a register and one
    `add` with a memory destination would do.

## Memory and addresses

Every load and store must say which byte it means. It almost never does so
with a whole address written into the instruction: an address is 64 bits, and
an AArch64 instruction has only 32 bits in all. Instead the instruction names
an **addressing mode**, the rule it uses to compute an address from registers
and small constants, such as "the address in `x0` plus the index in `x1`
times four".

Addressing modes matter because array code is mostly address arithmetic.
Reading `a[i, k]` from a Vortex array of type `[f32; rows, cols]` means
computing, by the row-major rule of
[record 43](../decisions/arrays.md#d43),

$$\text{address}(a) + (i \times \text{cols} + k) \times 4$$

A back end that spent separate instructions on every multiply and add in that
formula would spend most of a matmul kernel's instructions on addresses rather
than on the numbers being multiplied. Addressing modes let a load or store
absorb part of the formula, but only in the shapes the ISA's designers built
in.

### The AArch64 forms

AArch64's loads and stores accept a handful of such shapes. Figure 2 shows
four of them loading one `f32` from an array whose address is in `x0`. The
same four lines are assembled by the example that follows the figure.

<figure class="vx-figure">
<svg viewBox="0 0 760 380" role="img" aria-label="Four AArch64 addressing modes loading from the same array" aria-describedby="a1-addr-desc">
<title id="a1-addr-title">Four AArch64 addressing modes loading from the same array</title>
<desc id="a1-addr-desc">A six-element f32 array is drawn four times, once per addressing mode, with x0 pointing at element 0. Immediate offset, ldr s0, [x0, #16], reads element 4 and leaves x0 unchanged. Scaled register offset, ldr s0, [x0, x1, lsl #2], reads element i, here 2, and leaves x0 unchanged. Pre-index, ldr s0, [x0, #4]!, first moves x0 to element 1 and then reads there. Post-index, ldr s0, [x0], #4, reads element 0 and then moves x0 to element 1.</desc>
<g class="vx-seq" style="--vx-i: 0; --vx-n: 4">
<text class="vx-text" x="20" y="34">immediate offset</text>
<text class="vx-mono" x="200" y="20">ldr s0, [x0, #16]</text>
<text class="vx-text-muted" x="740" y="20" text-anchor="end">reads x0 + 16</text>
<rect x="200" y="46" width="40" height="30" class="vx-box"/>
<rect x="240" y="46" width="40" height="30" class="vx-box"/>
<rect x="280" y="46" width="40" height="30" class="vx-box"/>
<rect x="320" y="46" width="40" height="30" class="vx-box"/>
<rect x="360" y="46" width="40" height="30" class="vx-cell-on"/>
<rect x="400" y="46" width="40" height="30" class="vx-box"/>
<text class="vx-text-muted" x="220" y="94" text-anchor="middle">a[0]</text>
<text class="vx-text-muted" x="380" y="94" text-anchor="middle">a[4]</text>
<text class="vx-mono" x="460" y="66">x0 unchanged</text>
</g>
<g class="vx-seq" style="--vx-i: 1; --vx-n: 4">
<text class="vx-text" x="20" y="124">scaled register</text>
<text class="vx-mono" x="200" y="110">ldr s0, [x0, x1, lsl #2]</text>
<text class="vx-text-muted" x="740" y="110" text-anchor="end">reads x0 + x1 × 4</text>
<rect x="200" y="136" width="40" height="30" class="vx-box"/>
<rect x="240" y="136" width="40" height="30" class="vx-box"/>
<rect x="280" y="136" width="40" height="30" class="vx-cell-on"/>
<rect x="320" y="136" width="40" height="30" class="vx-box"/>
<rect x="360" y="136" width="40" height="30" class="vx-box"/>
<rect x="400" y="136" width="40" height="30" class="vx-box"/>
<text class="vx-text-muted" x="220" y="184" text-anchor="middle">a[0]</text>
<text class="vx-text-muted" x="300" y="184" text-anchor="middle">a[2]</text>
<text class="vx-mono" x="460" y="156">x1 = 2</text>
</g>
<g class="vx-seq" style="--vx-i: 2; --vx-n: 4">
<text class="vx-text" x="20" y="214">pre-index</text>
<text class="vx-mono" x="200" y="200">ldr s0, [x0, #4]!</text>
<text class="vx-text-muted" x="740" y="200" text-anchor="end">x0 = x0 + 4, then read</text>
<rect x="200" y="226" width="40" height="30" class="vx-box"/>
<rect x="240" y="226" width="40" height="30" class="vx-cell-on"/>
<rect x="280" y="226" width="40" height="30" class="vx-box"/>
<rect x="320" y="226" width="40" height="30" class="vx-box"/>
<rect x="360" y="226" width="40" height="30" class="vx-box"/>
<rect x="400" y="226" width="40" height="30" class="vx-box"/>
<text class="vx-text-muted" x="260" y="274" text-anchor="middle">a[1]</text>
<text class="vx-mono" x="460" y="246">x0 ends at a[1]</text>
</g>
<g class="vx-seq" style="--vx-i: 3; --vx-n: 4">
<text class="vx-text" x="20" y="304">post-index</text>
<text class="vx-mono" x="200" y="290">ldr s0, [x0], #4</text>
<text class="vx-text-muted" x="740" y="290" text-anchor="end">read, then x0 = x0 + 4</text>
<rect x="200" y="316" width="40" height="30" class="vx-cell-on"/>
<rect x="240" y="316" width="40" height="30" class="vx-box"/>
<rect x="280" y="316" width="40" height="30" class="vx-box"/>
<rect x="320" y="316" width="40" height="30" class="vx-box"/>
<rect x="360" y="316" width="40" height="30" class="vx-box"/>
<rect x="400" y="316" width="40" height="30" class="vx-box"/>
<text class="vx-text-muted" x="220" y="364" text-anchor="middle">a[0]</text>
<text class="vx-mono" x="460" y="336">x0 ends at a[1]</text>
</g>
</svg>
<figcaption>Figure 2. Four addressing modes, each loading one <code>f32</code> of the same array, with <code>x0</code> holding the array's address at the start of each row. The highlighted cell is the one read. The first two forms leave <code>x0</code> alone; pre-index and post-index move it by one element, before or after the load, which suits a loop that walks through an array.</figcaption>
</figure>

--8<-- "includes/examples/backend/a1-machine-model/aarch64-addressing-modes.s.md"

The scaled register form is the one array code lives on. `lsl #2` shifts the
index left by two bits, which multiplies it by 4, the size of an `f32`, so
`a[i]` costs one load and no separate arithmetic. The shift is not a free
choice. For a 4-byte load the manual allows `#0` or `#2`, and for an 8-byte
load `#0` or `#3`; a single bit in the instruction chooses between "no shift"
and "shift by the size of the access".[^a64-ldr-reg] Write
`ldr s0, [x0, x1, lsl #3]` and `llvm-mc` refuses it with the message
"expected 'lsl' or 'sxtx' with optional shift of #0 or #2". So the scale is
always 1 or the element's own size.

That fits Vortex's scalar types. The sizes [I6](../decisions/implementation.md#i6)
suggests, 4 bytes for `i32`, `u32`, `f32` and `char` and 8 for `f64` and
`usize`, are all powers of two, so an index into a one-dimensional array of
any of them needs no extra instruction. It does not fit everything. The row
stride of a `[f32; 2, 3]`, the distance from one row to the next, is
3 × 4 = 12 bytes, and a struct element can have any size the layout gives it.
Those multiplications have to be done by ordinary instructions before the
load. [A2](a2-aarch64-assembly.md#loads-stores-and-addresses) gives the full
table of forms and their limits.

### The x86-64 form

x86-64 has one general shape for a memory operand, and most instructions that
take a memory operand accept all of it:

$$\text{base} + \text{index} \times \text{scale} + \text{displacement}$$

where base and index are registers, the scale is 1, 2, 4 or 8, and the
displacement is a constant of up to 32 bits written into the
instruction.[^osdev] The scale is independent of the size of the access: a
4-byte load may scale its index by 8. The second function in the example,
`element`, shows both machines handling a row stride that is not a power of
two. For AArch64 clang wrote:

```gas
__Z7elementPA3_Kfll:
	mov	w8, #12
	madd	x8, x1, x8, x0
	ldr	s0, [x8, x2, lsl #2]
	ret
```

`madd` (multiply-add) computes `x1 × 12 + x0`, the start of row `i`, and the
load adds `k × 4` itself; [A2](a2-aarch64-assembly.md#one-element-of-a-fixed-shape-array)
walks through the same three instructions. For x86-64:

```gas
_Z7elementPA3_Kfll:
	leaq	(%rsi,%rsi,2), %rax
	leaq	(%rdi,%rax,4), %rax
	movss	(%rax,%rdx,4), %xmm0
	retq
```

`lea` (load effective address) computes the address a memory operand
describes and writes it into a register, without reading memory.[^fc-lea]
Clang uses it here as a small calculator. The first `lea` computes
`i + i × 2 = 3i`, the second `a + 3i × 4`, the start of row `i`, and the
`movss` load adds `k × 4`. No multiply instruction appears: the 12 was split
into a scale of 2 and a scale of 4. The two machines reach the same address by
different routes, and choosing a route is an instruction selector's job.

??? check "For `a: [f32; 4, 5]`, which parts of the address of `a[i, k]` can the final AArch64 load absorb, and which need their own instructions?"

    The load can absorb the base address and `k × 4`, with the scaled
    register form `[xrow, xk, lsl #2]`. The row part, `i × 20`, cannot be
    absorbed: 20 is neither 1 nor 4, and the load adds only one scaled
    register. So the row start `a + i × 20` needs its own instructions
    first, for example a `mov` of 20 and a `madd`, as in `element`.

## Encoding: an instruction is a fixed pattern of bits

An assembler's last job is to turn each instruction into the bits the
processor fetches. The mapping from an instruction and its operands to those
bits is its **encoding**. A back end that writes object files itself
([B3](b3-object-files.md)), or code into memory at run time
([D2](d2-jit.md)), has to produce encodings without an assembler's help.

### AArch64: one 32-bit word, read field by field

Every A64 instruction is one 32-bit word.[^arm-isets] A decoder first looks at
a few fixed bits to learn which group of instructions it has, then reads the
rest as that group's fields. At the top level, Arm's decode table uses bit 31
and the four bits 28 to 25: a pattern such as `x101` in bits 28 to 25 means
"data processing with registers", and `x1x0` means "a load or a
store".[^a64-index] The next example sorts a few instruction words into their
groups by those bits alone:

--8<-- "includes/examples/backend/a1-machine-model/a64-decode-groups.cpp.md"

Inside the "data processing, register" group, `add x0, x1, x2` has the
**ADD (shifted register)** layout, which Figure 3 draws above the x86-64
instruction it will be compared with.[^a64-add-reg]

<figure class="vx-figure">
<svg viewBox="0 0 760 320" role="img" aria-label="The fields of an AArch64 add word beside the bytes of an x86-64 addss" aria-describedby="a1-enc-desc">
<title id="a1-enc-title">The fields of an AArch64 add word beside the bytes of an x86-64 addss</title>
<desc id="a1-enc-desc">Top: the 32 bits of add x0, x1, x2, drawn to scale from bit 31 on the left to bit 0 on the right, in ten fields: sf, op, S (bits 31 to 29, values 1, 0, 0), the class pattern 01011 (bits 28 to 24), shift (bits 23 and 22, 00), a fixed 0 (bit 21), Rm (bits 20 to 16, 00010 for x2), imm6 (bits 15 to 10, zero), Rn (bits 9 to 5, 00001 for x1) and Rd (bits 4 to 0, 00000 for x0). The word is 0x8b020020 and is stored as bytes 20 00 02 8b. Bottom: the five bytes of addss (%rdi,%rcx,4), %xmm0: prefix F3, opcode 0F 58, ModR/M 04 and SIB 8F. Fixed parts are drawn strong; parts that depend on the operands are drawn in the accent style.</desc>
<text class="vx-text" x="28" y="22">AArch64: add x0, x1, x2</text>
<g class="vx-seq" style="--vx-i: 0; --vx-n: 3">
<rect x="28" y="36" width="66" height="34" rx="3" class="vx-box-strong"/>
<text class="vx-mono" x="61" y="57" text-anchor="middle">sf op S</text>
<rect x="94" y="36" width="110" height="34" rx="3" class="vx-box-strong"/>
<text class="vx-mono" x="149" y="57" text-anchor="middle">01011</text>
<rect x="204" y="36" width="44" height="34" rx="3" class="vx-box"/>
<text class="vx-mono" x="226" y="57" text-anchor="middle">shift</text>
<rect x="248" y="36" width="22" height="34" rx="3" class="vx-box-strong"/>
<text class="vx-mono" x="259" y="57" text-anchor="middle">0</text>
</g>
<g class="vx-seq" style="--vx-i: 1; --vx-n: 3">
<rect x="270" y="36" width="110" height="34" rx="3" class="vx-box-accent"/>
<text class="vx-mono" x="325" y="57" text-anchor="middle">Rm</text>
<rect x="380" y="36" width="132" height="34" rx="3" class="vx-box"/>
<text class="vx-mono" x="446" y="57" text-anchor="middle">imm6</text>
</g>
<g class="vx-seq" style="--vx-i: 2; --vx-n: 3">
<rect x="512" y="36" width="110" height="34" rx="3" class="vx-box-accent"/>
<text class="vx-mono" x="567" y="57" text-anchor="middle">Rn</text>
<rect x="622" y="36" width="110" height="34" rx="3" class="vx-box-accent"/>
<text class="vx-mono" x="677" y="57" text-anchor="middle">Rd</text>
</g>
<text class="vx-text-muted" x="28" y="86">31</text>
<text class="vx-text-muted" x="94" y="86">28</text>
<text class="vx-text-muted" x="204" y="86">23</text>
<text class="vx-text-muted" x="248" y="86">21</text>
<text class="vx-text-muted" x="270" y="86">20</text>
<text class="vx-text-muted" x="380" y="86">15</text>
<text class="vx-text-muted" x="512" y="86">9</text>
<text class="vx-text-muted" x="622" y="86">4</text>
<text class="vx-text-muted" x="732" y="86" text-anchor="end">0</text>
<text class="vx-mono" x="61" y="108" text-anchor="middle">1 0 0</text>
<text class="vx-mono" x="149" y="108" text-anchor="middle">01011</text>
<text class="vx-mono" x="226" y="108" text-anchor="middle">00</text>
<text class="vx-mono" x="259" y="108" text-anchor="middle">0</text>
<text class="vx-mono" x="325" y="108" text-anchor="middle">00010</text>
<text class="vx-mono" x="446" y="108" text-anchor="middle">000000</text>
<text class="vx-mono" x="567" y="108" text-anchor="middle">00001</text>
<text class="vx-mono" x="677" y="108" text-anchor="middle">00000</text>
<text class="vx-text" x="28" y="136">word 0x8b020020, stored as bytes 20 00 02 8b</text>
<line class="vx-line" x1="28" y1="154" x2="732" y2="154"/>
<text class="vx-text" x="28" y="180">x86-64: addss (%rdi,%rcx,4), %xmm0</text>
<g class="vx-seq" style="--vx-i: 0; --vx-n: 3">
<rect x="28" y="194" width="140" height="34" rx="3" class="vx-box-strong"/>
<text class="vx-mono" x="98" y="215" text-anchor="middle">F3</text>
<rect x="168" y="194" width="140" height="34" rx="3" class="vx-box-strong"/>
<text class="vx-mono" x="238" y="215" text-anchor="middle">0F</text>
<rect x="308" y="194" width="140" height="34" rx="3" class="vx-box-strong"/>
<text class="vx-mono" x="378" y="215" text-anchor="middle">58</text>
</g>
<g class="vx-seq" style="--vx-i: 1; --vx-n: 3">
<rect x="448" y="194" width="140" height="34" rx="3" class="vx-box-accent"/>
<text class="vx-mono" x="518" y="215" text-anchor="middle">04</text>
</g>
<g class="vx-seq" style="--vx-i: 2; --vx-n: 3">
<rect x="588" y="194" width="140" height="34" rx="3" class="vx-box-accent"/>
<text class="vx-mono" x="658" y="215" text-anchor="middle">8F</text>
</g>
<text class="vx-text-muted" x="28" y="246">prefix</text>
<text class="vx-text-muted" x="168" y="246">opcode, byte 1</text>
<text class="vx-text-muted" x="308" y="246">opcode, byte 2</text>
<text class="vx-text-muted" x="448" y="246">ModR/M</text>
<text class="vx-text-muted" x="588" y="246">SIB</text>
<rect x="28" y="274" width="16" height="14" rx="2" class="vx-box-strong"/>
<text class="vx-text-muted" x="52" y="286">fixed for this instruction</text>
<rect x="300" y="274" width="16" height="14" rx="2" class="vx-box-accent"/>
<text class="vx-text-muted" x="324" y="286">depends on the operands</text>
</svg>
<figcaption>Figure 3. Two encodings side by side. The AArch64 word (top) is drawn to scale, one unit per bit: fixed fields say which instruction this is, and three five-bit fields name the registers. The x86-64 instruction (bottom) is a string of bytes: a prefix and a two-byte opcode say what it is, and two more bytes describe the memory operand. The AArch64 fields are read at fixed positions; the x86-64 bytes are read one after another, and each one decides what comes next.</figcaption>
</figure>

Reading the AArch64 fields from the left: `sf` = 1 selects 64-bit registers;
`op` = 0 selects add rather than subtract; `S` = 0 leaves the flags alone;
`01011` identifies this class of instruction; `shift` = `00` selects a left
shift, and `imm6`, the shift amount, is 0, so `x2` is added unshifted; bit 21
is always 0 in this form; and `Rm`, `Rn` and `Rd` are the register numbers of
the second source, the first source and the destination.[^a64-add-reg]

Encoding by hand is then arithmetic. Here is `add x7, x8, x9`, field by field:

| Field | Bits | Value | In binary |
| --- | --- | --- | --- |
| `sf`, `op`, `S` | 31 to 29 | 64-bit, add, no flags | `100` |
| class | 28 to 24 | ADD (shifted register) | `01011` |
| `shift`, bit 21 | 23 to 21 | left shift, fixed 0 | `000` |
| `Rm` | 20 to 16 | `x9` | `01001` |
| `imm6` | 15 to 10 | shift amount 0 | `000000` |
| `Rn` | 9 to 5 | `x8` | `01000` |
| `Rd` | 4 to 0 | `x7` | `00111` |

Written out from bit 31 and grouped in fours, the bits are
`1000 1011 0000 1001 0000 0001 0000 0111`, which is `0x8b090107`. The first
byte in memory is the least significant one, `07`, so the bytes are
`07 01 09 8b`, which is what `llvm-mc -show-encoding` prints for
`add x7, x8, x9`. Storing the low byte of a multi-byte value at the lowest
address is called **little-endian** byte order; every instruction and value
in this book is stored that way. The example program does the same arithmetic
in C++:

--8<-- "includes/examples/backend/a1-machine-model/aarch64-add-encoding.cpp.md"

Its third line puts 31 in the `Rm` field. Five bits can count to 31, one past
the thirty-one general registers, and AArch64 uses the spare number for two
different things. In ADD (shifted register) the manual names register 31 the
**zero register**, `xzr`, which reads as zero.[^a64-add-reg] In ADD
(immediate), which adds a constant, the same number means the stack pointer,
`sp`.[^a64-add-imm] `llvm-mc` agrees: it disassembles `0x8b0103e0` as
`add x0, xzr, x1`, and assembles `add x0, sp, #0` to `e0 03 00 91`, a
different instruction with the same register number 31 in its `Rn` field. An
encoder that treats register 31 as an ordinary register is wrong for one of
the two.

??? check "Decode the word `0x8b0700a5` by hand. Which instruction is it?"

    The top byte, `8b` = `1000 1011`, is the same as in every example
    here: `sf` = 1, add, no flags, class `01011`. The next byte, `07` =
    `0000 0111`, gives `shift` = `00`, bit 21 = 0 and `Rm` = `00111` = 7.
    The low 16 bits, `0x00a5` = `0000 0000 1010 0101`, give `imm6` = 0,
    `Rn` = `00101` = 5 and `Rd` = `00101` = 5. So the word is
    `add x5, x5, x7`, and `llvm-mc --disassemble` prints exactly that.

### x86-64: a string of bytes, read in order

An x86-64 instruction is not a fixed number of bits. It is up to 15 bytes,
made of parts that appear in a fixed order: up to four optional legacy
**prefixes**, an opcode of one to three bytes, then a **ModR/M** byte (mode, register, register or memory) that names the
operands, a **SIB** byte (scale, index, base) if the ModR/M byte asks for a
memory address built from registers, and a displacement and an immediate if the
instruction carries them.[^osdev] Which parts are present depends on the
instruction, so a decoder cannot know where one instruction ends until it has
read its way through.

`addss (%rdi,%rcx,4), %xmm0` adds the `f32` in memory at `rdi + rcx × 4` into
the low part of `xmm0`. Its encoding, `F3 0F 58 /r` in Intel's notation, is
the prefix `F3`, the opcode `0F 58`, and a ModR/M byte whose `reg` field names
the register operand (the `/r`).[^fc-addss] Step through the decoding:

<div class="vx-stepper" markdown="1">
<div class="vx-step" markdown="1">

**Step 1. `F3`**

A byte from the legacy prefix range. On string instructions the same byte is
the REP prefix; in front of this opcode it is a **mandatory prefix**, part of
the instruction's identity.[^osdev] Here it selects the scalar
single-precision form.

| Bytes read | Known so far |
| --- | --- |
| `f3` | a prefix; the opcode comes next |

</div>
<div class="vx-step" markdown="1">

**Step 2. `0F`**

An escape byte: the opcode continues in a second table of opcodes.[^osdev]

| Bytes read | Known so far |
| --- | --- |
| `f3 0f` | prefix, then a two-byte opcode |

</div>
<div class="vx-step" markdown="1">

**Step 3. `58`**

`F3 0F 58` is ADDSS, and its opcode entry says a ModR/M byte
follows.[^fc-addss]

| Bytes read | Known so far |
| --- | --- |
| `f3 0f 58` | ADDSS; a ModR/M byte comes next |

</div>
<div class="vx-step" markdown="1">

**Step 4. `04` = `00 000 100`**

ModR/M has three fields: `mod` (2 bits), `reg` (3 bits) and `rm` (3 bits).
`reg` = `000` is `xmm0`, the destination. `mod` = `00` with `rm` = `100`
means "the other operand is in memory, described by a SIB byte, with no
displacement".[^osdev]

| Bytes read | Known so far |
| --- | --- |
| `f3 0f 58 04` | destination `xmm0`; a SIB byte comes next |

</div>
<div class="vx-step" markdown="1">

**Step 5. `8F` = `10 001 111`**

SIB has three fields: `scale` (2 bits), `index` (3) and `base` (3). `scale` =
`10` means 4, `index` = `001` is `rcx`, `base` = `111` is `rdi`.[^osdev]
Nothing else follows, so the instruction ends here.

| Bytes read | Known so far |
| --- | --- |
| `f3 0f 58 04 8f` | `xmm0 += f32 at rdi + rcx × 4`; five bytes |

</div>
</div>

The next example builds those five bytes from the register numbers. It keeps
to registers 0 to 7 on purpose, and its comment names the cases it leaves
out:

--8<-- "includes/examples/backend/a1-machine-model/x86-64-addss-encoding.cpp.md"

Both example programs were checked against an independent assembler before
their output was trusted: `llvm-mc -show-encoding` for AArch64, and for
x86-64, whose target is missing from the LLVM 18 tools on this machine,
`clang -c --target=x86_64-linux-gnu` followed by `objdump -d`.[^llvm-mc]

The same check shows the special cases the example avoids. With `rbp` as the base,
`mod` = `00` would mean "no base register", so the assembler switches to the
form with a one-byte displacement of zero, and `addss (%rbp,%rax,4), %xmm0`
takes six bytes, `f3 0f 58 44 85 00`. With `r8` as the base, a REX byte
carries the base register's fourth bit and sits between the prefix and the
opcode: `addss (%r8,%rax,4), %xmm0` is `f3 41 0f 58 04 80`.[^osdev]

??? check "Encode `addss (%rdi,%rsi,2), %xmm5` by hand. Start from `f3 0f 58`: what are the ModR/M and SIB bytes?"

    ModR/M: `mod` = `00`, `reg` = `101` for `xmm5`, `rm` = `100` for "a SIB
    byte follows": `00 101 100` = `0x2c`. SIB: `scale` = `01` for 2,
    `index` = `110` for `rsi`, `base` = `111` for `rdi`: `01 110 111` =
    `0x77`. The instruction is `f3 0f 58 2c 77`, which is what
    `objdump` shows for the assembled line.

### Two designs, two sets of costs

The observations in this chapter add up to two different designs:

| | AArch64 | x86-64 |
| --- | --- | --- |
| Instruction size | always 4 bytes[^arm-isets] | 1 to 15 bytes[^osdev] |
| Where the next instruction starts | 4 bytes on, always | after this one has been decoded |
| Largest constant in one instruction | small fields, such as 12 bits for `add` and 16 for `movz` (see [A2](a2-aarch64-assembly.md#reaching-a-global-table)) | up to 8 bytes: `movabsq` with a 64-bit constant is 10 bytes long |
| Instructions that touch memory | loads and stores only | most arithmetic may take one memory operand |
| Scale in an address | 1 or the access size | 1, 2, 4 or 8 |

Each column has costs. A fixed width means an AArch64 decoder knows where
every instruction starts without reading the ones before it, and a back end
that writes machine code needs only one kind of encoder: fill fields into a
word. It also means constants and branch distances must fit in fields of a
few bits, so a large constant takes several instructions, as A2 shows.

x86-64
lets short instructions be short, as `retq` at one byte and `incl` with a
memory operand at three show, and lets a full 64-bit constant sit in one
instruction; the price is a decoder, and an encoder, that must deal with
prefixes, optional bytes and special cases such as the two above. The
x86-64 lengths quoted here are from `objdump` on the assembled lines: `c3` for
`retq`, `89 f0` for `movl %esi, %eax`, `48 89 f0` for `movq %rsi, %rax` (the
`48` is a REX prefix selecting 64-bit operands), and ten bytes for the
`movabsq`.

## Why the machine model shapes a back end's decisions

Each later chapter makes decisions this model constrains:

- **Instruction selection** ([C1](c1-instruction-selection.md)) chooses which
  instructions and which addressing modes cover each piece of IR. A selector
  that knows the scaled register form turns `a[i]` into one load; one that
  does not spends a shift and an add on every access in the matmul.
- **Calling conventions** ([A4](a4-calling-conventions.md)) divide the
  register file into roles: which registers carry arguments, which a called
  function may overwrite, which it must preserve.
- **Register allocation** ([C3](c3-linear-scan.md)) fits a program's values
  into 31 or 16 general registers and stores the rest in memory.
- **Object files** ([B3](b3-object-files.md)) and **JIT compilation**
  ([D2](d2-jit.md)) produce the encodings of this chapter for every
  instruction a back end emits, not one at a time by hand.
- **Target descriptions** ([E2](e2-describing-a-target.md)) are how LLVM
  writes all of this down once, registers, instructions and encodings, so
  the rest of the code generator can read it.

## For Vortex

!!! vortex "Exercise"

    **Build an encoder for the instructions one array read needs.** The
    `element` listing above used three instruction forms that a Vortex
    back end also needs for `a[i, k]` on an `f32` array: `movz` (behind
    `mov w8, #12`), `madd`, and `ldr` with a scaled register offset, plus
    `str` in the same form for writing through `&mut c`. In your compiler,
    add a small encoder module that takes the operands of those four forms
    (register numbers, a 16-bit constant and its shift, the scale choice)
    and returns the 32-bit word. Take every field position from the pages
    for MOVZ, MADD, LDR (register, SIMD&FP) and STR (register, SIMD&FP) in
    Arm's DDI 0602.

    Register 31 must be handled as each form defines it:
    check, for every operand of every form, whether the manual says it names
    `sp`, `xzr` or `wzr`.

    **Not yet.** Do not wire the encoder into code generation: until
    [B3](b3-object-files.md), your back end can print assembly text and let
    the system assembler encode it. Do not build a table-driven encoder for
    every instruction, a disassembler, or x86-64 support; [B2](b2-x86-64.md)
    and B3 decide how much of that you need. Do not encode branches, whose
    fields depend on addresses the assembler does not know yet.

    **The test that proves it works.** Use `llvm-mc -show-encoding` as the
    oracle: generate assembly text for each case, run it through `llvm-mc`,
    and compare its bytes with your encoder's, case by case. For each form,
    run every value of each register field (0 to 31) with the other fields
    fixed, every allowed shift, and a few thousand random combinations from
    a fixed seed so that failures repeat. Check the refusals too: the
    encoder must reject a `lsl #3` on a 4-byte `s` load and a constant
    that does not fit in 16 bits, as `llvm-mc` does.

    **Done when** every case matches, and the test fails for each of three
    deliberate, temporary breakages: `Rn` and `Rm` swapped in `madd`, one
    field of the `str` form moved by one bit, and the `lsl #3` refusal
    removed. A test that has never failed has
    not shown that it can.

## Key ideas

!!! recap "You can now answer"

    - **What does an ISA promise, and what does it leave to the microarchitecture?** The meaning of every instruction and the bits that encode it; speed is left to each chip.
    - **What state can an AArch64 instruction change?** The general registers, `sp`, the `v` registers, the NZCV flags, the program counter and memory.
    - **Why does `a[i] += 1` take three AArch64 instructions but one x86-64 instruction?** AArch64 is load/store, so the add works only on registers; x86-64 is register-memory, so one instruction can name the element in memory.
    - **Which part of a Vortex index can an AArch64 load absorb?** A base register plus one index shifted by the element size; a row stride that is not 1 or the element size needs its own instructions.
    - **Why does an AArch64 register field have five bits, and what is register 31?** Five bits name 32 values; 31 of them are `x0` to `x30`, and 31 names `xzr` or `sp` depending on the instruction form.
    - **Why can an x86-64 decoder not know where the next instruction starts without decoding this one?** An instruction's length depends on its prefixes, its opcode and whether ModR/M, SIB, displacement and immediate bytes follow.

## Where this comes back

!!! next "You will use this again in"

    - [A2. Reading and writing AArch64 assembly](a2-aarch64-assembly.md): *addressing modes*, *register names*, *load/store architecture*
    - [A3. Floats and vectors in registers](a3-floats-and-vectors.md): *the `v` registers*
    - [A4. Calling conventions and ABIs](a4-calling-conventions.md): *register file*, *`sp`*
    - [B2. A second target: x86-64](b2-x86-64.md): *register-memory architecture*, *AT&T syntax*, *SIB addressing*
    - [B3. Object files and assemblers](b3-object-files.md): *instruction encoding*, *little-endian byte order*
    - [C1. Instruction selection](c1-instruction-selection.md): *addressing modes*, *folding a load into arithmetic*
    - [C3. Register allocation I: linear scan](c3-linear-scan.md): *register file size*
    - [D2. JIT compilation](d2-jit.md): *fixed 32-bit instruction words*
    - [E4. Testing back ends](e4-testing-backends.md): *`llvm-mc` as an encoding oracle*

## Sources and further reading

Arm's A64 guide (102374) is the gentlest introduction to the AArch64 side;
the DDI 0602 instruction pages are the reference when a bit matters, and the
OSDev page is a compact map of x86-64 encoding before the Intel manual's
volume 2. For a longer machine-level treatment of x86-64 from the C
programmer's side, CS:APP's chapters on machine-level programming are a
standard next step.[^csapp]

[^llvm-mc]: LLVM Project, `llvm-mc` command guide; the encodings in this chapter were produced with `llvm-mc` 18.1.8 (AArch64), and with Apple clang 21 and `objdump` (x86-64), on an Apple M4 Pro with macOS 27, September 2026. <https://llvm.org/docs/CommandGuide/llvm-mc.html>
[^arm-isets]: Arm, "Learn the architecture: A64 Instruction Set Architecture Guide" (102374, version 1.3), "Instruction sets in the Arm architecture": A64 is a fixed-length 32-bit instruction set, and the 64 names the execution state. <https://developer.arm.com/documentation/102374/0103/Instruction-sets-in-the-Arm-architecture>
[^arm-other]: Arm, A64 Instruction Set Architecture Guide (102374, version 1.3), "Registers in AArch64 - other registers": the zero registers, the stack pointer, and the program counter, which is not a general-purpose register. <https://developer.arm.com/documentation/102374/0103/Registers-in-AArch64---other-registers>
[^aapcs64]: Arm, "Procedure Call Standard for the Arm 64-bit Architecture (AArch64)", release 2025Q4, section "Machine Registers": thirty-one 64-bit general-purpose registers, the stack pointer, the NZCV status register, and the thirty-two SIMD and floating-point registers `v0` to `v31`. <https://github.com/ARM-software/abi-aa/blob/main/aapcs64/aapcs64.rst>
[^a64-index]: Arm, "Arm A-profile A64 Instruction Set Architecture" (DDI 0602, 2026-06), "Index by Encoding": the top-level decode table on `op0` and `op1`. <https://developer.arm.com/documentation/ddi0602/2026-06/Index-by-Encoding>
[^a64-add-reg]: Arm, DDI 0602 (2026-06), "ADD (shifted register)": the field layout, the shift encodings, and the register operands that name `xzr` for register 31. <https://developer.arm.com/documentation/ddi0602/2026-06/Base-Instructions/ADD--shifted-register---Add-optionally-shifted-register->
[^a64-add-imm]: Arm, DDI 0602 (2026-06), "ADD (immediate)": the destination and source operands are "register or stack pointer". <https://developer.arm.com/documentation/ddi0602/2026-06/Base-Instructions/ADD--immediate---Add-immediate-value->
[^a64-ldr-reg]: Arm, DDI 0602 (2026-06), "LDR (register)": the shift amount is `#0` or `#2` for a 32-bit load and `#0` or `#3` for a 64-bit load, selected by the `S` bit. <https://developer.arm.com/documentation/ddi0602/2026-06/Base-Instructions/LDR--register---Load-register--register-->
[^osdev]: OSDev Wiki, "X86-64 Instruction Encoding": the 15-byte limit, the order of prefixes, opcode, ModR/M, SIB, displacement and immediate, the register numbers, the REX prefix, mandatory prefixes, and the ModR/M and SIB tables with their special cases. <https://wiki.osdev.org/X86-64_Instruction_Encoding>
[^fc-addss]: Félix Cloutier, x86 and amd64 instruction reference (unofficial, extracted from the Intel SDM), "ADDSS": opcode `F3 0F 58 /r`. <https://www.felixcloutier.com/x86/addss>
[^fc-lea]: Félix Cloutier, x86 and amd64 instruction reference (unofficial, extracted from the Intel SDM), "LEA": computes the effective address of its source operand and stores it in the destination register. <https://www.felixcloutier.com/x86/lea>
[^csapp]: Randal E. Bryant and David R. O'Hallaron, *Computer Systems: A Programmer's Perspective*, 3rd edition, Pearson, 2015. <https://csapp.cs.cmu.edu/>

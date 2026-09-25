# A2. Reading and writing AArch64 assembly

<p class="page-intro">This chapter teaches you to read the AArch64 assembly that compilers write and to write small functions of your own: lines and directives, loads and stores, flags and branches, and the spellings Apple's tools expect. Vortex's first native back end will print this kind of text, and every later chapter on code generation reads it.</p>

<p class="vx-meta" markdown="1">Level: Foundations · Reading time: about 40 minutes · Builds on: [A1. The machine model](a1-machine-model.md)</p>

???+ remember "Before you start, remember"

    ??? question "What does a linker fill in that an object file leaves open?"

        The holes for names defined somewhere else. An object file can say
        "call the thing named `print`" without containing `print`; the linker
        joins it with the code that defines that name.

        Introduced in [6. The first machine code](../compiler/guide/stage-6-first-machine-code.md#from-object-file-to-executable).

    ??? question "In what order does Vortex store the elements of a `[f32; 2, 3]`?"

        Row-major and contiguous: `[0, 0]`, `[0, 1]`, `[0, 2]`, `[1, 0]`,
        `[1, 1]`, `[1, 2]`, four bytes each, with no gaps between them.

        Introduced in [8. Data in memory](../compiler/guide/stage-8-data-in-memory.md#arrays-in-memory). Decision: [record 43](../decisions/arrays.md#d43).

    ??? question "Which values of an `i32` index must a bounds check reject for a four-element array?"

        Every negative value and every value of 4 or more. An index is in
        bounds when `0 <= i < 4`, compared as mathematical integers.

        Introduced in [9. Runtime safety](../compiler/guide/stage-9-runtime-safety.md#array-bounds). Decision: [record 12](../decisions/arrays.md#d12).

    ??? question "What does a function receive when it is called with `&mut values`?"

        Access to the caller's own array, not a copy: writes through the
        parameter change the caller's storage. In machine terms, the function
        receives an address.

        Introduced in [8. Data in memory](../compiler/guide/stage-8-data-in-memory.md#references-and-passing-without-copying). Decision: [record 40](../decisions/references.md#d40).

    ??? question "What is an addressing mode?"

        The rule an instruction uses to compute a memory address from
        registers and constants, such as "a base register plus an index
        scaled by the element size".

        Introduced in [A1. The machine model](a1-machine-model.md).

!!! goals "In this chapter"

    - Read a line of AArch64 assembly and tell its label, mnemonic, operands and comment apart from a directive.
    - Write a small function in assembly, assemble it on macOS and Linux, and call it from C++.
    - Predict the instructions that load one element of a fixed-shape array, and explain where each part of the address comes from.
    - Work out the flags a comparison sets, and choose the signed or unsigned condition that tests what the program means.
    - Translate between Apple's Mach-O spellings and the ELF spellings that Linux uses.

Every back end ends in the same place. Whether the machine code comes from
LLVM, from a C compiler or from your own code generator, the processor runs
AArch64 instructions, and **assembly language** is the form of those
instructions that people can read: one instruction per line, with names
instead of bit patterns. [A1](a1-machine-model.md) described the machine and
its 32-bit instruction words. This chapter is about the text.

You need the text for two jobs. The first is writing it. The simplest native
back end ([B1](b1-simplest-backend.md)) prints assembly and lets the system
assembler and linker finish the work, so every line your compiler emits must
be one the assembler accepts on each platform you support. The second job is
reading, and it lasts longer. When a Vortex kernel is slower than expected, or
a floating-point result differs in its last bit, the assembly is the ground
truth: it shows what the machine was told to do.

The chapter works from small, complete examples: functions written in assembly
and called from C++, which you can run, change and break, set beside the
listings clang writes for the same jobs.

## The smallest complete file

Here is a function that adds two `int` values, written as a complete assembly
file:

--8<-- "includes/examples/backend/a2-aarch64-assembly/add.s.md"

Read it from the bottom. `add w0, w0, w1` is the only line that computes
anything. The **procedure call standard**, the agreement that lets separately
compiled functions call each other, passes the first arguments in registers
`x0` to `x7` (here in their 32-bit forms, `w0` and `w1`) and returns a result
in the same first register[^aapcs64], so adding `w1` into `w0` is the whole
body. `ret` returns to the caller: it branches to the address in register
`x30`, which the caller's `bl` (branch with link) instruction put there[^arm-call].
[A4](a4-calling-conventions.md) covers the call standard in full; this chapter
needs only those facts.

Every line has the same few parts, and Figure 1 labels them. A **label** is a
name followed by a colon, and it names the address of whatever comes next:
`add_ints:` names the address of the `add`. A **mnemonic** such as `add` or
`ldr` chooses the instruction, and the **operands** after it say what the
instruction works on. In AArch64 assembly the destination comes first, so
`add w0, w0, w1` means `w0 = w0 + w1`. A comment runs from `//` to the end of
the line.

<figure class="vx-figure">
<svg viewBox="0 0 760 260" role="img" aria-label="The parts of one line of AArch64 assembly" aria-describedby="a2-line-desc">
<title id="a2-line-title">The parts of one line of AArch64 assembly</title>
<desc id="a2-line-desc">The line "1: ldr w10, [x0, x9, lsl #2] // w10 = v[i]" split into five boxes, each labelled underneath. "1:" is a label, which names this address. "ldr" is the mnemonic, which chooses the instruction. "w10" is the destination, the register written. "[x0, x9, lsl #2]" is the address operand, x0 plus x9 shifted left by two, where the load reads. The comment after two slashes is skipped by the assembler. A second row shows the directive ".p2align 2", an order to the assembler rather than an instruction, and "add w0, w0, w1", which reads destination first as w0 = w0 + w1.</desc>
<g class="vx-seq" style="--vx-i: 0; --vx-n: 5">
<rect class="vx-box" x="20" y="36" width="52" height="40" rx="4"/>
<text class="vx-mono" x="46" y="61" text-anchor="middle">1:</text>
<line class="vx-line" x1="46" y1="76" x2="46" y2="98"/>
<text class="vx-text" x="20" y="118">label</text>
<text class="vx-text-muted" x="20" y="136">names this</text>
<text class="vx-text-muted" x="20" y="152">address</text>
</g>
<g class="vx-seq" style="--vx-i: 1; --vx-n: 5">
<rect class="vx-box" x="92" y="36" width="64" height="40" rx="4"/>
<text class="vx-mono" x="124" y="61" text-anchor="middle">ldr</text>
<line class="vx-line" x1="124" y1="76" x2="124" y2="98"/>
<text class="vx-text" x="92" y="118">mnemonic</text>
<text class="vx-text-muted" x="92" y="136">which</text>
<text class="vx-text-muted" x="92" y="152">instruction</text>
</g>
<g class="vx-seq" style="--vx-i: 2; --vx-n: 5">
<rect class="vx-box-accent" x="176" y="36" width="72" height="40" rx="4"/>
<text class="vx-mono" x="212" y="61" text-anchor="middle">w10,</text>
<line class="vx-line" x1="212" y1="76" x2="212" y2="98"/>
<text class="vx-text" x="176" y="118">destination</text>
<text class="vx-text-muted" x="176" y="136">the register</text>
<text class="vx-text-muted" x="176" y="152">written</text>
</g>
<g class="vx-seq" style="--vx-i: 3; --vx-n: 5">
<rect class="vx-box" x="268" y="36" width="214" height="40" rx="4"/>
<text class="vx-mono" x="375" y="61" text-anchor="middle">[x0, x9, lsl #2]</text>
<line class="vx-line" x1="375" y1="76" x2="375" y2="98"/>
<text class="vx-text" x="268" y="118">address operand</text>
<text class="vx-text-muted" x="268" y="136">x0 + (x9 &lt;&lt; 2):</text>
<text class="vx-text-muted" x="268" y="152">the load reads here</text>
</g>
<g class="vx-seq" style="--vx-i: 4; --vx-n: 5">
<rect class="vx-box" x="502" y="36" width="238" height="40" rx="4"/>
<text class="vx-mono" x="621" y="61" text-anchor="middle">// w10 = v[i]</text>
<line class="vx-line" x1="621" y1="76" x2="621" y2="98"/>
<text class="vx-text" x="502" y="118">comment</text>
<text class="vx-text-muted" x="502" y="136">skipped by the assembler</text>
</g>
<line class="vx-line" x1="20" y1="176" x2="740" y2="176"/>
<rect class="vx-box" x="20" y="196" width="150" height="40" rx="4"/>
<text class="vx-mono" x="95" y="221" text-anchor="middle">.p2align 2</text>
<text class="vx-text" x="186" y="210">directive</text>
<text class="vx-text-muted" x="186" y="227">an order to the assembler,</text>
<text class="vx-text-muted" x="186" y="243">not an instruction</text>
<rect class="vx-box" x="410" y="196" width="170" height="40" rx="4"/>
<text class="vx-mono" x="495" y="221" text-anchor="middle">add w0, w0, w1</text>
<text class="vx-text" x="596" y="210">destination first</text>
<text class="vx-mono" x="596" y="230">w0 = w0 + w1</text>
</svg>
<figcaption>Figure 1. One line from the loop example later in this chapter, taken apart. A line may hold a label, a mnemonic with its operands, and a comment, in that order. Lines that start with a dot are directives for the assembler. Operands put the destination first, and an address built from registers sits in square brackets.</figcaption>
</figure>

The lines that start with a dot are **directives**: orders to the assembler,
not instructions for the processor. `.text` says that what follows is code.
Both formats put code in a **section** of its own, a named region of the
output file: Apple's assembler maps `.text` to the section `__TEXT,__text`,
which holds only instructions[^apple-as], and on Linux the `.text` section
"holds the 'text,' or executable instructions" of a program[^gabi]. Separate
sections for code, read-only data and writable data let the loader give each
region the right permissions.

`.globl` makes a name visible to the linker[^apple-as]; without it the label is
private to this file, and no other file could call the function. `.p2align 2`
pads the output until the address is a multiple of $2^2 = 4$ bytes, the size of
one instruction[^gas-p2align]. Data directives place values instead of
instructions: `.long 0, 1, 4` writes three 4-byte integers, as the table
example later in this chapter does, and `.byte`, `.short` and `.quad` write 1,
2 and 8 bytes per value[^apple-as].

Two labels sit on one address because the two object file formats name C
functions differently. Apple's format, **Mach-O**, adds a leading underscore to
C names, so C code that calls `add_ints` looks for `_add_ints`[^hellosilicon].
Linux's format, **ELF**, uses the name as written. Defining both lets one file
serve both systems. A compiler emits only the spelling its target needs.

To turn the file into machine code, assemble it: `cc -c add.s -o add.o` runs
the system assembler, and `llvm-objdump -d add.o` (or `objdump -d`) prints
each instruction beside its encoding. For this file the encodings are the
words `0x0b010000` for the `add` and `0xd65f03c0` for the `ret`, one 32-bit
word per instruction, as A1 described.

Two cautions for hand-written files. First, write comments with `//`. GNU
`as`, the assembler from GNU binutils, reads `//` as a comment and `;` as the
end of a statement[^gas-chars]; Apple's assembler accepts `//` too, as every
example on this page shows, although clang's Mach-O listings use `;`. Second,
prefer `.p2align` to `.align`: the argument of `.align` is a byte count on some
targets and a power of two on others, and the GNU manual points to `.p2align`
and `.balign` because they behave the same on every architecture[^gas-align].

## Registers by name

The processor has 31 general-purpose registers, and assembly gives each one
two names[^arm-gpr]. `x0` to `x30` name the full 64-bit registers, and `w0`
to `w30` name their low 32 bits. The name you write sets the size of the
operation: `add w0, w1, w2` is a 32-bit addition and `add x0, x1, x2` a
64-bit one[^arm-gpr].

One rule about the two views matters more than any other when you read code:
**writing a `w` register sets the upper 32 bits of its `x` register to
zero**[^arm-gpr]. Compilers rely on it constantly. `mov w9, w1` is a copy and
also a zero-extension, and it is how clang turns an unsigned 32-bit value into
a 64-bit one. The signed counterpart is `sxtw x9, w1` (sign extend word),
which fills the upper half with copies of bit 31[^a64-sxtw].

A few names are not ordinary registers. `xzr` and `wzr` are the **zero
register**: they read as 0 and ignore writes[^arm-other]. `sp` is the stack
pointer. It can serve as the base address of a load or store and appear in a
few arithmetic instructions, but it is not a general-purpose
register[^arm-other]. The program counter, the address of the current
instruction, is not a general-purpose register either and cannot take part in
arithmetic[^arm-other]; branches and the `adr` and `adrp` instructions use it
implicitly.

The procedure call standard gives the registers roles[^aapcs64]:

| Registers | Role under the procedure call standard (AAPCS64) |
| --- | --- |
| `x0` to `x7` | arguments and results |
| `x8` | the address where a large result must be written |
| `x9` to `x15` | scratch: a function may overwrite them without saving them |
| `x16`, `x17` | scratch that the linker may also use between a call and its target |
| `x18` | the platform register, which a platform may reserve; Apple does, so never use it there[^apple-arm64] |
| `x19` to `x28` | saved: a function that uses one must restore it before returning |
| `x29`, `x30` | the frame pointer, and the link register that holds the return address |

A hand-written function that calls no other function, keeps to `x0` to `x15`
and never touches `x18` has nothing to save or restore, which is why every
example in this chapter keeps its own values in `x8` to `x10`. A function that
does make a call must save `x30` first, because its own `bl` overwrites the
return address. [A4](a4-calling-conventions.md) and [A5](a5-stack-frames.md)
cover the rest of the table.

One more rule from the standard affects every function that takes a 32-bit
argument. When an argument is smaller than its register, the unused bits "have
unspecified value"[^aapcs64]. An `int32_t` index arrives in the low half of
`x1`, and the upper half may hold anything. Code that needs the index as a
64-bit number must widen it first, with `sxtw` or `mov w`, before it takes part
in an address.

??? check "A function receives an `int32_t` index of 5 in `w1`, but the upper half of `x1` holds leftover bits. What goes wrong with `ldr w0, [x0, x1, lsl #2]`, and what fixes it?"

    The load uses all 64 bits of `x1`, so the leftover upper bits, times
    four, are added to the address, and the load reads far away from the
    array. Widen first: `sxtw x9, w1`, because the index is signed, then load
    from `[x0, x9, lsl #2]`. The load can also do the widening itself:
    `ldr w0, [x0, w1, sxtw #2]` sign-extends `w1` as part of forming the
    address[^a64-ldr-reg].

## Loads, stores and addresses

AArch64 is a **load-store architecture**: arithmetic works only on registers,
and memory is reached only through load and store instructions
([A1](a1-machine-model.md)). `ldr` (load register) copies from memory into a
register, and `str` (store register) copies a register into memory. The
register name sets the size, as it does for arithmetic: `ldr w0, [...]` moves
4 bytes and `ldr x0, [...]` moves 8. Byte and halfword forms add a suffix, so
`strb w0, [...]` stores the low byte of `w0` and `strh` its low 16
bits[^arm-size].

The address sits in square brackets, and the forms it can take inside them
are the addressing modes[^arm-addr]:

| Written | Address used | Base register afterwards |
| --- | --- | --- |
| `[x1]` | `x1` | unchanged |
| `[x1, #12]` | `x1 + 12` | unchanged |
| `[x1, x2, lsl #2]` | `x1 + (x2 << 2)` | unchanged |
| `[x1, w2, sxtw #2]` | `x1 + (w2 sign-extended, << 2)` | unchanged |
| `[x1, #16]!` | `x1 + 16` | `x1 + 16` (**pre-index**) |
| `[x1], #16` | `x1` | `x1 + 16` (**post-index**) |

The register forms are the ones array code lives on. Their shift amount is not
a free choice: for a 4-byte load it must be `#0` or `#2`, and for an 8-byte
load `#0` or `#3`, so the scale always matches the size of the element being
loaded[^a64-ldr-reg].

The immediate forms have limits too. A plain offset must be a multiple of the
access size, up to 16380 bytes for a 4-byte load, and a pre-index or post-index
step must lie between -256 and 255 bytes[^a64-ldr-imm].
A small offset that is negative or not a multiple of the size is encoded with
the unscaled forms `ldur` and `stur`, which accept -256 to 255[^a64-ldur]. The
assembler picks them for you, but disassemblers and listings print them, as in
`stur x8, [x29, #-24]` for a slot below the frame pointer. A larger offset has
to be computed into a register first.

Stores use the same modes, with one trap for the reader:
`str w8, [x2, x9, lsl #2]` writes `w8` to memory. The register before the
brackets is the value being stored, a source, even though it sits where every
other instruction puts its destination. The pair forms `ldp` and `stp` move two
registers to or from consecutive addresses[^arm-pair]. You will meet
`stp x29, x30, [sp, #-16]!` at the start of many functions: it pushes the frame
pointer and the return address onto the stack in one instruction, and
[A5](a5-stack-frames.md) takes it apart.

### One element of a fixed-shape array

Now a compiler's version. The example below is ordinary C++: `cell` returns
element `[row][col]` of a `2 x 3` matrix of `int32_t`. What it prints matters
less than the listing clang writes for it.

--8<-- "includes/examples/backend/a2-aarch64-assembly/cell.cpp.md"

Compiled with `-O2 -S` by Apple clang 21 on macOS, the function becomes three
instructions and a return. Here is its part of the listing, directives
included; the `.section` line comes from the top of the file:

```gas
	.section	__TEXT,__text,regular,pure_instructions
	.globl	_cell                           ; -- Begin function cell
	.p2align	2
_cell:                                  ; @cell
	.cfi_startproc
; %bb.0:
	mov	w8, #12                         ; =0xc
	madd	x8, x1, x8, x0
	ldr	w0, [x8, x2, lsl #2]
	ret
	.cfi_endproc
                                        ; -- End function
```

Read it with the layout rule in mind. Element `[row][col]` sits at byte offset
$(\text{row} \times 3 + \text{col}) \times 4$ from the start of the matrix, and
clang has split that sum between two instructions. `mov w8, #12` puts the row
stride in bytes, three elements of four bytes each, in `w8`, and by the rule
above it also zeroes the upper half of `x8`. `madd x8, x1, x8, x0` is a
multiply-add: it computes `x1 × x8 + x0`, the address where row `row`
starts[^a64-madd]. The load then adds `col` scaled by 4 on its own. Figure 2
follows the three steps for `row = 1` and `col = 2`.

<figure class="vx-figure">
<svg viewBox="0 0 760 320" role="img" aria-label="Finding element [1][2] of a 2 by 3 matrix" aria-describedby="a2-cell-desc">
<title id="a2-cell-title">Finding element [1][2] of a 2 by 3 matrix</title>
<desc id="a2-cell-desc">Six int32 cells in a row, m[0][0] to m[1][2], at byte offsets 0, 4, 8, 12, 16 and 20 from the address in x0. Row 0 covers the first three cells and row 1 the last three. Three instructions find m[1][2]. mov w8, #12 puts the row stride of 12 bytes in w8. madd x8, x1, x8, x0 computes 1 times 12 plus x0, the start of row 1 at byte 12. ldr w0, [x8, x2, lsl #2] adds 2 times 4 and reads the cell at byte 20, which holds 6.</desc>
<text class="vx-text-muted" x="230" y="18" text-anchor="middle">row 0</text>
<text class="vx-text-muted" x="530" y="18" text-anchor="middle">row 1</text>
<line class="vx-line" x1="84" y1="26" x2="376" y2="26"/>
<line class="vx-line" x1="384" y1="26" x2="676" y2="26"/>
<text class="vx-text-muted" x="74" y="42" text-anchor="end">offset</text>
<text class="vx-text-muted" x="84" y="42">0</text>
<text class="vx-text-muted" x="184" y="42">4</text>
<text class="vx-text-muted" x="284" y="42">8</text>
<text class="vx-text-muted" x="384" y="42">12</text>
<text class="vx-text-muted" x="484" y="42">16</text>
<text class="vx-text-muted" x="584" y="42">20</text>
<rect class="vx-box" x="80" y="48" width="100" height="44"/>
<rect class="vx-box" x="180" y="48" width="100" height="44"/>
<rect class="vx-box" x="280" y="48" width="100" height="44"/>
<rect class="vx-box" x="380" y="48" width="100" height="44"/>
<rect class="vx-box" x="480" y="48" width="100" height="44"/>
<rect class="vx-box-accent" x="580" y="48" width="100" height="44"/>
<text class="vx-mono" x="130" y="75" text-anchor="middle">m[0][0]</text>
<text class="vx-mono" x="230" y="75" text-anchor="middle">m[0][1]</text>
<text class="vx-mono" x="330" y="75" text-anchor="middle">m[0][2]</text>
<text class="vx-mono" x="430" y="75" text-anchor="middle">m[1][0]</text>
<text class="vx-mono" x="530" y="75" text-anchor="middle">m[1][1]</text>
<text class="vx-mono" x="630" y="75" text-anchor="middle">m[1][2]</text>
<polygon class="vx-arrowhead" points="74,106 80,96 86,106"/>
<text class="vx-mono" x="80" y="124" text-anchor="middle">x0</text>
<g class="vx-seq" style="--vx-i: 1; --vx-n: 3">
<polygon class="vx-arrowhead" points="374,106 380,96 386,106"/>
<text class="vx-mono" x="380" y="124" text-anchor="middle">x8</text>
<text class="vx-text-muted" x="380" y="140" text-anchor="middle">after madd</text>
</g>
<g class="vx-seq" style="--vx-i: 2; --vx-n: 3">
<polygon class="vx-arrowhead" points="624,106 630,96 636,106"/>
<text class="vx-text-muted" x="630" y="124" text-anchor="middle">the load</text>
<text class="vx-text-muted" x="630" y="140" text-anchor="middle">reads here</text>
</g>
<text class="vx-text-muted" x="40" y="170">with row = 1 in x1 and col = 2 in x2:</text>
<g class="vx-seq" style="--vx-i: 0; --vx-n: 3">
<rect class="vx-box-strong" x="40" y="182" width="290" height="36" rx="4"/>
<text class="vx-mono" x="54" y="205">mov w8, #12</text>
<text class="vx-text-muted" x="350" y="205">row stride: 3 elements × 4 bytes = 12</text>
</g>
<g class="vx-seq" style="--vx-i: 1; --vx-n: 3">
<rect class="vx-box-strong" x="40" y="228" width="290" height="36" rx="4"/>
<text class="vx-mono" x="54" y="251">madd x8, x1, x8, x0</text>
<text class="vx-text-muted" x="350" y="251">x8 = 1 × 12 + x0: where row 1 starts</text>
</g>
<g class="vx-seq" style="--vx-i: 2; --vx-n: 3">
<rect class="vx-box-strong" x="40" y="274" width="290" height="36" rx="4"/>
<text class="vx-mono" x="54" y="297">ldr w0, [x8, x2, lsl #2]</text>
<text class="vx-text-muted" x="350" y="297">reads x8 + 2 × 4 = x0 + 20: the value 6</text>
</g>
</svg>
<figcaption>Figure 2. The address of <code>m[1][2]</code>, built in three steps. Row-major order puts row 1 twelve bytes after the start of the matrix, and the element two places further along. The constant 12 comes from the type, so the compiler writes it into an instruction; only the row and column numbers arrive while the program runs.</figcaption>
</figure>

The comments in the listing are clang's. `; =0xc` repeats the immediate in
hexadecimal, and `; %bb.0:` names the function's first basic block.
`.cfi_startproc` and `.cfi_endproc` bracket the function's entry in the
unwinding tables[^gas-cfi], the data a debugger or an exception handler uses to
walk up the stack. [D1](d1-debug-info.md) explains them; when you read a
listing for its meaning, skip them.

Now the same function at `-O0`:

```gas
_cell:                                  ; @cell
	.cfi_startproc
; %bb.0:
	sub	sp, sp, #32
	.cfi_def_cfa_offset 32
	str	x0, [sp, #24]
	str	x1, [sp, #16]
	str	x2, [sp, #8]
	ldr	x8, [sp, #24]
	ldr	x9, [sp, #16]
	mov	x10, #12                        ; =0xc
	mul	x10, x9, x10
	ldr	x9, [sp, #8]
	add	x8, x8, x10
	ldr	w0, [x8, x9, lsl #2]
	add	sp, sp, #32
	ret
	.cfi_endproc
```

Without optimization, clang moves the stack pointer down by 32 bytes, stores
each argument into a **stack slot** of its own, loads the values back one at a
time and only then does the arithmetic. The code is longer and slower, and it
is simple to generate: every value has a home in memory, and every operation
loads its inputs from their homes and stores its result back. That is the
strategy of the first back end you will write in [B1](b1-simplest-backend.md),
and clang's `-O0` output shows that it is a legitimate starting point. Keeping
values in registers instead is the job of register allocation
([C3](c3-linear-scan.md)).

### The same access in Vortex

Vortex spells the access `m[row, col]`:

```vortex
// items: valid
fn cell(m: &[i32; 2, 3], row: usize, col: usize) -> i32 {
    return m[row, col];
}
```

Because the shape is part of the type, a Vortex compiler knows the 12-byte
stride when it compiles `cell`, exactly as clang did, and it can use the same
`mov`, `madd` and scaled load. A language whose array shapes are known only at
run time would have to keep the stride in a register, or load it from memory,
for every such access.

Two things differ from the C++ version. The `&` parameter arrives as an
address in a register (`x0`, if Vortex functions follow the platform's call
standard, a choice [A4](a4-calling-conventions.md) discusses), which is how the
"no copy" promise of
[stage 8](../compiler/guide/stage-8-data-in-memory.md#references-and-passing-without-copying)
looks in machine code. And Vortex requires a bounds check on each index
([record 12](../decisions/arrays.md#d12)), which C++ does not, so a Vortex
back end puts a comparison and a branch in front of the load for each of the
two indexes. The section on flags builds that check.

??? check "What changes in the three instructions for a `[usize; 2, 3]`, whose elements are 8 bytes?"

    The row stride becomes 24 bytes, so the constant is `#24`. The element
    is loaded into an `x` register with the scale `lsl #3`, the only other
    shift an 8-byte register-offset load allows[^a64-ldr-reg]:
    `ldr x0, [x8, x2, lsl #3]`. The `madd` is unchanged.

## Reaching a global table

The array in `cell` arrived as an argument. Code that uses a global array,
such as a table of constants, must build the table's address itself, and that
raises a problem: an address is 64 bits and an instruction is only 32, so no
single instruction can hold one.

The answer is to compute addresses relative to the program counter. Code and
data are loaded together, so the distance from an instruction to a table is
fixed once the program is linked, even though the absolute addresses are not.
`adrp` (address of page) takes the address of the 4 KiB page that holds the
instruction itself, adds a signed number of pages, and writes the start of the
resulting page into a register; it reaches any page within ±4 GB[^a64-adrp].
An `add` then supplies the table's offset inside that page, a 12-bit number.
Two instructions, two small fields, and any address in a wide window.

--8<-- "includes/examples/backend/a2-aarch64-assembly/table.cpp.md"

This is the first place where Apple's spelling differs from Linux's inside an
instruction. On Mach-O, `_squares@PAGE` asks for the page and
`_squares@PAGEOFF` for the offset within it[^hellosilicon]. On ELF the page is
written as the plain symbol and the offset, the low 12 bits of the address, as
`:lo12:squares`[^gas-reloc][^aaelf64]. The instructions and their encodings are the
same; only the notation differs. So does the section for read-only data:
`__TEXT,__const` on Mach-O[^apple-as] and `.rodata` on ELF[^gabi].

<figure class="vx-figure">
<svg viewBox="0 0 760 290" role="img" aria-label="How adrp and add reach a global table" aria-describedby="a2-adrp-desc">
<title id="a2-adrp-title">How adrp and add reach a global table</title>
<desc id="a2-adrp-desc">Memory drawn as a row of 4 KiB pages. The adrp instruction sits in the first page, and the table squares sits inside the last page. An arrow arcs from adrp to the start of the table's page: adrp computes that page's address by counting pages from its own page, within plus or minus 4 GB. A short arrow then moves from the page start to the table: add supplies the offset inside the page, the low 12 bits. Below, the pair is spelled for Mach-O as adrp x8, _squares@PAGE and add x8, x8, _squares@PAGEOFF, and for ELF as adrp x8, squares and add x8, x8, :lo12:squares.</desc>
<path class="vx-line" d="M105 78 C 105 0, 590 0, 590 58"/>
<polygon class="vx-arrowhead" points="584,52 590,62 596,52"/>
<text class="vx-text-accent" x="348" y="54" text-anchor="middle">adrp: start of the page, counted in pages from its own</text>
<rect class="vx-box" x="30" y="62" width="140" height="56"/>
<rect class="vx-box" x="170" y="62" width="140" height="56"/>
<rect class="vx-box" x="310" y="62" width="140" height="56"/>
<rect class="vx-box" x="450" y="62" width="140" height="56"/>
<rect class="vx-box" x="590" y="62" width="140" height="56"/>
<rect class="vx-box-strong" x="70" y="78" width="70" height="24" rx="3"/>
<text class="vx-mono" x="105" y="95" text-anchor="middle">adrp</text>
<rect class="vx-box-accent" x="646" y="78" width="76" height="24" rx="3"/>
<text class="vx-mono" x="684" y="95" text-anchor="middle">squares</text>
<line class="vx-line" x1="592" y1="110" x2="638" y2="110"/>
<polygon class="vx-arrowhead" points="638,105 646,110 638,115"/>
<text class="vx-text-muted" x="100" y="140" text-anchor="middle">the adrp's own page</text>
<text class="vx-text-muted" x="380" y="140" text-anchor="middle">each page is 4 KiB</text>
<text class="vx-text-muted" x="660" y="140" text-anchor="middle">the table's page</text>
<text class="vx-text-accent" x="620" y="160" text-anchor="middle">add: the low 12 bits</text>
<circle class="vx-dot" r="5">
<animateMotion dur="6s" repeatCount="indefinite" path="M105 78 C 105 0, 590 0, 590 62 L590 110 L646 110" keyPoints="0;0;1;1" keyTimes="0;0.1;0.8;1" calcMode="linear"/>
</circle>
<rect class="vx-box" x="30" y="186" width="340" height="92" rx="4"/>
<text class="vx-text" x="46" y="210">Mach-O (macOS)</text>
<text class="vx-mono" x="46" y="236">adrp x8, _squares@PAGE</text>
<text class="vx-mono" x="46" y="260">add x8, x8, _squares@PAGEOFF</text>
<rect class="vx-box" x="390" y="186" width="340" height="92" rx="4"/>
<text class="vx-text" x="406" y="210">ELF (Linux)</text>
<text class="vx-mono" x="406" y="236">adrp x8, squares</text>
<text class="vx-mono" x="406" y="260">add x8, x8, :lo12:squares</text>
</svg>
<figcaption>Figure 3. Two instructions reach a global table. <code>adrp</code> finds the start of the table's 4 KiB page by counting pages from its own position, which works anywhere within ±4 GB; <code>add</code> then adds the table's offset inside that page. The linker supplies both numbers. The two formats spell the request differently and produce the same instructions.</figcaption>
</figure>

Neither the compiler nor the assembler knows the final distance, because the
table and the code may end up anywhere in the finished program. So the
assembler leaves a note for the linker, called a **relocation**: "put the page
distance to `squares` into this instruction". For reading assembly, it is
enough to recognize `@PAGE`, `@PAGEOFF` and `:lo12:` as requests for such
notes; [B3](b3-object-files.md) is about the notes themselves.

Disassembling the object files with `llvm-objdump -dr` shows them. For the
Mach-O version it printed `ARM64_RELOC_PAGE21` against the `adrp` and
`ARM64_RELOC_PAGEOFF12` against the `add`; for the ELF version the same pair is
`R_AARCH64_ADR_PREL_PG_HI21` and `R_AARCH64_ADD_ABS_LO12_NC`. The names come
from Apple's relocation header and from Arm's ELF
specification[^xnu-reloc][^aaelf64].

A variable defined in another file or library is often one step further away.
For `extern int shared_count;` Apple clang 21 wrote
`adrp x8, _shared_count@GOTPAGE` and `ldr x8, [x8, _shared_count@GOTPAGEOFF]`:
the pair loads the variable's address from the **global offset table** (GOT),
a table of addresses that the linker and loader fill in[^aaelf64], and a third
instruction loads the value. Mach-O has its own relocations for this
pair[^xnu-reloc], and ELF listings spell the same requests `:got:` and
`:got_lo12:`[^aaelf64]; [B4](b4-linking-and-loading.md) explains the table.

The full Apple listing adds two things around those three instructions: a
label such as `Lloh0:` in front of each, and a line
`.loh AdrpLdrGotLdr Lloh0, Lloh1, Lloh2` after the function. These are
**linker optimization hints**. They tell the linker which instructions compute
one address, so that it can replace them with cheaper ones once the final
addresses are known; only Mach-O object files record them[^llvm-loh]. When you
read for meaning, skip them as you skip the `.cfi` lines.

Tutorials written for Linux often load an address with the pseudo-instruction
`ldr x1, =msg` instead. The assembler puts the address in a **literal pool**, a
block of constants near the code, and loads it from there[^gas-opcodes].
Apple's linker refuses the result[^hellosilicon] (on macOS 27 the link stops
with "Found illegal text-relocations"), so on Apple platforms use the `adrp`
and `add` pair.

Constants meet the same limit on a smaller scale. `mov w8, #12` works because
12 fits in 16 bits. This `mov` is an **alias**, a second name for another
instruction: here `movz`, which moves a 16-bit immediate into a
register[^a64-mov]. Assemblers accept aliases, disassemblers print them, and
Arm's manual gives each one a page that names the instruction behind it.

A wider constant is usually built 16 bits at a time with `movk` (move wide with
keep), which writes 16 bits at a shift of 0 or 16 in a `w` register, or also 32
or 48 in an `x` register, and leaves the other bits alone[^a64-movk]. The
32-bit constant `0x12345678` therefore takes two instructions,
`mov w0, #0x5678` followed by `movk w0, #0x1234, lsl #16`.

Arithmetic immediates are narrower still. `add` and `sub` take an unsigned
12-bit immediate, 0 to 4095, which may be shifted left by 12[^a64-add-imm]. The
assembler accepts `add x0, x0, #4096` and encodes it as
`add x0, x0, #1, lsl #12`, but 4097 fits neither form: for `x + 4097`, Apple
clang 21 wrote `mov w8, #4097` and then `add x0, x0, x8`. A back end meets this
limit whenever it moves `sp` by the size of a large stack frame
([A5](a5-stack-frames.md)).

## Flags and conditions

A conditional branch such as `b.lo` does not compare anything itself. A
comparison runs first and records four facts about its result in the
**condition flags**; a later instruction reads the flags and decides. The next
example makes the flags visible. It is the bounds check that Vortex needs for
an `i32` index into a four-element array, followed by a function that runs the
same two instructions and returns the flags instead of branching on them.

--8<-- "includes/examples/backend/a2-aarch64-assembly/bounds.cpp.md"

It prints:

```text
--8<-- "examples/backend/a2-aarch64-assembly/bounds.expected"
```

The flags are part of the processor's state[^arm-cc], and `index_flags` copies
them into a general-purpose register with `mrs x0, nzcv` so that the program
can print them. They carry nothing across a call: the procedure call standard
leaves them undefined when a function starts and when it returns[^aapcs64].
Each flag answers one question about the last result that set
them[^arm-cc][^arm-carry][^arm-v]:

- **N** (negative): is the top bit of the result 1?
- **Z** (zero): is the result zero?
- **C** (carry): after an addition, did it carry out of the top bit? After a
  subtraction, did it finish *without* borrowing, that is, was the first
  operand at least the second when both are read as unsigned numbers?
- **V** (overflow): did the result leave the signed range, for a 32-bit
  operation below $-2^{31}$ or at $2^{31}$ and above?

Only instructions that ask to set the flags do so: `adds` and `subs` set them,
while `add` and `sub` leave them alone[^arm-cc]. `cmp x9, #4` is an alias of
`subs xzr, x9, #4`: it subtracts, sends the result to the zero register, and
keeps only the flags. `tst` does the same for a bitwise AND[^arm-cc]. Some
branches skip the flags altogether, such as `cbz x0, label`, which branches
when `x0` is zero and leaves the flags unchanged[^a64-cbz]; its partner `cbnz`
branches when the register is not zero.

A **condition code** is a named test of the flags. They come in opposite
pairs[^arm-suffix]:

| Code | Holds when | Meaning after `cmp a, b` |
| --- | --- | --- |
| `eq` / `ne` | Z set / Z clear | a = b / a ≠ b |
| `hs` (also `cs`) / `lo` (also `cc`) | C set / C clear | unsigned a ≥ b / a < b |
| `hi` / `ls` | C set and Z clear / C clear or Z set | unsigned a > b / a ≤ b |
| `ge` / `lt` | N equals V / N differs from V | signed a ≥ b / a < b |
| `gt` / `le` | Z clear and N equals V / Z set or N differs from V | signed a > b / a ≤ b |
| `mi` / `pl` | N set / N clear | result negative / not negative |
| `vs` / `vc` | V set / V clear | signed overflow / no overflow |

Now read the example's output with the table. The comparison is `x9 - 4`, with
the index sign-extended to 64 bits. For the indexes 0 and 3 the result is
negative (N = 1) and the subtraction borrows (C = 0), so both `lt` and `lo`
hold, and both are right. For 4 the result is zero (Z = 1) and neither holds.

The interesting rows are the negative indexes. For -1, `lt` holds, because -1
is less than 4 as a signed number, so a check written with `lt` would let -1
through and read the word before the array. `lo` does not hold: read as
unsigned, the 64-bit pattern of -1 is $2^{64} - 1$, which is not lower than 4.
The same bits have two orders and give two answers, and Figure 4 draws both.

<figure class="vx-figure">
<svg viewBox="0 0 760 280" role="img" aria-label="The same 64-bit values in signed and unsigned order" aria-describedby="a2-order-desc">
<title id="a2-order-title">The same 64-bit values in signed and unsigned order</title>
<desc id="a2-order-desc">Two number lines. The upper line orders 64-bit values as signed numbers, from minus 2 to the 63 on the left to 2 to the 63 minus 1 on the right, with minus 2 to the 31, minus 1, 0 and 4 marked. A dashed region covers everything left of 4: a signed test for less than 4 accepts all of it, negative indexes included, while only 0 to 3 are in bounds. The lower line orders the same values as unsigned numbers, from 0 to 2 to the 64 minus 1, with 4, 2 to the 63 and 2 to the 64 minus 2 to the 31 marked; only 0 to 3 lie below 4. Two flowing arrows carry minus 2 to the 31 and minus 1 from the upper line to 2 to the 64 minus 2 to the 31 and 2 to the 64 minus 1 on the lower line, above 2 to the 63, where an unsigned test rejects them.</desc>
<text class="vx-text" x="40" y="26">signed order: lt, le, gt, ge</text>
<text class="vx-text-muted" x="230" y="54" text-anchor="middle">x &lt; 4 as signed: every negative number passes</text>
<rect class="vx-box-bad" x="40" y="62" width="380" height="16"/>
<rect class="vx-cell-on" x="380" y="62" width="40" height="16"/>
<line class="vx-line" x1="40" y1="78" x2="720" y2="78"/>
<line class="vx-line" x1="40" y1="72" x2="40" y2="84"/>
<line class="vx-line" x1="250" y1="72" x2="250" y2="84"/>
<line class="vx-line" x1="340" y1="72" x2="340" y2="84"/>
<line class="vx-line" x1="380" y1="72" x2="380" y2="84"/>
<line class="vx-line" x1="420" y1="72" x2="420" y2="84"/>
<line class="vx-line" x1="720" y1="72" x2="720" y2="84"/>
<text class="vx-text-muted" x="40" y="100">−2⁶³</text>
<text class="vx-text-muted" x="250" y="100" text-anchor="middle">−2³¹</text>
<text class="vx-text-muted" x="340" y="100" text-anchor="middle">−1</text>
<text class="vx-text-muted" x="380" y="100" text-anchor="middle">0</text>
<text class="vx-text-muted" x="420" y="100" text-anchor="middle">4</text>
<text class="vx-text-muted" x="720" y="100" text-anchor="end">2⁶³ − 1</text>
<path class="vx-flow" d="M250 106 C 250 150, 620 140, 620 192"/>
<polygon class="vx-arrowhead" points="614,188 620,198 626,188"/>
<path class="vx-flow" d="M340 106 C 340 150, 720 140, 720 192"/>
<polygon class="vx-arrowhead" points="714,188 720,198 726,188"/>
<text class="vx-text" x="40" y="160">unsigned order: lo, ls, hi, hs</text>
<text class="vx-text-muted" x="40" y="186">x &lt; 4 as unsigned: only 0 to 3 pass</text>
<rect class="vx-cell-on" x="40" y="192" width="40" height="16"/>
<line class="vx-line" x1="40" y1="208" x2="720" y2="208"/>
<line class="vx-line" x1="40" y1="202" x2="40" y2="214"/>
<line class="vx-line" x1="80" y1="202" x2="80" y2="214"/>
<line class="vx-line" x1="380" y1="202" x2="380" y2="214"/>
<line class="vx-line" x1="620" y1="202" x2="620" y2="214"/>
<line class="vx-line" x1="720" y1="202" x2="720" y2="214"/>
<text class="vx-text-muted" x="40" y="230" text-anchor="middle">0</text>
<text class="vx-text-muted" x="80" y="230" text-anchor="middle">4</text>
<text class="vx-text-muted" x="380" y="230" text-anchor="middle">2⁶³</text>
<text class="vx-text-muted" x="620" y="230" text-anchor="middle">2⁶⁴ − 2³¹</text>
<text class="vx-text-muted" x="720" y="230" text-anchor="end">2⁶⁴ − 1</text>
<line class="vx-line" x1="380" y1="244" x2="720" y2="244"/>
<line class="vx-line" x1="380" y1="238" x2="380" y2="250"/>
<line class="vx-line" x1="720" y1="238" x2="720" y2="250"/>
<text class="vx-text-accent" x="550" y="268" text-anchor="middle">every negative index, sign-extended, lands here</text>
</svg>
<figcaption>Figure 4. One set of 64-bit values, two orders. In signed order a negative index sits below 4, so a signed test lets it through. In unsigned order the same bits sit at or above 2⁶³, beyond any array length, so one unsigned comparison keeps exactly the indexes 0 to 3. The highlighted cells mark the in-bounds indexes; the dashed band marks what a signed test would accept.</figcaption>
</figure>

So the check `0 <= i < n` that [record 12](../decisions/arrays.md#d12) asks for
needs only one comparison: sign-extend the index, compare it with the length,
and branch on `hs` to the error path. Every negative index becomes an unsigned
number of at least $2^{63}$, larger than any length, and fails the same test as
an index that is too large. A `u32` index needs a zero-extension instead, and a
`usize` index can be compared as it stands. Compilers use the unsigned
conditions for lengths, indexes and addresses, so `lo`, `ls`, `hi` and `hs` in
a listing often mark a bounds check or a loop count.

??? check "`cmp w0, w1` with `w0 = -2147483648` and `w1 = 1`: what are N, Z, C and V, and which of `lt` and `lo` holds?"

    The true difference, -2147483649, does not fit in 32 bits. The 32-bit
    result wraps round to 2147483647, so N = 0 and Z = 0, and the signed
    overflow sets V = 1. Read as unsigned, 2147483648 is at least 1, so there
    is no borrow and C = 1. `lt` holds, because N differs from V, and it is
    right: $-2^{31} < 1$. `lo` does not hold. This is why `lt` tests N and V
    together: N alone says "not negative" here, and it is wrong exactly when V
    reports an overflow[^arm-v].

### Choosing without branching

A branch is not the only instruction that reads the flags. `csel w8, w10, w8, gt`
(**conditional select**) writes `w10` to `w8` when `gt` holds and `w8` to itself
otherwise; `cinc` adds one when its condition holds[^arm-csel]; and
`cset w0, lo` writes 1 or 0 depending on the condition[^a64-cset]. Arm's guide
notes that a select removes a branch that the processor might predict wrongly,
and that compilers often use one for a small `if` statement[^arm-csel]. The loop
example below keeps the largest value seen so far with a `csel`.

### Checked arithmetic, half finished

Vortex requires every `i32`, `u32` and `usize` addition, subtraction and
multiplication to be checked, and a failed check is a runtime error
([record 34](../decisions/diagnostics.md#d34)). The flag-setting forms do most
of that work on AArch64: compute with `adds` or `subs` instead of `add` or
`sub`, then branch to the error path on the condition that means "the result
did not fit".

Here is the pattern with the conditions left for you. The first pair is
`total = a + b` on `i32` values in `w0` and `w1`; the second is `count -= 1` on
a `u32` in `w0`:

```text
adds    w8, w0, w1      // i32 addition
b.??    overflow        // which condition?

subs    w8, w0, #1      // u32 subtraction
b.??    overflow        // which condition?
```

Before you open the answer, decide what "did not fit" means in each case, in
terms of N, Z, C and V.

??? check "Which conditions complete the two checks, and what would a `u32` addition use?"

    The `i32` addition fails exactly when the signed result is out of range,
    which is what V records, so the branch is `b.vs`[^arm-v]. The `u32`
    subtraction fails when it has to borrow (`0 - 1` has no `u32` answer), and
    a borrow clears C, so the branch is `b.lo`, which is the same test as
    `b.cc`[^arm-carry]. A `u32` addition fails when it carries out of bit 31,
    which sets C, so it branches on `b.hs` (`b.cs`). Signed and unsigned types
    share the instructions and differ only in the condition.

    Multiplication needs a different check, because none of the multiply
    instructions sets the flags[^a64-index]: one approach computes the full
    64-bit product and tests whether it fits in 32 bits.

## Branches and loops

AArch64 has a small family of branches:

| Instruction | Branches when | Reach |
| --- | --- | --- |
| `b label` | always | ±128 MiB |
| `b.cond label` | the condition holds | ±1 MiB |
| `cbz x0, label` / `cbnz` | the register is zero / not zero | ±1 MiB |
| `tbz x0, #3, label` / `tbnz` | bit 3 of the register is 0 / 1 | ±32 KiB |
| `bl label` | always, saving the return address in `x30` | ±128 MiB |
| `br x0` / `blr x0` | always, to the address in a register (`blr` saves the return address) | any address |
| `ret` | always, to the address in `x30` | any address |

The reach is how far the target may lie from the branch, because the distance
is stored in the instruction itself[^a64-bcond][^a64-cbz][^a64-tbz][^aaelf64];
the register forms can reach any address[^arm-call]. Hand-written code rarely
comes near these limits, but a compiler must plan for them in a large function
or a call to a distant one. For a distant call, the linker may insert a small
stub called a **veneer**, and the call standard allows a veneer to overwrite
`x16` and `x17`[^aapcs64]; [B3](b3-object-files.md) returns to this. `ret` also
tells the processor that this branch is a return from a function, which helps
it predict where execution goes next[^arm-call].

Labels for branch targets come in two kinds. In hand-written code, **numeric
local labels** save inventing names: `1:` defines a label, `1b` refers to the
nearest `1:` backwards, and `1f` to the nearest one forwards[^gas-symbols].
Compilers generate named local labels instead, such as `LBB0_2` in clang's
Mach-O listings and `.LBB0_2` in its ELF listings. Neither kind reaches the
object file's symbol table: `nm` on the compiled objects lists each function
but none of its block labels, and the GNU manual documents the rule for the
`.L` prefix on ELF[^gas-symbols].

The rule matters on Mach-O. Clang ends every file it compiles for Apple
platforms with `.subsections_via_symbols`, which lets the linker cut the file
into blocks at each symbol and drop the blocks that nothing uses[^apple-as]. A
branch target with an ordinary name, such as `loop:`, inside top-level `asm` in
a C++ file does become a symbol (`nm` lists it), so it splits its function in
two; HelloSilicon renames such labels to start with `L`[^hellosilicon].
Numeric labels, as in this chapter's examples, never become symbols.

### Two loops over one array

The loop example finds the largest element of an `int32_t` array twice, with
two different addressing modes, and checks both against a C++ loop:

--8<-- "includes/examples/backend/a2-aarch64-assembly/max_loop.cpp.md"

`max_indexed` keeps an index `i` in `x9` and lets each load scale it. Its shape
is worth learning: the test sits at the bottom of the loop, as in most compiled
loops, and the function jumps straight to it on entry with `b 2f`. Each trip
round the loop then costs one conditional branch, `b.lo 1b`, rather than a test
at the top plus a jump back at the bottom. The loop test uses `lo` because `i`
and the count are unsigned; the comparison of elements uses `gt` because the
elements are signed.

`max_postinc` drops the index. `ldr w10, [x0], #4` loads through `x0` and then
advances `x0` by 4, so the pointer itself walks the array, while the count in
`x1` runs down to zero. `subs x1, x1, #1` both decrements the count and sets Z
when it reaches zero, so `b.ne 1b` needs no separate comparison. Step through
one call, with the registers shown after each instruction:

<div class="vx-stepper" markdown="1">
<div class="vx-step" markdown="1">

**Step 1. On entry**

The call is `max_postinc(v, 3)` with `v = {3, -5, 7}`. The array's address
arrives in `x0` and the count in `x1`.

| `x0` | `x1` | `w8` (best) | `w10` | N Z C V |
| --- | --- | --- | --- | --- |
| `v` | 3 | unset | unset | left over |

</div>
<div class="vx-step" markdown="1">

**Step 2. `ldr w8, [x0], #4`**

The first element becomes the best so far, and the post-index moves `x0` on
to `v[1]`. A load leaves the flags alone.

| `x0` | `x1` | `w8` (best) | `w10` | N Z C V |
| --- | --- | --- | --- | --- |
| `v + 4` | 3 | 3 | unset | left over |

</div>
<div class="vx-step" markdown="1">

**Step 3. `subs x1, x1, #1`, then `b.eq 2f`**

One element is used. The result, 2, is not zero, so Z is clear and the branch
to the end is not taken. C is set because 3 minus 1 needed no borrow.

| `x0` | `x1` | `w8` (best) | `w10` | N Z C V |
| --- | --- | --- | --- | --- |
| `v + 4` | 2 | 3 | unset | 0 0 1 0 |

</div>
<div class="vx-step" markdown="1">

**Step 4. `ldr w10, [x0], #4`, `cmp w10, w8`, `csel w8, w10, w8, gt`**

`w10` gets -5 and `x0` moves to `v[2]`. The comparison computes -5 minus 3:
negative, so N is set. C is also set, because -5 read as unsigned is a large
number, but `gt` ignores C. It needs Z clear and N equal to V, and N differs
from V, so `csel` keeps 3.

| `x0` | `x1` | `w8` (best) | `w10` | N Z C V |
| --- | --- | --- | --- | --- |
| `v + 8` | 2 | 3 | -5 | 1 0 1 0 |

</div>
<div class="vx-step" markdown="1">

**Step 5. `subs x1, x1, #1`, then `b.ne 1b`**

The count drops to 1. Z is clear, so `b.ne` goes back to the label `1:`.

| `x0` | `x1` | `w8` (best) | `w10` | N Z C V |
| --- | --- | --- | --- | --- |
| `v + 8` | 1 | 3 | -5 | 0 0 1 0 |

</div>
<div class="vx-step" markdown="1">

**Step 6. `ldr w10, [x0], #4`, `cmp w10, w8`, `csel w8, w10, w8, gt`**

`w10` gets 7. The comparison computes 7 minus 3 = 4: Z clear and N equal to
V, so `gt` holds and `csel` makes 7 the best so far.

| `x0` | `x1` | `w8` (best) | `w10` | N Z C V |
| --- | --- | --- | --- | --- |
| `v + 12` | 1 | 7 | 7 | 0 0 1 0 |

</div>
<div class="vx-step" markdown="1">

**Step 7. `subs x1, x1, #1`, `b.ne 1b`, `mov w0, w8`, `ret`**

The count reaches 0 and sets Z, so `b.ne` falls through to `2:`. The result
moves to `w0`, where the caller expects it, and `ret` returns 7. `x0` now
points one element past the end, which is harmless because nothing reads
through it.

| `x0` | `x1` | `w0` (result) | `w10` | N Z C V |
| --- | --- | --- | --- | --- |
| `v + 12` | 0 | 7 | 7 | 0 1 1 0 |

</div>
</div>

With vectorization and unrolling turned off
(`-O2 -fno-vectorize -fno-unroll-loops`), Apple clang 21 compiles
`max_reference`, the C++ version in the same file, to this (the `.cfi` lines
are removed):

```gas
_max_reference:                         ; @max_reference
; %bb.0:
	mov	x8, x0
	ldr	w0, [x0]
	cmp	x1, #2
	b.lo	LBB0_3
; %bb.1:
	sub	x9, x1, #1
	add	x8, x8, #4
LBB0_2:                                 ; =>This Inner Loop Header: Depth=1
	ldr	w10, [x8], #4
	cmp	w10, w0
	csel	w0, w10, w0, gt
	subs	x9, x9, #1
	b.ne	LBB0_2
LBB0_3:
	ret
```

The loop body, from `LBB0_2` to `b.ne`, is `max_postinc` instruction for
instruction. Clang turned the indexed loop of the C++ source into a pointer
walk with a count that runs down, and it guards the loop with `cmp x1, #2` and
`b.lo`, because a count below 2 leaves nothing to compare. At the default
`-O2` it goes further and uses vector instructions: each `smax.4s` keeps the
larger element of four pairs at once. [A3](a3-floats-and-vectors.md)
introduces the registers they use, and [P10](../optimize/p10-vectorization.md)
the transformation that produces them.

## Apple's spellings next to Linux's

The two object file formats agree on every instruction and disagree about much
of the text around them. Compiling the same `cell` function for Linux with the
same clang (`--target=aarch64-linux-gnu`) gives, for the function itself:

```gas
	.text
	.globl	cell                            // -- Begin function cell
	.p2align	2
	.type	cell,@function
cell:                                   // @cell
	.cfi_startproc
// %bb.0:
	mov	w8, #12                         // =0xc
	madd	x8, x1, x8, x0
	ldr	w0, [x8, x2, lsl #2]
	ret
.Lfunc_end0:
	.size	cell, .Lfunc_end0-cell
	.cfi_endproc
                                        // -- End function
```

The three instructions are identical. Around them there is no underscore,
comments start with `//`, the section is plain `.text`, and two directives
appear that Mach-O does not use: `.type` marks `cell` as a function and `.size`
records its length in bytes[^gas-type][^gas-size], computed from the local
label `.Lfunc_end0` placed after the last instruction. The table collects the
differences this chapter has met:

| What | Apple (Mach-O) | Linux (ELF) |
| --- | --- | --- |
| C function `f` in assembly | `_f`[^hellosilicon] | `f` |
| Code section | `.text`, the same as `.section __TEXT,__text,regular,pure_instructions`[^apple-as] | `.text`[^gabi] |
| Read-only data | `.section __TEXT,__const`[^apple-as] | `.section .rodata`[^gabi] |
| Page of symbol `s` | `adrp x8, s@PAGE`[^hellosilicon] | `adrp x8, s`[^gas-reloc] |
| Low 12 bits of `s` | `add x8, x8, s@PAGEOFF`[^hellosilicon] | `add x8, x8, :lo12:s`[^gas-reloc] |
| Address of `s` from the GOT | `adrp x8, s@GOTPAGE`, `ldr x8, [x8, s@GOTPAGEOFF]` | `adrp x8, :got:s`, `ldr x8, [x8, :got_lo12:s]`[^aaelf64] |
| Comments in clang's listings | `;` | `//` |
| Vector operands in clang's listings | `smax.4s v0, v1, v2` | `smax v0.4s, v1.4s, v2.4s` |
| Compiler's local labels | `LBB0_2` | `.LBB0_2` |
| Function type and size | not written | `.type`, `.size` |
| Linker optimization hints | `Lloh0:` labels and `.loh` lines[^llvm-loh] | not written |
| Cutting the file into blocks at symbols | `.subsections_via_symbols`, on the last line[^apple-as] | not written |
| Register `x18` | reserved: never use it[^apple-arm64] | platform-specific; portable code avoids it[^aapcs64] |

The two listings can also differ in ways that have nothing to do with the file
format. Asked with `clang -###`, Apple clang 21 reports `-target-cpu apple-m1`
for the macOS target and `-target-cpu generic` for `aarch64-linux-gnu`, so the
same source is tuned for different processors and can come out with different
instruction choices and a different order. When you compare two listings,
compare like with like.

For hand-written code, a portable style follows from the table, and the
examples use it: `//` comments, `.p2align` rather than `.align`, numeric labels
for branch targets, `adrp` for addresses, both spellings of each function name
or an `#if defined(__APPLE__)` around the parts that differ (as `table.cpp`
does), and no `x18`.

## Reading any listing

A procedure that works on any compiler's output:

1. **Find the function.** Search for its label, `_name:` on macOS and `name:`
   on Linux. Everything up to the next function's label belongs to it.
2. **Skip the bookkeeping.** Lines that start with `.cfi` describe the
   function to unwinders and debuggers. Leave them for a second reading.
3. **Mark the blocks.** Every branch ends a basic block, and every branch
   target, such as `LBB0_2:` or `1:`, starts one
   ([stage 7](../compiler/guide/stage-7-functions-and-control-flow.md#basic-blocks-and-control-flow-graphs)).
   Clang also marks a block that no branch targets with a comment such as
   `; %bb.1:`. Draw the arrows between blocks before reading any arithmetic;
   a loop shows up as an arrow that points backwards.
4. **Name the registers.** Write down what each argument register holds on
   entry and follow each value forward. A `w` destination means a 32-bit
   value; a `sxtw`, or a `w` result that is later read through its `x` name,
   means a widening.
5. **Read each condition.** For each flag-setting instruction, find the
   instruction that reads the flags and translate the pair back into a source
   comparison with the condition table. `lo`, `ls`, `hi` and `hs` mean the
   compiler treats the operands as unsigned.
6. **Compare two settings.** Read the `-O0` listing to see every source
   operation, then `-O2` to see what the optimizer kept. Compiler Explorer
   shows several compilers and settings side by side[^godbolt], and every C++
   example on these pages has a link that opens it there.

## For Vortex

!!! vortex "Exercise"

    **Build an assembly check for the stage 10 kernel.** Whatever back end you
    chose in [stage 6](../compiler/guide/stage-6-first-machine-code.md#choosing-a-back-end),
    make your build able to show the AArch64 assembly of a compiled Vortex
    program: from your own emitter, from `llc`, or from your C compiler's
    `-S`, depending on that choice. Then write a test that compiles the
    program from [stage 10](../compiler/guide/stage-10-matrix-multiplication.md#the-program-the-milestone-asks-for)
    and checks three properties of the text for `multiply`:

    1. **No fused multiply-add.** None of `fmadd`, `fmsub`, `fnmadd`,
       `fnmsub`, `fmla` or `fmls` appears, with optimization off or on.
       [Record 56](../decisions/numbers.md#d56) forbids contraction, and these
       mnemonics are what contraction looks like on AArch64;
       [A3](a3-floats-and-vectors.md) explains them.
    2. **Unsigned bounds checks.** With optimization off, every array access
       in the kernel is guarded by a comparison whose flags are read with an
       unsigned condition (`hs`, `hi`, `lo` or `ls`, in a branch, a `cset` or
       a `csel`) on the way to the runtime-error path. A signed condition
       there would let a negative index through ([record 12](../decisions/arrays.md#d12)).
       If your back end rejects negative indexes with a separate test first
       (of the sign bit, or a signed comparison with zero), check for that
       test and for the comparison with the extent that follows it.
    3. **Results go through the reference.** With optimization off, at least
       one `f32` store (`str` or `stur` with an `s` register) uses a base
       register other than `sp` and `x29`. That is the write into `c`, whose
       address arrived with the `&mut c` argument
       ([record 40](../decisions/references.md#d40)); stores relative to `sp`
       or `x29` reach only the function's own stack slots.

    Keep beside the test a copy of the `-O0` assembly for `multiply`,
    annotated by hand: for each instruction, the Vortex expression it came
    from. Use the procedure from "Reading any listing".

    **Not yet.** Do not write your own assembler, object file writer or
    instruction selector for this; [B1](b1-simplest-backend.md) and
    [B3](b3-object-files.md) build those. Do not compare whole listings line
    by line: exact-text tests break on every harmless change, and
    [E4](e4-testing-backends.md) shows how to write checks that survive. Do
    not make the kernel faster; here it only has to be correct and readable.

    **Done when** the test passes on your unmodified compiler on macOS arm64
    (and on Linux arm64, if your CI runs there), and fails for each of three
    deliberate, temporary breakages: contraction allowed, one bounds check
    changed so that a negative index would pass (for a single comparison,
    a signed condition), and the store of `sum` redirected to a stack slot. A test that has never failed has not shown that it can.

    Try the first breakage at both optimization levels, because fusion
    depends on the level: on an Apple M4 Pro with macOS 27 in September 2026,
    Apple clang 21 with its default setting fused `s + a * b` into `fmadd`
    even at `-O0`, while `-ffp-contract=fast`, and the `contract` flag on
    LLVM IR instructions given to `llc` 18.1.8, fused it only at `-O2`.

## Key ideas

!!! recap "You can now answer"

    - **What decides whether an instruction works on 32 or 64 bits?** The register names, `w` for 32 and `x` for 64; writing a `w` register zeroes the upper half of its `x` register.
    - **Where does `ldr w0, [x8, x2, lsl #2]` read?** At `x8 + x2 × 4`; the shift must match the 4-byte size of the access.
    - **Why does a fixed-shape array need so few instructions per access?** Its row stride is a compile-time constant, so a `mov` and a `madd` find the row and the load's own scaling finds the column.
    - **How does code reach a global table?** `adrp` finds the table's 4 KiB page relative to the program counter and `add` adds the low 12 bits; Apple writes `@PAGE` and `@PAGEOFF`, ELF writes the symbol and `:lo12:`.
    - **What does `cmp x9, #4` leave behind?** The N, Z, C and V flags of `x9 - 4`, and nothing else.
    - **Why is one comparison enough to bounds-check a signed index?** After sign extension, a negative index read as unsigned is at least $2^{63}$, so `hs` rejects it together with every index that is too large.
    - **What differs between a Mach-O and an ELF listing of the same function?** The notation: underscores, section names, comment characters, local-label prefixes and relocation operators. The instructions are the same.

## Where this comes back

!!! next "You will use this again in"

    - [A3. Floats and vectors in registers](a3-floats-and-vectors.md): *register views*, *scaled loads*, *fused multiply-add*
    - [A4. Calling conventions and ABIs](a4-calling-conventions.md): *argument registers*, *unspecified upper bits*, *`x18`*
    - [A5. Stack frames](a5-stack-frames.md): *`stp` and pre-index addressing*, *`x29` and `x30`*
    - [B1. The simplest back end that works](b1-simplest-backend.md): *directives*, *labels*, *every value in a stack slot*
    - [B3. Object files and assemblers](b3-object-files.md): *relocations*, *`@PAGE` and `:lo12:`*, *branch reach*
    - [B4. Linking and loading](b4-linking-and-loading.md): *the global offset table*, *`.subsections_via_symbols`*
    - [C1. Instruction selection](c1-instruction-selection.md): *addressing modes*, *multiply-add*
    - [C7. Peephole optimization](c7-peephole.md): *post-index addressing*, *conditional select*
    - [D1. Debug information](d1-debug-info.md): *`.cfi` directives*
    - [O8. Loops: structure, induction variables and bounds checks](../optimize/o8-loops.md): *the unsigned bounds check*
    - [E4. Testing back ends](e4-testing-backends.md): *checking assembly text*

## Sources and further reading

Arm's A64 guide (102374) is the best first read after this chapter; the
instruction pages of DDI 0602 are the reference when a detail matters.
HelloSilicon collects the Apple-specific traps in one place.

[^aapcs64]: Arm, "Procedure Call Standard for the Arm 64-bit Architecture (AArch64)", release 2025Q4: the general-purpose register table, the rule that unused bits of an argument register are unspecified, the NZCV flags being undefined on entry to and return from a public interface, and the use of IP0 and IP1 by linker veneers. <https://github.com/ARM-software/abi-aa/blob/main/aapcs64/aapcs64.rst>
[^aaelf64]: Arm, "ELF for the Arm 64-bit Architecture (AArch64)", release 2025Q4: the relocation tables, including the range checks for branch relocations, and the `adrp` sequences with `:lo12:`, `:got:` and `:got_lo12:`. <https://github.com/ARM-software/abi-aa/blob/main/aaelf64/aaelf64.rst>
[^apple-arm64]: Apple, "Writing ARM64 code for Apple platforms", Apple Developer Documentation: the platforms reserve `x18`. <https://developer.apple.com/documentation/xcode/writing-arm64-code-for-apple-platforms>
[^apple-as]: Apple, *OS X Assembler Reference*, chapter "Assembler Directives" (archived documentation, 2009): `.section`, `.text`, `.const`, `.globl`, `.p2align`, the data directives `.byte`, `.short`, `.long` and `.quad`, and `.subsections_via_symbols`. <https://developer.apple.com/library/archive/documentation/DeveloperTools/Reference/Assembler/040-Assembler_Directives/asm_directives.html>
[^xnu-reloc]: Apple, XNU source, `EXTERNAL_HEADERS/mach-o/arm64/reloc.h`: the Mach-O relocation types for arm64, including the page and page-offset relocations for GOT slots. <https://github.com/apple-oss-distributions/xnu/blob/main/EXTERNAL_HEADERS/mach-o/arm64/reloc.h>
[^llvm-loh]: LLVM Project, `llvm/lib/Target/AArch64/AArch64CollectLOH.cpp`, header comment: what a linker optimization hint (LOH) describes, the `.loh` forms, and which object writer records them. <https://github.com/llvm/llvm-project/blob/main/llvm/lib/Target/AArch64/AArch64CollectLOH.cpp>
[^hellosilicon]: HelloSilicon, GitHub repository `below/HelloSilicon`: the examples of Stephen Smith's *Programming with 64-Bit ARM Assembly Language* adapted to Apple Silicon, with notes on `@PAGE` and `@PAGEOFF`, the underscore prefix, `x18`, the linker's refusal of `LDR X1, =symbol`, and labels in inline assembly. <https://github.com/below/HelloSilicon>
[^gabi]: *System V Application Binary Interface*, draft of 10 June 2013, chapter 4, "Sections": the special sections `.text` and `.rodata`. <https://www.sco.com/developers/gabi/latest/ch4.sheader.html>
[^arm-gpr]: Arm, "Learn the architecture: A64 Instruction Set Architecture Guide" (102374, version 1.3), "Registers in AArch64 - general-purpose registers". <https://developer.arm.com/documentation/102374/0103/Registers-in-AArch64---general-purpose-registers>
[^arm-other]: Arm, A64 Instruction Set Architecture Guide (102374, version 1.3), "Registers in AArch64 - other registers". <https://developer.arm.com/documentation/102374/0103/Registers-in-AArch64---other-registers>
[^arm-size]: Arm, A64 Instruction Set Architecture Guide (102374, version 1.3), "Loads and stores - size". <https://developer.arm.com/documentation/102374/0103/Loads-and-stores---size>
[^arm-addr]: Arm, A64 Instruction Set Architecture Guide (102374, version 1.3), "Loads and stores - addressing". <https://developer.arm.com/documentation/102374/0103/Loads-and-stores---addressing>
[^arm-pair]: Arm, A64 Instruction Set Architecture Guide (102374, version 1.3), "Loads and stores - load pair and store pair". <https://developer.arm.com/documentation/102374/0103/Loads-and-stores---load-pair-and-store-pair>
[^arm-cc]: Arm, A64 Instruction Set Architecture Guide (102374, version 1.3), "Program flow - generating condition code". <https://developer.arm.com/documentation/102374/0103/Program-flow---generating-condition-code>
[^arm-csel]: Arm, A64 Instruction Set Architecture Guide (102374, version 1.3), "Program flow - conditional select instructions". <https://developer.arm.com/documentation/102374/0103/Program-flow---conditional-select-instructions>
[^arm-call]: Arm, A64 Instruction Set Architecture Guide (102374, version 1.3), "Function calls". <https://developer.arm.com/documentation/102374/0103/Function-calls>
[^arm-suffix]: Arm, "Arm Instruction Set Reference Guide" (100076, version 1.0, now marked superseded), "Condition code suffixes and related flags", table D1-2. <https://developer.arm.com/documentation/100076/0100/A64-Instruction-Set-Reference/Condition-Codes/Condition-code-suffixes-and-related-flags>
[^arm-carry]: Arm, "Arm Instruction Set Reference Guide" (100076, version 1.0, now marked superseded), "Carry flag". <https://developer.arm.com/documentation/100076/0100/A64-Instruction-Set-Reference/Condition-Codes/Carry-flag>
[^arm-v]: Arm, "Arm Instruction Set Reference Guide" (100076, version 1.0, now marked superseded), "Overflow flag". <https://developer.arm.com/documentation/100076/0100/A64-Instruction-Set-Reference/Condition-Codes/Overflow-flag>
[^a64-index]: Arm, "Arm A-profile A64 Instruction Set Architecture" (DDI 0602, 2026-06), "Base Instructions", the alphabetic index: the instructions described as "setting flags" include no multiply. <https://developer.arm.com/documentation/ddi0602/2026-06/Base-Instructions>
[^a64-ldr-reg]: Arm, DDI 0602 (2026-06), "LDR (register)". <https://developer.arm.com/documentation/ddi0602/2026-06/Base-Instructions/LDR--register---Load-register--register-->
[^a64-ldr-imm]: Arm, DDI 0602 (2026-06), "LDR (immediate)". <https://developer.arm.com/documentation/ddi0602/2026-06/Base-Instructions/LDR--immediate---Load-register--immediate-->
[^a64-ldur]: Arm, DDI 0602 (2026-06), "LDUR", load register (unscaled). <https://developer.arm.com/documentation/ddi0602/2026-06/Base-Instructions/LDUR--Load-register--unscaled-->
[^a64-madd]: Arm, DDI 0602 (2026-06), "MADD". <https://developer.arm.com/documentation/ddi0602/2026-06/Base-Instructions/MADD--Multiply-add->
[^a64-mov]: Arm, DDI 0602 (2026-06), "MOV (wide immediate)", an alias of MOVZ. <https://developer.arm.com/documentation/ddi0602/2026-06/Base-Instructions/MOV--wide-immediate---Move-wide-immediate-value--an-alias-of-MOVZ->
[^a64-movk]: Arm, DDI 0602 (2026-06), "MOVK". <https://developer.arm.com/documentation/ddi0602/2026-06/Base-Instructions/MOVK--Move-wide-with-keep->
[^a64-add-imm]: Arm, DDI 0602 (2026-06), "ADD (immediate)": an unsigned 12-bit immediate, optionally shifted left by 12. <https://developer.arm.com/documentation/ddi0602/2026-06/Base-Instructions/ADD--immediate---Add-immediate-value->
[^a64-sxtw]: Arm, DDI 0602 (2026-06), "SXTW". <https://developer.arm.com/documentation/ddi0602/2026-06/Base-Instructions/SXTW--Sign-extend-word--an-alias-of-SBFM->
[^a64-adrp]: Arm, DDI 0602 (2026-06), "ADRP". <https://developer.arm.com/documentation/ddi0602/2026-06/Base-Instructions/ADRP--Form-PC-relative-address-to-4KB-page->
[^a64-cset]: Arm, DDI 0602 (2026-06), "CSET", an alias of CSINC. <https://developer.arm.com/documentation/ddi0602/2026-06/Base-Instructions/CSET--Conditional-set--an-alias-of-CSINC->
[^a64-bcond]: Arm, DDI 0602 (2026-06), "B.cond". <https://developer.arm.com/documentation/ddi0602/2026-06/Base-Instructions/B-cond--Branch-conditionally->
[^a64-cbz]: Arm, DDI 0602 (2026-06), "CBZ". <https://developer.arm.com/documentation/ddi0602/2026-06/Base-Instructions/CBZ--Compare-and-branch-on-zero->
[^a64-tbz]: Arm, DDI 0602 (2026-06), "TBZ". <https://developer.arm.com/documentation/ddi0602/2026-06/Base-Instructions/TBZ--Test-bit-and-branch-if-zero->
[^gas-chars]: Free Software Foundation, *Using as* (the GNU assembler manual), section 9.1.3.1, "Special Characters" for AArch64. <https://sourceware.org/binutils/docs/as/AArch64_002dChars.html>
[^gas-reloc]: Free Software Foundation, *Using as*, section 9.1.3.3, "Relocations" for AArch64. <https://sourceware.org/binutils/docs/as/AArch64_002dRelocations.html>
[^gas-opcodes]: Free Software Foundation, *Using as*, section 9.1.6, "Opcodes" for AArch64: the `ldr` with `=` pseudo-instruction and literal pools. <https://sourceware.org/binutils/docs/as/AArch64-Opcodes.html>
[^gas-symbols]: Free Software Foundation, *Using as*, section 5.3, "Symbol Names": local labels and the `.L` prefix. <https://sourceware.org/binutils/docs/as/Symbol-Names.html>
[^gas-p2align]: Free Software Foundation, *Using as*, ".p2align". <https://sourceware.org/binutils/docs/as/P2align.html>
[^gas-align]: Free Software Foundation, *Using as*, ".align". <https://sourceware.org/binutils/docs/as/Align.html>
[^gas-type]: Free Software Foundation, *Using as*, ".type". <https://sourceware.org/binutils/docs/as/Type.html>
[^gas-size]: Free Software Foundation, *Using as*, ".size". <https://sourceware.org/binutils/docs/as/Size.html>
[^gas-cfi]: Free Software Foundation, *Using as*, section 7.13, "CFI directives". <https://sourceware.org/binutils/docs/as/CFI-directives.html>
[^godbolt]: Matt Godbolt and contributors, Compiler Explorer. <https://godbolt.org/>

# B3. Object files and assemblers

<p class="page-intro">This chapter follows assembly text into an object file: the sections that hold its bytes, the symbols that name them, and the relocations that mark the bytes only the linker can finish. It then opens the assembler itself, to show how it encodes an instruction whose target it has not seen yet and how it picks between a short and a long branch. Vortex needs all of it before it can write machine code without the system assembler.</p>

<p class="vx-meta" markdown="1">Level: Intermediate · Reading time: about 45 minutes · Builds on: [B1. The simplest back end that works](b1-simplest-backend.md), [B2. A second target: x86-64](b2-x86-64.md)</p>

???+ remember "Before you start, remember"

    ??? question "What does a linker fill in that an object file leaves open?"

        The holes for names defined somewhere else. An object file can say
        "call the thing named `print`" without containing `print`; the linker
        joins it with the code that defines that name.

        Introduced in [6. The first machine code](../compiler/guide/stage-6-first-machine-code.md#from-object-file-to-executable).

    ??? question "Does the assembler write any bytes for a label such as `loop:`?"

        No. A label names the address of whatever comes next. The assembler
        records that address and moves on; the next instruction or data
        directive supplies the bytes.

        Introduced in [A2. Reading and writing AArch64 assembly](a2-aarch64-assembly.md#the-smallest-complete-file).

    ??? question "Why does AArch64 code reach a global with two instructions, `adrp` and `add`?"

        Every A64 instruction is one 32-bit word, too small to hold a 64-bit
        address. `adrp` computes the address of the 4 KiB page that holds the
        global, relative to its own page, and `add` supplies the low 12 bits.

        Introduced in [A2. Reading and writing AArch64 assembly](a2-aarch64-assembly.md#reaching-a-global-table).

    ??? question "How far can `b`, `b.cond` and `tbz` reach on AArch64?"

        `b` and `bl` reach ±128 MiB, `b.cond`, `cbz` and `cbnz` ±1 MiB, and
        `tbz` and `tbnz` ±32 KiB, because each stores its distance in a field
        of a different width.

        Introduced in [A2. Reading and writing AArch64 assembly](a2-aarch64-assembly.md#branches-and-loops).

    ??? question "How long is an x86-64 instruction?"

        It depends on the instruction: anywhere from 1 to 15 bytes. AArch64
        instructions are always 4.

        Introduced in [A1. The machine model](a1-machine-model.md).

!!! goals "In this chapter"

    - Read an object file's sections, symbols and relocations with `llvm-objdump`, and say which bytes the linker will change.
    - Explain why a zero-filled global costs no space in the object file, and where ELF and Mach-O put it.
    - Name the relocation kinds for a call and for a global's address on AArch64 and x86-64, in both ELF and Mach-O, and tell a relocation from a fixup the assembler resolves itself.
    - Encode an AArch64 branch by hand, including one whose target comes later in the file.
    - Trace branch relaxation on x86-64 to its fixed point, and say why AArch64 needs a different answer for a conditional branch that cannot reach.

[B1](b1-simplest-backend.md) and [B2](b2-x86-64.md) end with assembly
text. Your back end prints it, and the system assembler turns it into the
bytes the processor fetches. This chapter opens that step. The **assembler**
is the program that reads assembly text and writes an **object file**: a
file of machine code and data that is not yet a complete program, because it
may use names whose code or data lives in some other file.[^gabi-intro]

Two questions run through the chapter. What does an object file have to say
so that a linker can finish it? And how does an assembler produce that file
in one sweep over text that refers to labels it has not reached yet? The
answers are the three tables every object file carries, sections, symbols
and relocations, and the two techniques every assembler uses, fixups and
relaxation.

The step is worth understanding even while the system assembler does it for
you. Printing text and running a separate assembler costs time: when LLVM
built its own assembler in 2010, Chris Lattner reported that the external
assembler took about 20% of Clang's compile time for C code at `-O0
-g`.[^lattner-mc] A compiler that writes its own bytes skips that round trip,
and a JIT ([D2](d2-jit.md)) has no assembler to call at all.

## Three holes in five instructions

Here is a small function in AArch64 assembly for macOS. It calls a runtime
function that this file does not define, then loads a counter defined at the
bottom of the same file:

--8<-- "includes/examples/backend/b3-object-files/relocations.s.md"

Assemble it with `cc -c relocations.s -o r.o` and ask `llvm-objdump -dr r.o`
for the instructions and their relocations. On an Apple M4 Pro with macOS 27
and LLVM 18.1.8, in September 2026, it printed:

```text
0000000000000000 <ltmp0>:
       0: 94000000      bl      0x0 <ltmp0>
        0000000000000000:  ARM64_RELOC_BRANCH26   _vx_log_i32
       4: 90000001      adrp    x1, 0x0 <ltmp0>
        0000000000000004:  ARM64_RELOC_PAGE21     _counts
       8: 91000021      add     x1, x1, #0x0
        0000000000000008:  ARM64_RELOC_PAGEOFF12  _counts
       c: b9400020      ldr     w0, [x1]
      10: d65f03c0      ret
```

(`ltmp0` is a temporary label the assembler adds at the start of the
section; the disassembler prints it because it is the nearest name.) Read
the second column. `0xd65f03c0` is `ret`, finished. `0xb9400020` is
`ldr w0, [x1]`, finished: it means the same 32 bits in any program. The
first three words are not finished. `0x94000000` is `bl` with its 26-bit
offset field set to zero, so as it stands it branches to itself. `adrp` and
`add` hold zero page and zero offset.

Each unfinished word has a line under it. That line is a **relocation**: a
record in the object file that says which bytes are unfinished, which name
will finish them and what kind of patch to apply. The assembler leaves a
hole with a zero in it, and writes down how to fill it.

Why is `_counts` a hole at all, when it is defined in this same file? Because
it is in a different **section** from the code, and the distance between two
sections is not decided by the assembler. The linker places every section of
every input file, and only then is the page distance from the `adrp` to
`_counts` known. The rest of this chapter takes the three tables in turn,
then opens the assembler.

## Sections and segments: where bytes live

An object file is split into named regions because code, constants, data
with initial values and data that starts as zero need different treatment.
Code must be executable and should not be writable; data must be writable and
should not be executable. ELF, the format Linux uses, calls these regions
**sections** and describes each one in a **section header table**: its name,
its type, its flags (writable, allocated in memory, executable), its size
and where its bytes are in the file.[^gabi-intro][^gabi-sections]

ELF has a second view of the same bytes for running programs. An executable
also carries a **program header table**, which groups sections into
**segments**, the runs of memory the loader maps with one set of permissions.
The gABI draws these as the "linking view" and the "execution view" of one
file, and says program headers are meaningful only for executables and shared
objects.[^gabi-intro][^gabi-pheader] An object file therefore has sections
and no segments; the linker builds the segments in [B4](b4-linking-and-loading.md).

Mach-O, the format macOS uses, reaches the same place by another road. Its
header is followed by **load commands**, records that describe the rest of the
file, and a segment command lists the sections inside that segment. Section
names carry their segment's name: `__TEXT,__text` for code and
`__DATA,__data` for writable data. In an object file, though, Apple's header
says all sections sit in "one unnamed segment" with no padding, the compact
form an assembler writes; the named `__TEXT` and `__DATA` segments appear when
the linker builds an executable.[^xnu-loader] Apple's prose reference for
Mach-O, *Mach-O Programming Topics*, is archived and no longer updated, so
these headers are the current description of the format.[^macho-topics]

The assembler's job here is bookkeeping. `.text` and `.data` in the source
switch the current section, and every byte that follows is appended to that
section at the section's own **location counter**, the running offset where
the next byte will go.[^salomon] In `relocations.s`, `_touches_globals` is at
offset 0 of `__text` and `_counts` at offset 0 of `__data`. On Mach-O,
`llvm-objdump -h r.o` then lays the sections out one after the other, so
`__data` starts at address 0x14, right after the 20 bytes of code.

### Data that is only zeros

One kind of section holds no bytes at all. ELF's `.bss` "holds uninitialized
data" that the system sets to zero when the program starts, and its type,
`SHT_NOBITS`, means it "occupies no file space".[^gabi-sections] The section
header records only a size. Mach-O has the same idea as a section type,
`S_ZEROFILL`, "zero fill on demand".[^xnu-loader]

The saving is real. A C file containing `int counts[1024] = {0};` and a
four-element array with nonzero values, compiled with Apple clang 21 on the
same machine, produced a 608-byte object file. Its assembly placed `counts`
with the directive `.zerofill __DATA,__common,_counts,4096,2`: 4096 bytes of
memory, no bytes in the file. Compiled for `aarch64-linux-gnu`, the same array
went into a `.bss` section of type `NOBITS` and size 0x1000. The
nonzero array went into `__DATA,__data` and `.data`, 16 bytes each, stored in
the file.

A Vortex program that declares a large array of zeros at the top level,
should a later version allow one, is exactly this case. Emitting it as 4 MiB
of literal zero bytes works, and costs 4 MiB of disk and assembler time for
nothing.

<figure class="vx-figure">
<svg viewBox="0 0 760 380" role="img" aria-label="The object file for relocations.s: two sections, a relocation table whose rows point at three unfinished words, and a symbol table" aria-describedby="b3-anatomy-desc">
<title id="b3-anatomy-title">Inside the object file for relocations.s</title>
<desc id="b3-anatomy-desc">A header box at the top. Below it, the __TEXT,__text section holds five words at offsets 0x00 to 0x10: bl with a zero field, adrp with a zero field and add with a zero field, all three highlighted as holes, then ldr and ret, which are complete. Below that, the __DATA,__data section holds one word at address 0x14, the value 0 labelled _counts. At the bottom left, the relocation table has three rows: offset 0x0, BRANCH26, _vx_log_i32; offset 0x4, PAGE21, _counts; offset 0x8, PAGEOFF12, _counts. An arrow runs from each row up to the hole it describes. At the bottom right, the symbol table lists _touches_globals as global and defined at offset 0 of __text, _counts as local and defined at 0x14 in __data, and _vx_log_i32 as undefined.</desc>
<rect class="vx-box" x="10" y="10" width="740" height="34" rx="4"/>
<text class="vx-text" x="380" y="32" text-anchor="middle">header and load commands (ELF: header and section header table)</text>
<rect class="vx-box" x="10" y="56" width="740" height="92" rx="4"/>
<text class="vx-text-muted" x="20" y="74">__TEXT,__text (ELF: .text)</text>
<rect class="vx-box-accent" x="20" y="84" width="136" height="52" rx="3"/>
<text class="vx-mono" x="88" y="106" text-anchor="middle">0x00 94000000</text>
<text class="vx-text-muted" x="88" y="126" text-anchor="middle">bl: hole</text>
<rect class="vx-box-accent" x="166" y="84" width="136" height="52" rx="3"/>
<text class="vx-mono" x="234" y="106" text-anchor="middle">0x04 90000001</text>
<text class="vx-text-muted" x="234" y="126" text-anchor="middle">adrp: hole</text>
<rect class="vx-box-accent" x="312" y="84" width="136" height="52" rx="3"/>
<text class="vx-mono" x="380" y="106" text-anchor="middle">0x08 91000021</text>
<text class="vx-text-muted" x="380" y="126" text-anchor="middle">add: hole</text>
<rect class="vx-box" x="458" y="84" width="136" height="52" rx="3"/>
<text class="vx-mono" x="526" y="106" text-anchor="middle">0x0c b9400020</text>
<text class="vx-text-muted" x="526" y="126" text-anchor="middle">ldr: complete</text>
<rect class="vx-box" x="604" y="84" width="136" height="52" rx="3"/>
<text class="vx-mono" x="672" y="106" text-anchor="middle">0x10 d65f03c0</text>
<text class="vx-text-muted" x="672" y="126" text-anchor="middle">ret: complete</text>
<rect class="vx-box" x="10" y="160" width="740" height="56" rx="4"/>
<rect class="vx-box" x="458" y="170" width="136" height="36" rx="3"/>
<text class="vx-mono" x="526" y="193" text-anchor="middle">0x14 00000000</text>
<text class="vx-text-muted" x="610" y="186">__DATA,__data</text>
<text class="vx-text-muted" x="610" y="204">(ELF: .data)</text>
<rect class="vx-box" x="10" y="232" width="360" height="136" rx="4"/>
<text class="vx-text-muted" x="20" y="252">relocations for __text</text>
<text class="vx-mono" x="30" y="284">0x0  BRANCH26   _vx_log_i32</text>
<text class="vx-mono" x="30" y="316">0x4  PAGE21     _counts</text>
<text class="vx-mono" x="30" y="348">0x8  PAGEOFF12  _counts</text>
<rect class="vx-box" x="390" y="232" width="360" height="136" rx="4"/>
<text class="vx-text-muted" x="400" y="252">symbol table</text>
<text class="vx-mono" x="400" y="284">_touches_globals</text>
<text class="vx-text-muted" x="580" y="284">global, __text+0</text>
<text class="vx-mono" x="400" y="316">_counts</text>
<text class="vx-text-muted" x="580" y="316">local, 0x14 in __data</text>
<text class="vx-mono" x="400" y="348">_vx_log_i32</text>
<text class="vx-text-muted" x="580" y="348">undefined</text>
<path class="vx-flow" d="M 40 272 C 40 220, 70 180, 80 140"/>
<polygon class="vx-arrowhead" points="74,146 81,136 86,148"/>
<path class="vx-flow" d="M 160 304 C 170 230, 220 190, 230 140"/>
<polygon class="vx-arrowhead" points="224,146 231,136 236,148"/>
<path class="vx-flow" d="M 300 336 C 320 250, 370 190, 378 140"/>
<polygon class="vx-arrowhead" points="372,146 379,136 384,148"/>
</svg>
<figcaption>Figure 1. The object file that <code>relocations.s</code> became, as <code>llvm-objdump</code> showed it. The assembler finished two of the five instructions; for the other three it wrote a zero field and a relocation row that points back at it. <code>_counts</code> is defined in this file, but in another section, so its address is still a hole.</figcaption>
</figure>

## Symbols: names the linker must match up

A **symbol** is an entry in the object file's symbol table: a name and what
the file knows about it. `llvm-objdump -t r.o` printed the table for the
example:

```text
0000000000000000 l     F __TEXT,__text ltmp0
0000000000000014 l     O __DATA,__data _counts
0000000000000014 l     O __DATA,__data ltmp1
0000000000000000 g     F __TEXT,__text _touches_globals
0000000000000000         *UND* _vx_log_i32
```

Each row answers two questions. The first is where the name is defined. A
**defined** symbol names a place in one of this file's sections:
`_touches_globals` is offset 0 of `__text`. An **undefined** symbol, shown
as `*UND*`, is a name the file uses but does not define; ELF marks it with
the section index `SHN_UNDEF`, and when the linker finds a definition in
another file, "this file's references to the symbol will be linked to the
actual definition".[^gabi-symtab] Mach-O marks it with the type
`N_UNDF`.[^xnu-nlist]

The second question is who may see the name. That is the symbol's
**binding**, the `l` or `g` in the listing. A **local** symbol is visible
only inside its own file, so two files may each have a local `counts`
without a clash. A **global** symbol is visible to every file in the
link.[^gabi-symtab] The `.globl` directive is what made `_touches_globals`
global; `_counts` has none, so it stayed local, and no other file could name
it. Mach-O records the same fact as the `N_EXT` bit.[^xnu-nlist]

ELF adds a third binding. A **weak** symbol resembles a global one, "but
their definitions have lower precedence".[^gabi-symtab] Arm's ELF supplement
states the rule plainly: when a link contains both a weak and a non-weak
definition of a name, the non-weak one is used.[^aaelf64] A weak *reference*,
an undefined weak symbol, may stay unresolved without an error.[^aaelf64] Mach-O marks a
weak definition with `N_WEAK_DEF`.[^xnu-nlist] [B4](b4-linking-and-loading.md)
shows the linker applying these rules.

The names themselves differ by platform. Mach-O adds an underscore to C
names, so `vx_log_i32` is spelled `_vx_log_i32` in the file, and ELF uses
the name as written.[^hellosilicon] A back end that calls its runtime must
spell the symbol the way the platform's C compiler spelled the definition,
or the link fails with an undefined symbol.

### Section symbols and addends

The ELF version of the same function shows one more kind of entry.
Assembled for Linux with `llvm-mc -triple=aarch64-linux-gnu -filetype=obj`,
its relocations for the `adrp` and `add` did not name `counts` at all. They
named the section: `R_AARCH64_ADR_PREL_PG_HI21 .data + 0`. The symbol table
had gained an entry of type `SECTION` for `.data`. The gABI describes such
symbols as existing "primarily for relocation".[^gabi-symtab] Because
`counts` is local, nothing outside this file can refer to it, so the
assembler is free to describe its address as "the start of `.data`, plus 0".
The "plus 0" is an **addend**, the constant part of a relocation, and it
would be 8 for a local defined 8 bytes into `.data`.

??? check "`_counts` is defined in the same file as the `adrp` that uses it. Why does the `adrp` still need a relocation, while a `cbnz` to a label in the same function would not?"

    The `adrp` needs the distance in pages from its own address to
    `_counts`, and the two are in different sections. The assembler knows
    each one's offset within its own section, but only the linker decides
    where `__data` goes relative to `__text`, so the distance is unknown
    until then. A `cbnz` to a label in the same function crosses no section
    boundary: the distance is fixed when the assembler lays out `__text`,
    so the assembler fills it in and writes no relocation.

## Relocations: the holes themselves

A relocation has four parts: the **offset** of the bytes to patch within
their section, the **kind** of patch, the **symbol** whose final address it
needs, and the **addend**. The kind names both a formula and a place to put
the result. For `R_AARCH64_CALL26`, Arm's specification gives the formula
`S + A - P`, where S is the symbol's address, A the addend and P the address
of the place being patched, and says to check that the result lies in
$[-2^{27}, 2^{27})$ and to store its bits 27 to 2 in the
instruction.[^aaelf64] The psABI defines the same letters for x86-64, and
there `R_X86_64_PC32` is `S + A - P` into a 32-bit field.[^psabi]

The kind has to carry this much because instructions keep their immediate
fields in different places. Figure 2 draws three AArch64 words. `bl` keeps a
26-bit field at the bottom of the word. `cbz` keeps 19 bits in the middle,
with the register number below them. `adrp` splits its 21-bit page count
into two pieces: two low bits at 30 and 29, and 19 high bits at 23 to 5. A
relocation kind is, in effect, an instruction to the linker saying which bits
to compute and where to scatter them.[^a64-b][^a64-cbz][^a64-adrp]

<figure class="vx-figure">
<svg viewBox="0 0 760 300" role="img" aria-label="Bit layouts of the bl, cbz and adrp instructions, with the fields a relocation or fixup fills highlighted" aria-describedby="b3-fields-desc">
<title id="b3-fields-title">Where the hole sits in three AArch64 words</title>
<desc id="b3-fields-desc">Three 32-bit words drawn from bit 31 on the left to bit 0 on the right. bl: bits 31 to 26 are the fixed pattern 100101, and bits 25 to 0 are the highlighted imm26 field, filled with the distance divided by 4. cbz: bits 31 to 24 are the fixed pattern 10110100, bits 23 to 5 are the highlighted imm19 field, the distance divided by 4, and bits 4 to 0 hold the register. adrp: bit 31 is 1, bits 30 and 29 are the highlighted immlo field, bits 28 to 24 are the pattern 10000, bits 23 to 5 are the highlighted immhi field, and bits 4 to 0 hold the destination register. immlo and immhi together hold the page distance, 21 bits.</desc>
<text class="vx-text-muted" x="100" y="22">bit 31</text>
<text class="vx-text-muted" x="740" y="22" text-anchor="end">bit 0</text>
<text class="vx-mono" x="20" y="61">bl</text>
<rect class="vx-box" x="100" y="36" width="120" height="40"/>
<text class="vx-mono" x="160" y="61" text-anchor="middle">100101</text>
<rect class="vx-box-accent" x="220" y="36" width="520" height="40"/>
<text class="vx-mono" x="480" y="61" text-anchor="middle">imm26 (bits 25 to 0)</text>
<text class="vx-text-muted" x="220" y="96">distance / 4, so ±128 MiB. Filled by BRANCH26 or CALL26.</text>
<text class="vx-mono" x="20" y="141">cbz</text>
<rect class="vx-box" x="100" y="116" width="160" height="40"/>
<text class="vx-mono" x="180" y="141" text-anchor="middle">10110100</text>
<rect class="vx-box-accent" x="260" y="116" width="380" height="40"/>
<text class="vx-mono" x="450" y="141" text-anchor="middle">imm19 (bits 23 to 5)</text>
<rect class="vx-box" x="640" y="116" width="100" height="40"/>
<text class="vx-mono" x="690" y="141" text-anchor="middle">Rt</text>
<text class="vx-text-muted" x="260" y="176">distance / 4, so ±1 MiB. Usually filled by the assembler.</text>
<text class="vx-mono" x="20" y="221">adrp</text>
<rect class="vx-box" x="100" y="196" width="20" height="40"/>
<text class="vx-mono" x="110" y="221" text-anchor="middle">1</text>
<rect class="vx-box-accent" x="120" y="196" width="40" height="40"/>
<text class="vx-mono" x="140" y="221" text-anchor="middle">lo</text>
<rect class="vx-box" x="160" y="196" width="100" height="40"/>
<text class="vx-mono" x="210" y="221" text-anchor="middle">10000</text>
<rect class="vx-box-accent" x="260" y="196" width="380" height="40"/>
<text class="vx-mono" x="450" y="221" text-anchor="middle">immhi (bits 23 to 5)</text>
<rect class="vx-box" x="640" y="196" width="100" height="40"/>
<text class="vx-mono" x="690" y="221" text-anchor="middle">Rd</text>
<text class="vx-text-muted" x="120" y="256">page distance, 21 bits: the low 2 in bits 30 and 29, the high 19 in bits 23 to 5.</text>
<text class="vx-text-muted" x="120" y="276">Filled by PAGE21 or ADR_PREL_PG_HI21.</text>
</svg>
<figcaption>Figure 2. The highlighted bits are what a relocation or a fixup writes; the rest was finished by the assembler. Each shape needs its own relocation kind, because the value, its width and its position all differ. Checked with <code>llvm-mc -show-encoding</code>: <code>adrp x1</code> one page ahead is <code>0xb0000001</code> (bit 29 set), and four pages ahead is <code>0x90000021</code> (bit 5 set).</figcaption>
</figure>

The kinds you will meet first are the ones for the example's two jobs, a
direct call and a global's address. Here they are for the four combinations
this book targets, each observed with `llvm-objdump -dr` on the example
translated for that platform and checked against the defining document:

| What needs patching | ELF AArch64 | Mach-O arm64 | ELF x86-64 | Mach-O x86-64 |
| --- | --- | --- | --- | --- |
| A direct call | `R_AARCH64_CALL26` | `ARM64_RELOC_BRANCH26` | `R_X86_64_PLT32` | `X86_64_RELOC_BRANCH` |
| A direct jump (tail call) | `R_AARCH64_JUMP26` | `ARM64_RELOC_BRANCH26` | as for a call | as for a call |
| A global's page | `R_AARCH64_ADR_PREL_PG_HI21` | `ARM64_RELOC_PAGE21` | `R_X86_64_PC32` on `leaq counts(%rip)` | `X86_64_RELOC_SIGNED` |
| A global's low 12 bits | `R_AARCH64_ADD_ABS_LO12_NC` | `ARM64_RELOC_PAGEOFF12` | none: one 32-bit field holds the whole distance | none |

The ELF AArch64 names come from AAELF64,[^aaelf64] the Mach-O names from
Apple's relocation headers,[^xnu-arm64-reloc][^xnu-x86-reloc] and the ELF
x86-64 names from the psABI.[^psabi] Mach-O arm64 uses one kind for `b` and
`bl`, where ELF has two. x86-64 needs no second kind for a global, because a
RIP-relative `leaq` carries a 32-bit displacement from the next instruction,
which reaches the whole distance in one field.

### Where the addend lives

ELF offers two relocation record formats. `Elf64_Rela` carries an explicit
addend in the record; `Elf64_Rel` does not, and the addend is instead the
value already sitting in the bytes to be patched.[^gabi-reloc] The x86-64
psABI uses only `Rela` records.[^psabi] You can see the addend at work in the
x86-64 dump: the call printed as `R_X86_64_PLT32 vx_log_i32-0x4`. The
displacement is measured from the end of the 5-byte `call`, which is 4 bytes
past the field being patched, so the assembler folds a -4 into the addend.

Mach-O has no `Rela` records. Apple's x86-64 header says the addend "is
encoded in the instruction", and for arm64 a separate `ARM64_RELOC_ADDEND`
record may precede a page relocation to carry one.[^xnu-x86-reloc][^xnu-arm64-reloc]
AAELF64 permits either format on AArch64, and says how a `Rel` addend is
read out of the instruction's own immediate field.[^aaelf64] The practical
rule for a back end author: when you compare your object file with the system
assembler's, compare the addend too, wherever each format keeps it.

### When a branch cannot reach

A relocation's field width is also a limit. `CALL26` and `JUMP26` accept
$-2^{27} \le X < 2^{27}$ bytes, ±128 MiB; `CONDBR19`, for `b.cond`, accepts
±1 MiB; and `TSTBR14`, for `tbz` and `tbnz`, ±32 KiB.[^aaelf64] When a call's
target lands farther away than 128 MiB, the linker may insert a **veneer**, a
short stub placed within reach of the call that jumps the rest of the way.
AAELF64 allows a veneer only for `CALL26`, `JUMP26` and `PLT32`; "in all
other cases" the linker must report an error.[^aaelf64]

A veneer may overwrite `x16` and `x17`, which the procedure call standard
names IP0 and IP1, and the condition flags. So a compiler must assume those
two registers change across any call that the linker could route through a
veneer.[^aapcs64] A conditional branch gets no such help: if a `b.cond` could
end up out of reach, the compiler or assembler has to rewrite it before the
linker sees it. That is one form of relaxation, the subject of the last
section.

??? check "A function ends up 2 MiB long, and a `b.ne` at its top targets a block at its bottom. Can the linker insert a veneer to fix the branch? If not, what must be emitted instead?"

    No. `b.ne` stores a 19-bit word offset, ±1 MiB, and AAELF64 allows
    veneers only for `CALL26`, `JUMP26` and `PLT32` relocations; for any
    other kind the linker must report an error. And a branch within one
    function usually has no relocation at all, because the assembler
    resolves it. The code must be rewritten before that point: invert the
    condition and branch over an unconditional `b`, which reaches
    ±128 MiB:

    ```text
        b.eq    1f          // the opposite condition skips the long jump
        b       far_block
    1:
    ```

## Encoding with holes: fixups

Now the assembler's side. An assembler reads a line, encodes the instruction,
and writes its bytes. When an operand is a label, the encoder may not know
its value yet. LLVM's assembler, described by Chris Lattner when it was new,
has a target-specific **instruction encoder** that turns each instruction
into bytes plus a list of **fixups**, the places whose value depends on a
symbol.[^lattner-mc] `llvm-mc -show-encoding` prints them:

```text
    cbz   x0, done    // encoding: [0bAAA00000,A,A,0xb4]
                      //   fixup A - offset: 0, value: done, kind: fixup_aarch64_pcrel_branch19
```

The `A`s are the bits the fixup owns, in little-endian byte order. They are
the imm19 field of Figure 2: the top three bits of the first byte, all of the
next two bytes, and the low bits of the last one. A fixup and a relocation
describe the same kind of hole. The difference is who fills it. At the end of
the file the assembler resolves every fixup whose value it can now compute
itself, and turns only the rest into relocations.

The example below is a one-pass encoder for a four-line function. It emits
each word as soon as it reads the line, leaves every branch field zero, and
records a fixup. When the section is complete, it resolves each fixup whose
label is in the section and keeps the other as a relocation:

--8<-- "includes/examples/backend/b3-object-files/encode_with_fixups.cpp.md"

It prints:

```text
--8<-- "examples/backend/b3-object-files/encode_with_fixups.expected"
```

`llvm-mc` 18.1.8 wrote the same four words for the same text, and its ELF
object had one relocation, `R_AARCH64_JUMP26` against `tick`, at offset 0xc.

Work the first fixup by hand. `cbz x0, done` is at word 0 and `done` is at
word 3, so the distance is +3 words, 12 bytes. The field stores the distance
in words, because every A64 instruction is 4 bytes and the low two bits of a
distance are always zero. So imm19 is 3, which goes into bits 23 to 5: 3 << 5
is 0x60. `cbz` with register `x0` and a zero field is 0xb4000000, and the
patched word is 0xb4000060.

??? check "Finish the second fixup by hand: `cbnz x0, loop` is at word 2 and `loop` is at word 1. What is the word?"

    The distance is -1 word. In 19 bits of two's complement, -1 is 0x7FFFF.
    Shifted into bits 23 to 5 it becomes 0x7FFFF << 5 = 0x00FFFFE0. `cbnz
    x0` with a zero field is 0xb5000000, so the word is 0xb5ffffe0, as the
    program printed.

## Two passes, or one pass and a patch list

The `cbz` above names `done` two lines before `done` is defined. That is a
**forward reference**, and it is the central problem of assembler design.
David Salomon calls it the future symbol problem and describes the two
classic answers.[^salomon]

A **two-pass assembler** reads the source twice. The first pass looks only
for label definitions: it advances the location counter by each
instruction's size and records every label's address in the symbol table. No
instruction is assembled. The second pass reads the source again and
assembles each instruction with a complete table.[^salomon] The next example
does exactly that for a made-up three-instruction machine, so that no real
encoding gets in the way:

--8<-- "includes/examples/backend/b3-object-files/two_pass_fixups.cpp.md"

It prints:

```text
--8<-- "examples/backend/b3-object-files/two_pass_fixups.expected"
```

`JMP start` is a backward reference: by the time the assembler reads it,
`start` is known, and even a single pass could resolve it on the spot.
`JMP skip` is a forward reference. The two-pass design treats both the same
way, because pass 2 runs only after the table is complete.

A **one-pass assembler** reads the source once. When it meets a future
symbol, it assembles the instruction without the value and remembers where
the hole is; when the symbol is defined, it goes back and fills every hole
that waits for it.[^salomon] Salomon's version threads the list of holes
through the holes themselves. `encode_with_fixups.cpp` keeps a separate
list, which is closer to how a modern assembler records fixups. Figure 3
puts the two strategies side by side.

<figure class="vx-figure">
<svg viewBox="0 0 760 330" role="img" aria-label="A two-pass assembler and a one-pass assembler with a patch list, resolving the same forward branch" aria-describedby="b3-passes-desc">
<title id="b3-passes-title">Two strategies for one forward reference</title>
<desc id="b3-passes-desc">Left panel, two passes. Pass 1 reads the four lines and fills a label table: count_down at 0, loop at 0, done at 12; no bytes are written. Pass 2 reads the lines again and writes the finished words b4000060, d1000400, b5ffffe0, and b with a relocation for tick. Right panel, one pass. The words are written in order as the lines are read; the cbz and cbnz words have zero fields and the b word has a zero field. A patch list holds three entries: word 0 to done, word 2 to loop, word 3 to tick. At the end, arrows from the list fill word 0 with 3 and word 2 with minus 1, and the tick entry becomes a relocation.</desc>
<rect class="vx-box" x="10" y="10" width="360" height="310" rx="4"/>
<text class="vx-text" x="24" y="34">Two passes</text>
<text class="vx-text-muted" x="24" y="60">pass 1: sizes and labels only</text>
<rect class="vx-box-strong" x="24" y="70" width="330" height="80" rx="3"/>
<text class="vx-mono" x="40" y="94">count_down  0</text>
<text class="vx-mono" x="40" y="116">loop        4</text>
<text class="vx-mono" x="40" y="138">done       12</text>
<text class="vx-text-muted" x="210" y="116">no bytes yet</text>
<text class="vx-text-muted" x="24" y="176">pass 2: encode with the full table</text>
<g class="vx-seq" style="--vx-i: 0; --vx-n: 2">
<rect class="vx-box" x="24" y="186" width="330" height="120" rx="3"/>
<text class="vx-mono" x="40" y="210">0x00 b4000060  cbz  done</text>
<text class="vx-mono" x="40" y="234">0x04 d1000400  sub</text>
<text class="vx-mono" x="40" y="258">0x08 b5ffffe0  cbnz loop</text>
<text class="vx-mono" x="40" y="282">0x0c 14000000  b    tick</text>
<text class="vx-text-accent" x="40" y="300">tick: not in the table, so a relocation</text>
</g>
<rect class="vx-box" x="390" y="10" width="360" height="310" rx="4"/>
<text class="vx-text" x="404" y="34">One pass and a patch list</text>
<text class="vx-text-muted" x="404" y="60">words written as the lines are read</text>
<rect class="vx-box" x="404" y="70" width="330" height="110" rx="3"/>
<rect class="vx-box-accent" x="412" y="78" width="314" height="22" rx="2"/>
<text class="vx-mono" x="420" y="94">0x00 b4000000  cbz  ?</text>
<text class="vx-mono" x="420" y="118">0x04 d1000400  sub</text>
<rect class="vx-box-accent" x="412" y="126" width="314" height="22" rx="2"/>
<text class="vx-mono" x="420" y="142">0x08 b5000000  cbnz ?</text>
<rect class="vx-box-accent" x="412" y="150" width="314" height="22" rx="2"/>
<text class="vx-mono" x="420" y="166">0x0c 14000000  b    ?</text>
<text class="vx-text-muted" x="404" y="206">patch list, resolved at the end</text>
<g class="vx-seq" style="--vx-i: 1; --vx-n: 2">
<rect class="vx-box-strong" x="404" y="216" width="330" height="90" rx="3"/>
<text class="vx-mono" x="420" y="240">word 0 -&gt; done: +3, patched</text>
<text class="vx-mono" x="420" y="264">word 2 -&gt; loop: -1, patched</text>
<text class="vx-mono" x="420" y="288">word 3 -&gt; tick: relocation</text>
</g>
</svg>
<figcaption>Figure 3. The same function assembled two ways. A two-pass assembler reads the text twice and never writes an unfinished word. A one-pass assembler writes each word at once, holes included, and keeps a list of the holes to fill when the section is complete. Both end with the same three finished words and one relocation.</figcaption>
</figure>

Salomon ties the choice to the output. A classic one-pass assembler loaded its
code straight into memory, while a two-pass assembler wrote a relocatable
object file for a loader.[^salomon] A modern assembler does both jobs at once:
it keeps its fixups, resolves what it can inside each section, and hands the
rest to the linker as relocations. For a fixed-length instruction set such as
AArch64, one pass and a patch list is enough, because every instruction is 4
bytes and every label's address is known the moment the assembler reaches it.
x86-64 breaks that assumption.

## Relaxation: when a jump's size depends on its distance

On x86-64, a conditional jump has two sizes. Assembled with Apple clang 21's
integrated assembler for `x86_64-linux-gnu`, a `jne` to a label one byte
ahead came out as `75 01`, 2 bytes with a one-byte displacement. The same
`jne` over 200 bytes of padding came out as `0f 85 c8 00 00 00`, 6 bytes with
a four-byte displacement. A signed byte covers -128 to 127, so the short form
reaches only that far, counted from the end of the jump.

The assembler wants the short form wherever it fits, but whether it fits
depends on the distance, and the distance depends on the sizes of every
instruction in between, including other jumps. Lattner describes the cycle
this way: "the size of this instruction depends on how far apart these two
labels are".[^lattner-mc] Choosing the forms is called **relaxation**, and
LLVM's assembler performs it as it lays out the code into
sections.[^lattner-mc]

The example below relaxes a toy layout with two forward jumps. Every jump
starts short. Each round lays out the code with the current sizes, and
widens any jump that cannot reach:

--8<-- "includes/examples/backend/b3-object-files/relax_branches.cpp.md"

It prints:

```text
--8<-- "examples/backend/b3-object-files/relax_branches.expected"
```

Walk the rounds with Figure 4. In round 1, jump 0 reaches `Lx` with 126
bytes, which fits a signed byte. Jump 1 needs 134 bytes to reach `Ly`, so it
widens. That adds 4 bytes between jump 0 and `Lx`, and in round 2 jump 0's
distance becomes 130, which no longer fits: jump 0 widens too, although
nothing about jump 0 itself changed. Round 3 changes nothing, so the layout
is final at 206 bytes.

<figure class="vx-figure">
<svg viewBox="0 0 760 320" role="img" aria-label="Three rounds of branch relaxation: widening one jump pushes the other's target out of reach" aria-describedby="b3-relax-desc">
<title id="b3-relax-title">Relaxation reaches a fixed point</title>
<desc id="b3-relax-desc">Three horizontal bars, one per round, drawn to scale. Each bar holds jump 0, 60 filler bytes, jump 1, 64 filler bytes, the label Lx, 70 filler bytes and the label Ly. A dashed tick on each bar marks the farthest byte jump 0 can reach in its short form. Round 1: both jumps are 2 bytes; Lx at 128 is inside jump 0's short reach; jump 1 must reach Ly at 198, 134 bytes away, and is marked to widen. Round 2: jump 1 is now 6 bytes, which moves Lx to 132, past jump 0's short reach at 129; jump 0 is marked to widen. Round 3: both jumps are 6 bytes, Lx is at 136 and Ly at 206; nothing changes.</desc>
<text class="vx-text-muted" x="20" y="56">round 1</text>
<rect class="vx-box" x="110" y="40" width="6" height="26"/>
<rect class="vx-box" x="116" y="40" width="180" height="26"/>
<rect class="vx-box-bad" x="296" y="40" width="6" height="26"/>
<rect class="vx-box" x="302" y="40" width="192" height="26"/>
<rect class="vx-box" x="494" y="40" width="210" height="26"/>
<line class="vx-line" x1="494" y1="34" x2="494" y2="72"/>
<text class="vx-mono" x="494" y="88" text-anchor="middle">Lx 128</text>
<line class="vx-line" x1="704" y1="34" x2="704" y2="72"/>
<text class="vx-mono" x="704" y="88" text-anchor="middle">Ly 198</text>
<line class="vx-line" x1="497" y1="30" x2="497" y2="76" stroke-dasharray="4 3"/>
<text class="vx-text-muted" x="116" y="30">jump 0: 2 bytes, reaches Lx</text>
<text class="vx-text-accent" x="300" y="100">jump 1: needs 134, widen</text>
<g class="vx-seq" style="--vx-i: 1; --vx-n: 3">
<text class="vx-text-muted" x="20" y="156">round 2</text>
<rect class="vx-box-bad" x="110" y="140" width="6" height="26"/>
<rect class="vx-box" x="116" y="140" width="180" height="26"/>
<rect class="vx-box-strong" x="296" y="140" width="18" height="26"/>
<rect class="vx-box" x="314" y="140" width="192" height="26"/>
<rect class="vx-box" x="506" y="140" width="210" height="26"/>
<line class="vx-line" x1="506" y1="134" x2="506" y2="172"/>
<text class="vx-mono" x="506" y="188" text-anchor="middle">Lx 132</text>
<line class="vx-line" x1="716" y1="134" x2="716" y2="172"/>
<text class="vx-mono" x="716" y="188" text-anchor="middle">Ly 202</text>
<line class="vx-line" x1="497" y1="130" x2="497" y2="176" stroke-dasharray="4 3"/>
<text class="vx-text-accent" x="116" y="130">jump 0: needs 130, past its short reach, widen</text>
</g>
<g class="vx-seq" style="--vx-i: 2; --vx-n: 3">
<text class="vx-text-muted" x="20" y="256">round 3</text>
<rect class="vx-box-strong" x="110" y="240" width="18" height="26"/>
<rect class="vx-box" x="128" y="240" width="180" height="26"/>
<rect class="vx-box-strong" x="308" y="240" width="18" height="26"/>
<rect class="vx-box" x="326" y="240" width="192" height="26"/>
<rect class="vx-box" x="518" y="240" width="210" height="26"/>
<line class="vx-line" x1="518" y1="234" x2="518" y2="272"/>
<text class="vx-mono" x="518" y="288" text-anchor="middle">Lx 136</text>
<line class="vx-line" x1="728" y1="234" x2="728" y2="272"/>
<text class="vx-mono" x="728" y="288" text-anchor="middle">Ly 206</text>
<text class="vx-text-muted" x="128" y="230">both jumps 6 bytes: nothing changes, done</text>
</g>
<text class="vx-text-muted" x="110" y="314">3 pixels per byte; the dashed line marks byte 129, the farthest a short jump at 0 reaches</text>
</svg>
<figcaption>Figure 4. The rounds printed by <code>relax_branches.cpp</code>, to scale. Widening jump 1 in round 1 moves <code>Lx</code> four bytes further from jump 0, past the reach of its short form. Jumps only ever grow, so the process stops; here it stops after three rounds.</figcaption>
</figure>

Why does this stop? Each round either widens at least one jump or changes
nothing, and a jump that has widened never shrinks back. With n jumps, there
can be at most n rounds that widen something, so relaxation ends after at
most n + 1 rounds. Starting every jump short and only growing is what makes
that argument work.

AArch64 has no second size for a given branch: every form is 4 bytes, and the
field width decides the reach. The analogue of relaxation there is the
rewrite from the veneer check: when a `b.cond`, `cbz` or `tbz` might not
reach, replace it with the opposite test over an unconditional `b`. The
decision has the same circular shape, because the rewrite adds 4 bytes and
can push another branch out of range, and the same grow-only loop settles
it. With ±1 MiB for `b.cond` you will rarely need it, but `tbz`'s ±32 KiB is
within reach of a large unrolled kernel.

??? check "In round 2 of the example, jump 0 had to widen although its own target and its own size had not changed since round 1. What changed?"

    Jump 1 sits between jump 0 and `Lx`. When jump 1 grew from 2 to 6
    bytes, every byte after it moved 4 bytes later, `Lx` included. Jump 0's
    distance grew from 126 to 130 bytes, past the 127 a signed byte can
    hold. Sizes of instructions in between are part of every distance, which
    is why relaxation must repeat until nothing changes.

## Reading what the assembler made

Every claim in this chapter can be checked on a real object file, and that is
the habit to build. The LLVM tools read both ELF and Mach-O:

| To see | Command |
| --- | --- |
| Sections, with sizes and types | `llvm-objdump -h file.o`, or `llvm-readobj --section-headers file.o`[^llvm-objdump][^llvm-readobj] |
| The symbol table | `llvm-objdump -t file.o`, or `llvm-readobj --syms file.o` |
| Instructions with their relocations | `llvm-objdump -dr file.o` |
| Relocation records, one per line | `llvm-readobj --relocations file.o`; add `--expand-relocs` for every field |
| An instruction's bytes and fixups, without an object file | `llvm-mc -show-encoding`[^llvm-mc] |
| Bytes back to instructions | `llvm-mc --disassemble`[^llvm-mc] |
| An object file from assembly text | `llvm-mc -filetype=obj -triple=<target>`[^llvm-mc] |

Two practical notes for the owner's machine. The local LLVM 18.1.8 build
was configured for AArch64 only: `llvm-mc -triple=x86_64-linux-gnu` reported
that it could not find the target, and its `llvm-objdump` could not
disassemble x86-64 objects. Apple clang 21's integrated assembler did accept
`-target x86_64-linux-gnu` and `-target x86_64-apple-macos`, and
`/usr/bin/objdump`, Apple's build of `llvm-objdump`, disassembled the
results. That is how the x86-64 rows above were checked.

The second note is about trust. `llvm-mc` is an oracle: an independent,
widely used implementation you can compare your own output with. When your
encoder and `llvm-mc` disagree about a word, one of them is wrong, and it is
usually the new one. [E4](e4-testing-backends.md) turns this into a testing
method for a whole back end.

## For Vortex

!!! vortex "Exercise"

    **Check the object files your back end already produces, then encode
    its branches yourself.** Your B1 back end hands assembly text to the
    system assembler. Keep that path, and add two tests beside it.

    1. **An object-file inspection test.** Compile a small Vortex program
       that calls at least two runtime functions (the `print` from
       [stage 6](../compiler/guide/stage-6-first-machine-code.md#the-small-runtime)
       and the runtime error from
       [stage 9](../compiler/guide/stage-9-runtime-safety.md#stopping-with-a-clear-runtime-error)),
       assemble it, and read the object with `llvm-readobj` or
       `llvm-objdump`. Check that:

        - every runtime function your compiler calls appears as an
          undefined symbol, spelled with the platform's prefix;
        - each call site carries a call relocation of the kind in this
          chapter's table, against that symbol;
        - each Vortex function is a defined symbol in the text section,
          global only if something outside the file must call it;
        - any data your back end emits (text for a message, a table of
          constants) sits in a data or read-only section, and the text
          section holds only instructions.

    2. **An encoder for your branches, checked against the assembler.** In
       your compiler, encode the branch forms your B1 templates emit on
       AArch64 (`b`, `bl`, `b.cond`, `cbz` and `cbnz`, if you use them) into
       32-bit words, with label fixups inside a function and a record for
       every call whose target is outside it. For every function in your
       existing test suite, compare your words at each branch with the words
       the system assembler produced for the same text, and compare your
       list of outside calls with the object file's call relocations.

    **Not yet.** Do not write an ELF or Mach-O file writer, a linker or
    relocation processing: [B4](b4-linking-and-loading.md) covers linking,
    and [D2](d2-jit.md) is where your own bytes first go straight into
    memory. Do not encode non-branch instructions yet; the system assembler
    still produces those bytes. Do not implement branch relaxation unless a
    test program needs it, and if one does, add a check that fails
    when a branch is out of reach instead of wrapping silently.

    **Done when** both tests pass on your whole suite on macOS arm64 (and on
    Linux arm64, if your CI runs there), and each fails with a clear message
    for a deliberate, temporary breakage: a runtime call emitted under a
    misspelled name, which must show up as an unexpected undefined symbol;
    and the imm19 field shifted into bits 22 to 4 instead of 23 to 5, which
    must show up as the first mismatching word and its address.

    Keep a note of the numbers from your own compiler, with its version and
    the date:

    | Program | Branch words checked | Mismatches | Outside calls | Relocations in the object |
    | --- | --- | --- | --- | --- |
    | Stage 6's first program | | | | |
    | The stage 10 matmul kernel | | | | |

## Key ideas

!!! recap "You can now answer"

    - **What makes an object file different from a program?** Some of its bytes are unfinished: it may use names defined elsewhere, and it has no final addresses, so it carries relocations for the linker.
    - **What does a relocation record say?** Which bytes to patch (offset), how (kind), with whose address (symbol) and with what constant (addend), as in `S + A - P` for a call.
    - **Why does a same-file global still need a relocation on AArch64?** It is in another section, and only the linker decides how far apart the sections are.
    - **Why does a zero-filled array cost no space in the object file?** Its section (`.bss`, `SHT_NOBITS`, or a Mach-O zero-fill section) records only a size, and the system zeroes the memory when the program starts.
    - **What is the difference between a fixup and a relocation?** Both are holes; the assembler fills a fixup itself when it can compute the value, and turns only the rest into relocations.
    - **How do assemblers handle a label used before it is defined?** Either two passes, the first only recording label addresses, or one pass that leaves holes and patches them from a list.
    - **Why must relaxation start small and only grow?** Growing is what guarantees it stops; widening one jump can push another out of reach, so the loop repeats until nothing changes.

## Where this comes back

!!! next "You will use this again in"

    - [B4. Linking and loading](b4-linking-and-loading.md): *undefined symbols*, *relocations applied by hand*, *weak definitions*, *veneers and thunks*
    - [D1. Debug information](d1-debug-info.md): *sections that hold tables rather than code*, *relocations in debug data*
    - [D2. JIT compilation](d2-jit.md): *encoding instruction words yourself*, *the 26-bit branch field*
    - [E3. LLVM's allocator, scheduler and MC layer](e3-llvm-allocator-scheduler-mc.md): *fixups*, *relaxation*, *the instruction encoder*
    - [E4. Testing back ends](e4-testing-backends.md): *`llvm-mc` as an oracle*

## Sources and further reading

Salomon's book is the clearest account of how an assembler works inside, and
it is free. For the formats, read the gABI chapter on object files first,
then Apple's headers side by side with it.

[^gabi-intro]: *System V Application Binary Interface*, draft of 10 June 2013, chapter 4, "Introduction": relocatable, executable and shared object files, and the linking and execution views of one file. <https://www.sco.com/developers/gabi/latest/ch4.intro.html>
[^gabi-sections]: *System V Application Binary Interface*, draft of 10 June 2013, chapter 4, "Sections": the section header, `SHT_NOBITS`, and the special sections `.bss` and `.data`. <https://www.sco.com/developers/gabi/latest/ch4.sheader.html>
[^gabi-symtab]: *System V Application Binary Interface*, draft of 10 June 2013, chapter 4, "Symbol Table": `SHN_UNDEF`, the bindings `STB_LOCAL`, `STB_GLOBAL` and `STB_WEAK`, and `STT_SECTION`. <https://www.sco.com/developers/gabi/latest/ch4.symtab.html>
[^gabi-reloc]: *System V Application Binary Interface*, draft of 10 June 2013, chapter 4, "Relocation": `Elf64_Rel` and `Elf64_Rela`, and explicit and implicit addends. <https://www.sco.com/developers/gabi/latest/ch4.reloc.html>
[^gabi-pheader]: *System V Application Binary Interface*, draft of 10 June 2013, chapter 5, "Program Header": program headers are meaningful only for executables and shared objects. <https://www.sco.com/developers/gabi/latest/ch5.pheader.html>
[^xnu-loader]: Apple, XNU source, `EXTERNAL_HEADERS/mach-o/loader.h`: the Mach-O header, load commands, segment and section structures, the `MH_OBJECT` layout with all sections in one unnamed segment, and `S_ZEROFILL`. <https://github.com/apple-oss-distributions/xnu/blob/main/EXTERNAL_HEADERS/mach-o/loader.h>
[^xnu-nlist]: Apple, XNU source, `EXTERNAL_HEADERS/mach-o/nlist.h`: the Mach-O symbol table entry, `N_UNDF`, `N_EXT` and `N_WEAK_DEF`. <https://github.com/apple-oss-distributions/xnu/blob/main/EXTERNAL_HEADERS/mach-o/nlist.h>
[^xnu-arm64-reloc]: Apple, XNU source, `EXTERNAL_HEADERS/mach-o/arm64/reloc.h`: the arm64 relocation types, including `ARM64_RELOC_BRANCH26`, `ARM64_RELOC_PAGE21`, `ARM64_RELOC_PAGEOFF12` and `ARM64_RELOC_ADDEND`. <https://github.com/apple-oss-distributions/xnu/blob/main/EXTERNAL_HEADERS/mach-o/arm64/reloc.h>
[^xnu-x86-reloc]: Apple, XNU source, `EXTERNAL_HEADERS/mach-o/x86_64/reloc.h`: the x86-64 relocation types `X86_64_RELOC_BRANCH` and `X86_64_RELOC_SIGNED`, and the addend stored in the instruction. <https://github.com/apple-oss-distributions/xnu/blob/main/EXTERNAL_HEADERS/mach-o/x86_64/reloc.h>
[^macho-topics]: Apple, "Mach-O Programming Topics", Apple Developer Documentation (archived, last updated 2009-02-04, marked as no longer updated). <https://developer.apple.com/library/archive/documentation/DeveloperTools/Conceptual/MachOTopics/0-Introduction/introduction.html>
[^aaelf64]: Arm, "ELF for the Arm 64-bit Architecture (AArch64)" (AAELF64), release 2025Q4: weak definitions, `REL` and `RELA` addends, the relocation tables with their formulas and range checks, and the rules for veneers. <https://github.com/ARM-software/abi-aa/blob/main/aaelf64/aaelf64.rst>
[^aapcs64]: Arm, "Procedure Call Standard for the Arm 64-bit Architecture (AArch64)" (AAPCS64), release 2025Q4, "Use of IP0 and IP1 by the linker". <https://github.com/ARM-software/abi-aa/blob/main/aapcs64/aapcs64.rst>
[^psabi]: *System V Application Binary Interface, AMD64 Architecture Processor Supplement*, chapter "Object Files", "Relocation Types": `Elf64_Rela` only, and `R_X86_64_PC32` and `R_X86_64_PLT32`. <https://gitlab.com/x86-psABIs/x86-64-ABI>
[^a64-b]: Arm, "Arm A-profile A64 Instruction Set Architecture" (DDI 0602, 2026-06), "B", branch. <https://developer.arm.com/documentation/ddi0602/2026-06/Base-Instructions/B--Branch->
[^a64-cbz]: Arm, DDI 0602 (2026-06), "CBZ". <https://developer.arm.com/documentation/ddi0602/2026-06/Base-Instructions/CBZ--Compare-and-branch-on-zero->
[^a64-adrp]: Arm, DDI 0602 (2026-06), "ADRP". <https://developer.arm.com/documentation/ddi0602/2026-06/Base-Instructions/ADRP--Form-PC-relative-address-to-4KB-page->
[^hellosilicon]: HelloSilicon, GitHub repository `below/HelloSilicon`: `@PAGE` and `@PAGEOFF`, and the leading underscore on Apple platforms. <https://github.com/below/HelloSilicon>
[^salomon]: David Salomon, *Assemblers and Loaders*, Ellis Horwood, 1993, now free from the author: chapter 1, sections 1.2, "The Two-Pass Assembler", and 1.3, "The One-Pass Assembler", on the location counter and the future symbol problem. <https://www.davidsalomon.name/assem.advertis/asl.pdf>
[^lattner-mc]: Chris Lattner, "Intro to the LLVM MC Project", LLVM Project Blog, 9 April 2010: the instruction encoder, fixups, relaxation in the assembler backend, and the observation that the external assembler took about 20% of Clang's compile time for C code at `-O0 -g`. <https://blog.llvm.org/2010/04/intro-to-llvm-mc-project.html>
[^llvm-mc]: LLVM Project, `llvm-mc` command guide: `--show-encoding`, `--disassemble` and `--filetype`. <https://llvm.org/docs/CommandGuide/llvm-mc.html>
[^llvm-readobj]: LLVM Project, `llvm-readobj` command guide. <https://llvm.org/docs/CommandGuide/llvm-readobj.html>
[^llvm-objdump]: LLVM Project, `llvm-objdump` command guide. <https://llvm.org/docs/CommandGuide/llvm-objdump.html>

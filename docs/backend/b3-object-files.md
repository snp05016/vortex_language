# B3. Object files and assemblers

<p class="page-intro">What an assembler turns text into before a linker can touch it: sections, symbols and relocations, and the two-pass process that fills in what a single pass cannot yet know.</p>

<p class="vx-meta" markdown="1">Level: Intermediate · Reading time: about 35 minutes · Builds on: [B1. The simplest back end that works](b1-simplest-backend.md), [B2. A second target: x86-64](b2-x86-64.md)</p>

???+ remember "Before you start, remember"

    ??? question "What does a linker fill in that an object file leaves open?"

        The holes for names defined somewhere else. An object file can say
        "call the thing named `print`" without containing `print`; the linker
        joins it with the code that defines that name.

        Introduced in [6. The first machine code](../compiler/guide/stage-6-first-machine-code.md#from-object-file-to-executable).

    ??? question "What does a label do in AArch64 assembly, and does the assembler emit any bytes for it?"

        It names an address so other lines can refer to it. The assembler
        emits no bytes of its own for a label; it only records where the
        next real line will land.

        Introduced in [A2. Reading and writing AArch64 assembly](a2-aarch64-assembly.md).

    ??? question "What is the difference between a directive like `.p2align` and an instruction like `add`?"

        A directive is an order to the assembler about how to lay out the
        file (align here, start a new section, make this name visible
        outside the file). An instruction is a line the processor itself
        will execute.

        Introduced in [A2. Reading and writing AArch64 assembly](a2-aarch64-assembly.md).

    ??? question "Why does `adrp` split a global's address into a page and an offset?"

        Because AArch64 has no instruction that loads a 64-bit address as a
        single immediate; every A64 instruction is one 32-bit word, too
        small to hold one. `adrp` supplies the page and a following `add` or
        load supplies the low 12 bits.

        Introduced in [A2. Reading and writing AArch64 assembly](a2-aarch64-assembly.md).

!!! goals "In this chapter"

    - Explain why an assembler cannot always fill in an instruction's operand the moment it reads that line.
    - Recognize a defined symbol, an undefined symbol and a relocation in an object file, in both ELF and Mach-O terms.
    - Read the verified relocation names for a call and a global-address load on AArch64 and x86-64, on both operating systems this book targets.
    - Trace, by hand, how a two-pass assembler resolves a branch to a label it has not seen yet.
    - Name the tools that show what is really inside an object file, and use one of them to check your own work.

## From two lines of assembly to an object file

[B1](b1-simplest-backend.md) and [B2](b2-x86-64.md) leave off with assembly
text: readable lines like the ones in [A2](a2-aarch64-assembly.md), printed by
your own back end. Text is not a program yet. Somewhere between here and a
running binary, something has to turn `bl vx_log_i32` into the 32-bit word
the processor actually fetches, and it has to do that without knowing, at the
moment it reads that line, where `vx_log_i32` will end up living. This
chapter is about that something: the **assembler**, the program that reads
assembly text and writes an **object file**, a file of machine code that is
not yet a complete program because it may still use names whose code lives
somewhere else.[^stage6-vocab]

Take a function that does two ordinary things: it calls a function this file
does not define, and it reads a global array this file does not initialize
here either.

--8<-- "includes/examples/backend/b3-object-files/relocations.s.md"

Every other instruction in that function is complete the moment the
assembler reads it: `ldr w0, [x1]` means the same 32 bits no matter what else
is in the file. Three instructions are not complete. `bl _vx_log_i32` needs
the address of a function that might be defined in a different file entirely,
possibly one that has not even been compiled yet. `adrp x1, _counts@PAGE` and
`add x1, x1, _counts@PAGEOFF` need the address of `_counts`, which this
assembler call does know (it is defined a few lines down, in the same file),
but AArch64 has no instruction that carries a full 64-bit address as one
immediate, so the address has to travel in two pieces, each requiring the
final address to compute.[^apple-arm64]

An assembler's answer to "I do not know this value yet" is not to stop. It
writes the instruction with the value it does know how to fill in later, a
placeholder, and it writes down, in a separate table inside the object file,
exactly which bytes are wrong and what they need to become correct: the name
of the symbol involved, which bytes to patch, and how to compute the patch.
That table entry is a **relocation**. The rest of this chapter is about the
three ideas that make relocations possible: where bytes live (sections), what
a name refers to (symbols), and how the placeholder and its fix are recorded
(relocations themselves), then how the assembler produces all of this in two
passes over the input instead of one.

## Sections and segments: where bytes live

An object file is not one undifferentiated blob of bytes. It is split into
named regions, because code, read-only constants, initialized data and
zero-initialized data behave differently at load time and deserve different
treatment. ELF, the format Linux uses, calls these regions **sections**, and
groups them under **program headers** that tell the loader which parts to map
into memory and with what permissions.[^gabi] Mach-O, the format macOS uses,
groups sections under **segments**, described by **load commands** in the
file's header; the two segments almost every program has are `__TEXT`, for
code and read-only data, and `__DATA`, for everything else.[^macho-headers]
The names differ; the job is the same, and it is the same job an assembler
must do for either format: decide, for every byte it emits, which section
that byte belongs to.

The decision usually follows directly from what the byte is for. Code goes in
a text section. A global variable that starts with real, non-zero content
goes in a data section, because the file has to carry those bytes. A global
variable whose initial value is entirely zero can go in a special
zero-fill section instead (`.bss` in ELF, or a zero-fill section under
`__DATA` in Mach-O), which costs nothing in the file at all: the section
records only a size, and the operating system's loader zero-fills that many
bytes when the program starts, rather than the object file storing that many
zero bytes on disk.[^gabi] A large global array of default-valued elements is
exactly the situation this section exists for.

<figure class="vx-figure">
<svg viewBox="0 0 760 400" role="img" aria-label="The layout of an object file, and how two relocations point at the bytes they will patch" aria-describedby="b3-anatomy-desc">
<title id="b3-anatomy-title">The layout of an object file, and how two relocations point at the bytes they will patch</title>
<desc id="b3-anatomy-desc">A header box sits above a text section box, which contains two highlighted four-byte instructions: a bl instruction and an adrp/add pair, both shown with question marks in place of the address bits the assembler could not fill in. Below that is a data section box holding one zero word for the global "counts". Below that is a symbol table listing three rows: event count, defined, in the data section; bump and report, defined, in the text section; report, undefined, no section. At the bottom, a relocation table lists two rows, one naming report and one naming counts. A curved arrow rises from the report relocation row to the bl instruction's question marks, and a second curved arrow rises from the counts relocation row to the adrp and add question marks, showing which bytes each relocation will patch once the linker knows both addresses.</desc>

<rect class="vx-box" x="20" y="16" width="720" height="36" rx="4"/>
<text class="vx-text" x="380" y="39" text-anchor="middle">header: format, target, offsets to everything below</text>

<rect class="vx-box" x="20" y="70" width="720" height="70" rx="4"/>
<text class="vx-text-muted" x="32" y="90">.text / __TEXT,__text</text>
<rect class="vx-box-accent" x="200" y="98" width="130" height="34" rx="3"/>
<text class="vx-mono" x="265" y="120" text-anchor="middle">bl ????</text>
<rect class="vx-box-accent" x="360" y="98" width="200" height="34" rx="3"/>
<text class="vx-mono" x="460" y="120" text-anchor="middle">adrp / add ????</text>

<rect class="vx-box" x="20" y="158" width="720" height="46" rx="4"/>
<text class="vx-text-muted" x="32" y="178">.data / __DATA,__data</text>
<rect class="vx-box" x="200" y="164" width="90" height="30" rx="3"/>
<text class="vx-mono" x="245" y="184" text-anchor="middle">counts: 0</text>

<rect class="vx-box" x="20" y="222" width="720" height="90" rx="4"/>
<text class="vx-text-muted" x="32" y="240">symbol table</text>
<text class="vx-mono" x="40" y="262">event_count</text>
<text class="vx-text-muted" x="220" y="262">defined, .data</text>
<text class="vx-mono" x="40" y="282">bump_and_report</text>
<text class="vx-text-muted" x="220" y="282">defined, .text</text>
<text class="vx-mono" x="40" y="302">report</text>
<text class="vx-text-muted" x="220" y="302">undefined, no section</text>

<rect class="vx-box" x="20" y="330" width="720" height="56" rx="4"/>
<text class="vx-text-muted" x="32" y="348">relocation table</text>
<text class="vx-mono" x="40" y="370">.text+0  -&gt; report</text>
<text class="vx-mono" x="360" y="370">.text+4  -&gt; counts (event_count's neighbor in this example)</text>

<path class="vx-flow" d="M 100 330 C 100 250, 180 150, 260 132"/>
<polygon class="vx-arrowhead" points="255,124 265,128 259,138"/>
<path class="vx-flow" d="M 400 330 C 400 250, 420 150, 455 132"/>
<polygon class="vx-arrowhead" points="450,124 460,128 454,138"/>
</svg>
<figcaption>Every box above the relocation table is data the assembler already wrote with confidence. The relocation table is a promise about the two highlighted spots: once the linker knows where <code>report</code> and <code>counts</code> finally live, it goes back and overwrites exactly those bytes. Nothing else in the file changes.</figcaption>
</figure>

??? check "Which section does an array of default-valued elements belong in, and why does that section cost nothing in the object file?"

    A zero-fill section: `.bss` in ELF, or a zero-fill section under `__DATA`
    in Mach-O. The section records only how many bytes the array needs; the
    loader zero-fills that much memory when the program starts, so the file
    on disk never has to store the zero bytes themselves.

## Symbols: names the linker must match up

A **symbol** is a name an object file records, together with what it knows
about that name. If the file itself defines the name, the symbol is
**defined** and points at an offset into one of the file's own sections:
"`bump_and_report` starts at byte 0 of `.text`." If the file only uses the
name, expecting some other file to define it, the symbol is **undefined**:
the entry exists, but its address is left for the linker to find. A third
kind, a **weak** symbol, is defined but marked as a definition the linker may
silently discard in favor of a different definition of the same name found
elsewhere, used for things like inline function bodies that several files
might emit identically.

The toy module below has exactly one function and one global, which is
enough to show all three roles a symbol can play in a single small symbol
table.

--8<-- "includes/examples/backend/b3-object-files/object_model.ll.md"

Once this module is turned into an object file, its symbol table needs three
entries. `@event_count` is defined, in a data section, because it has an
initial value. `@bump_and_report` is defined, in the text section, because it
is code this file actually contains. `@report` is undefined: nothing in this
file says what `@report` does, only that something, somewhere, must.

That undefined entry is not a gap the assembler failed to fill; it is the
whole point of keeping an undefined symbol at all. The call to `@report`
still has to become a real `bl` (or `call`) instruction with real bits, and
those bits still have to be patched once an address exists. Without an
undefined symbol recording the name `report`, the assembler would have
nowhere to write down whose address the patch needs, and the linker would
have no way to find it. This is exactly `_vx_log_i32`'s situation in
`relocations.s`: the compiler that eventually calls a runtime function it did
not itself define is relying on this same mechanism.

??? check "Why must a called function that this file does not define get an undefined symbol table entry, rather than nothing at all?"

    The call instruction's bytes still need to be patched with a real
    address once one exists. The undefined symbol is what tells the linker
    which name that patch is for; without it, the relocation that references
    the call would have no name to resolve.

## Relocations: the holes themselves

A **relocation** is the object file's record of one unfinished patch: which
bytes are wrong, which symbol will fix them, and how to combine the symbol's
final address with whatever is already sitting in those bytes. Different
instruction forms need different kinds of patch, so both ELF and Mach-O
define a set of named relocation kinds, one per shape of hole. The two
instruction forms in `relocations.s`, a direct call and a page-relative
address, are common enough that their relocation names are worth knowing by
sight, on every combination this book's back ends target:

| What needs patching | ELF AArch64 | Mach-O arm64 | ELF x86-64 | Mach-O x86-64 |
| --- | --- | --- | --- | --- |
| A direct call or branch | `R_AARCH64_CALL26` | `ARM64_RELOC_BRANCH26` | `R_X86_64_PLT32` | `X86_64_RELOC_BRANCH` |
| A global's page address | `R_AARCH64_ADR_PREL_PG_HI21` | `ARM64_RELOC_PAGE21` | `R_X86_64_PC32` (RIP-relative `lea`) | `X86_64_RELOC_SIGNED` |
| A global's low 12 bits | `R_AARCH64_ADD_ABS_LO12_NC` | `ARM64_RELOC_PAGEOFF12` | N/A (folded into the PC32 form above) | N/A |

Sources: the AArch64 names are defined in AAELF64;[^aaelf64] the Mach-O arm64
names are defined in XNU's `arm64/reloc.h` and mirrored in LLVM's
`BinaryFormat/MachO.h`;[^macho-headers][^llvm-macho] the x86-64 ELF names are
defined in the System V AMD64 psABI;[^psabi] x86-64 has no separate low-12
relocation because its addressing already reaches a whole 32-bit
PC-relative displacement in one instruction, unlike AArch64's fixed 32-bit
instruction words. Apple's own Mach-O reference prose is archived and marked
"no longer being updated"; the header files are the current source for
these names.[^macho-topics]

A `26` or `32` in a relocation's name is usually the width, in bits, of the
value the relocation patches, and that width sets a hard limit on how far the
patched instruction can reach. AArch64's unconditional branch instructions
encode a signed word offset in 26 bits, which reaches ±128 MiB; its
conditional branches use 19 bits, ±1 MiB; and `TBZ`/`TBNZ`, which test one bit
and branch, use only 14, ±32 KiB.[^aaelf64] A call outside a branch's range
cannot be assembled directly at all. The usual fix is a **veneer**: a short
stub the linker inserts near the call, close enough to reach with the direct
branch's limited range, which then jumps the rest of the way with an
instruction that has no such limit. AArch64 veneers may clobber `x16` and
`x17`, the two registers AAPCS64 reserves as intra-procedure-call temporaries
for exactly this purpose, so code must not treat those two registers as safe
across a call whose target is unknown at compile time.[^aapcs64-b3]

x86-64 has an analogous limit for its short conditional jump form (`jcc
rel8`, one signed byte, ±128 bytes) against its near form (`jcc rel32`, four
signed bytes); an assembler that wants to use the short form when it can has
to be prepared to widen a jump to the near form if, after laying out the rest
of the file, the target turns out to be too far away after all.

??? check "Why can a `bl` to a function 300 MiB away not be assembled directly on AArch64, no matter how the linker eventually places things?"

    `BL`'s encoding holds a signed 26-bit word offset, which reaches at most
    ±128 MiB. 300 MiB is outside that range regardless of layout, so the
    linker must insert a veneer, a short stub within range of the `bl` that
    then reaches the real target with an instruction that is not limited the
    same way.

## Two passes: how an assembler resolves a forward reference

Reading `relocations.s` line by line, an assembler meets `_counts` for the
first time inside `adrp x1, _counts@PAGE`, several lines before the line that
actually defines it (`_counts:`). If the assembler tried to finish each line
the moment it read it, it would have no address for `_counts` yet: this is
called a **forward reference**, a use of a name that appears before that
name's definition. The fix used since the earliest assemblers is to make two
passes over the same input:[^lattner-mc]

1. **Pass 1: measure and record.** Walk every line, computing how many bytes
   each instruction or directive will occupy and adding that many to a
   running address. Whenever a label is seen, record the address it names in
   a table. Nothing is emitted yet.
2. **Pass 2: emit.** Walk the lines again. This time every label in the
   program has already been recorded, whether it appeared before or after
   each use, so every operand that names a label can be computed and
   written for real (an internal one, resolved on the spot) or turned into a
   relocation (an external one, left for the linker).

The example below builds a label table this way for a program with one
forward branch and one backward branch, over a made-up three-instruction
machine chosen specifically so the fixup idea is visible without any real
AArch64 or x86-64 encoding in the way.

--8<-- "includes/examples/backend/b3-object-files/two_pass_fixups.cpp.md"

It prints:

```text
--8<-- "examples/backend/b3-object-files/two_pass_fixups.expected"
```

The backward jump, `JMP start`, targets a label the assembler already passed
by the time it reaches this line; a one-pass assembler could resolve it
immediately, the moment it is read. The forward jump, `JMP skip`, targets a
label three lines further down, which a one-pass assembler simply does not
have yet. Making the label table in a first pass, before emitting anything,
means the second pass never has to tell the two cases apart: by the time
pass 2 runs, every label, forward or backward, is already in the table.
Real assemblers refine this scheme considerably, for instance by folding the
two passes into one using per-instruction "fragments" that can be resized and
relocated after the fact rather than measured twice,[^lattner-mc] but the
underlying problem, and its solution, a table built before any encoding
depends on it, are the same.

??? check "A one-pass assembler that never rewrites bytes it already wrote could still resolve `JMP start` immediately. Why can it not do the same for `JMP skip`?"

    By the time it reaches `JMP start`, `start`'s address is already known:
    the assembler passed that label earlier in the same walk through the
    file. `skip` is defined later; at the point `JMP skip` is read, no
    address has been assigned to `skip` yet, so there is nothing to resolve
    it against without a second pass or an explicit patch list.

## Reading what the assembler made

Everything in this chapter can be checked against a real object file, not
only reasoned about. `llvm-readobj` and `llvm-objdump` print an object file's
sections, symbols and relocations on both ELF and Mach-O;[^readobj-objdump]
`readelf` and `otool` are the platform-native equivalents this book's
research assumed alongside them. `llvm-mc --show-encoding` prints the bytes
an instruction assembles to without producing a whole object file, and
`llvm-mc --disassemble` goes the other way, from bytes back to text; both are
useful as an oracle when checking a hand-written encoder against a trusted
one, a technique [E4](e4-testing-backends.md) returns to for testing an
entire back end.[^llvm-mc-doc] This chapter does not print sample tool output
of its own: the exact layout printed by these tools depends on the tool's
version and the platform's own conventions, and showing invented output
would be worse than showing none. Running one of them on your own generated
object files is the reliable way to see the concrete shape of what this
chapter has described in general terms.

## For Vortex

!!! vortex "Exercise"

    **Look inside your own back end's object files.** Whichever way your
    back end currently reaches an object file, whether it hands assembly
    text to the system assembler as part of [stage 6](../compiler/guide/stage-6-first-machine-code.md#choosing-a-back-end),
    or does something else, write a test that compiles a small Vortex
    program with at least one runtime call (the kind [stage 9](../compiler/guide/stage-9-runtime-safety.md)
    introduces for a failed bounds check, or the call [stage 6](../compiler/guide/stage-6-first-machine-code.md)
    uses for `print`) and at least one global your back end emits, then
    inspects the resulting object file with `llvm-readobj` or the
    platform's native tool and checks two things:

    1. **Every runtime call your compiler emits shows up as an undefined
       symbol**, by exactly the name your compiler used for it, with a
       relocation referencing that symbol at the call site.
    2. **Every global your back end emits lands in the section its content
       requires**: initialized data in a data section, and, if your back end
       ever emits a global with no meaningful initial content, that global
       in a zero-fill section rather than as literal zero bytes on disk.

    Then run a **differential check**: assemble one of the small `.s` files
    from `examples/backend/` (or a short one you write by hand, in the style
    of `relocations.s`) with the system assembler, and compare its symbol
    table and relocation list, name by name and kind by kind, against what
    your compiler's own output produces for an equivalent hand-written
    Vortex function. They do not need to be byte-identical; they need to
    agree on which names are undefined and which relocation kind each call
    and each global address gets.

    **Not yet.** Do not write your own ELF or Mach-O writer. Producing
    object files without shelling out to the system assembler removes a real
    cost (Chris Lattner reports that, before LLVM's MC layer existed, the
    external assembler could cost as much as roughly 20% of an `-O0` C
    compile's total time[^lattner-mc]), but it is a project of its own, and
    nothing in this exercise depends on it. Treat the system assembler as a
    trusted black box for now; [B4](b4-linking-and-loading.md) and
    [D2](d2-jit.md) are where writing your own bytes directly starts to pay
    for itself.

    **Done when** the inspection test passes for a program with at least one
    call and one global, and fails (with a clear message, not a crash) if
    you temporarily rename the runtime call your compiler emits without
    updating the corresponding relocation's target name.

## Key ideas

!!! recap "You can now answer"

    - **What is an object file, in one sentence?** Machine code that is not yet a complete program, because it may use names, such as a runtime call, whose code lives somewhere else.
    - **What is the difference between a defined and an undefined symbol?** A defined symbol points at an offset inside one of the file's own sections; an undefined symbol is a name the file uses but expects another file to define.
    - **What is a relocation?** A record of one unfinished patch: which bytes are wrong, which symbol will fix them, and how to combine that symbol's final address with what is already in those bytes.
    - **Why does a zero-fill section cost nothing in the file on disk?** It records only a size; the loader, not the file, is responsible for producing that many zero bytes at load time.
    - **Why does an assembler need two passes (or an equivalent trick) rather than one?** A forward reference, a name used before it is defined in the file, has no address yet on a single pass through the input; a first pass that only records label addresses gives the second pass everything it needs.
    - **What limits how far a direct branch can reach on AArch64?** The width of the signed offset its encoding carries: 26 bits (±128 MiB) for `B`/`BL`, 19 bits (±1 MiB) for `B.cond`, 14 bits (±32 KiB) for `TBZ`/`TBNZ`; beyond that, a linker-inserted veneer is required.

## Where this comes back

!!! next "You will use this again in"

    - [B4. Linking and loading](b4-linking-and-loading.md): *undefined symbols*, *the relocation table*, *sections becoming segments at load time*
    - [D1. Debug information](d1-debug-info.md): *extra sections that carry no code, only tables for a debugger*
    - [D2. JIT compilation](d2-jit.md): *skipping the object file and linker entirely, and what that costs you instead*
    - [E3. LLVM's allocator, scheduler and MC layer](e3-llvm-allocator-scheduler-mc.md): *MCInst, MCStreamer, and fixups resolved without two full passes*
    - [E4. Testing back ends](e4-testing-backends.md): *`llvm-mc` as an oracle*, *differential testing against a trusted assembler*

## Sources and further reading

[^stage6-vocab]: Vortex documentation, [6. The first machine code](../compiler/guide/stage-6-first-machine-code.md), "Words for this stage": the definitions of object file, linker and executable used throughout this chapter. <../compiler/guide/stage-6-first-machine-code.md>
[^apple-arm64]: Apple, "Writing ARM64 code for Apple platforms", Apple Developer Documentation: the `adrp`/`@PAGEOFF` idiom for reaching a global's address in two pieces. <https://developer.apple.com/documentation/xcode/writing-arm64-code-for-apple-platforms>
[^gabi]: *System V Application Binary Interface*, gABI, chapter 4, "Object Files": sections, program headers, and the special sections including `.bss`. <https://gabi.xinuos.com/> ; 2013 draft <https://www.sco.com/developers/gabi/latest/contents.html>
[^macho-headers]: Apple, XNU source, `EXTERNAL_HEADERS/mach-o/loader.h`: the Mach-O header, load commands and segment/section structures. <https://github.com/apple-oss-distributions/xnu/blob/main/EXTERNAL_HEADERS/mach-o/loader.h>
[^macho-topics]: Apple, "Mach-O Programming Topics", Apple Developer Documentation (archived, last updated 2009-02-04, marked "no longer being updated"): cited here only to note that the header files, not this document, are the current reference. <https://developer.apple.com/library/archive/documentation/DeveloperTools/Conceptual/MachOTopics/0-Introduction/introduction.html>
[^aaelf64]: Arm, "ELF for the Arm 64-bit Architecture (AArch64)" (AAELF64), release 2025Q4: the relocation type names and the range checks for branch relocations. <https://github.com/ARM-software/abi-aa/blob/main/aaelf64/aaelf64.rst>
[^llvm-macho]: LLVM Project, `llvm/include/llvm/BinaryFormat/MachO.h`: the Mach-O relocation type enumerations mirrored from Apple's headers. <https://github.com/llvm/llvm-project/blob/main/llvm/include/llvm/BinaryFormat/MachO.h>
[^psabi]: *System V Application Binary Interface, AMD64 Architecture Processor Supplement*: the x86-64 ELF relocation type names. <https://gitlab.com/x86-psABIs/x86-64-ABI>
[^aapcs64-b3]: Arm, "Procedure Call Standard for the Arm 64-bit Architecture (AArch64)" (AAPCS64), release 2025Q4: `x16`/`x17` (IP0/IP1) as the registers a linker veneer may use. <https://github.com/ARM-software/abi-aa/blob/main/aapcs64/aapcs64.rst>
[^lattner-mc]: Chris Lattner, "Intro to the LLVM MC Project", LLVM Project Blog, 9 April 2010: the two-pass assembler model, its replacement with fragments and relaxation, and the reported cost of an external assembler before MC existed. <https://blog.llvm.org/2010/04/intro-to-llvm-mc-project.html>
[^readobj-objdump]: LLVM Project, `llvm-readobj` and `llvm-objdump` command guides. <https://llvm.org/docs/CommandGuide/llvm-readobj.html> ; <https://llvm.org/docs/CommandGuide/llvm-objdump.html>
[^llvm-mc-doc]: LLVM Project, `llvm-mc` command guide: `--show-encoding` and `--disassemble`. <https://llvm.org/docs/CommandGuide/llvm-mc.html>

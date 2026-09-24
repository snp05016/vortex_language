# D1. Debug information

<p class="page-intro">A compiled function is a run of bytes with no source code left in it. Debug information is a second, parallel encoding, stored beside the machine code, that maps those bytes back to file names, line numbers and stack frames, so a debugger, a profiler or a crash report can speak in the terms the programmer used.</p>

<p class="vx-meta" markdown="1">Level: Advanced · Reading time: about 45 minutes · Builds on: [A5. Stack frames](a5-stack-frames.md), [B3. Object files and assemblers](b3-object-files.md)</p>

???+ remember "Before you start, remember"

    ??? question "What is a frame record, and what must AArch64 code do to walk one?"

        The saved FP and LR of one call, with FP pointing at them. Walking the chain means following each frame's saved FP to the one below it, down to a zero that marks the first call.

        Introduced in [A5. Stack frames](a5-stack-frames.md#key-ideas).

    ??? question "Why must a leaf function on Apple's targets still sometimes build a frame record?"

        It must not, if it truly calls nothing; the exception AAPCS64 and Apple both name is "leaf functions or tail calls," and any function that calls anything else, even once, falls outside it.

        Introduced in [A5. Stack frames](a5-stack-frames.md#key-ideas).

    ??? question "What is a relocation, and what three things does it record?"

        The object file's record of one unfinished patch: which bytes are wrong, which symbol will fix them, and how to combine the symbol's final address with whatever is already sitting in those bytes.

        Introduced in [B3. Object files and assemblers](b3-object-files.md#relocations-the-holes-themselves).

    ??? question "When a Vortex runtime check fails, what exact line does the program write to standard error?"

        `runtime error[<kind>]: <message> at <file>:<line>:<column>`, then the program exits with status 101. The kind names the failed check, such as `bounds` or `stack`.

        Introduced in [stage 9. Runtime safety](../compiler/guide/stage-9-runtime-safety.md#stopping-with-a-clear-runtime-error).

!!! goals "In this chapter"

    - Explain why a line-number table is stored as a short opcode program rather than one row per address.
    - Recognize a debugging information entry (DIE), an abbreviation and the `.debug_info` tree, and explain why many DIEs share one abbreviation code.
    - Explain what call frame information adds beyond the frame record from [A5](a5-stack-frames.md), and why an unwinder needs it at every instruction, not only at a function's first one.
    - Read the difference between Apple's compact unwind encoding and DWARF's CIE/FDE model, and say when a tool falls back from one to the other.

## A run of bytes cannot name itself

Take a function that has already been compiled: a run of machine instructions at some address, with no comment, no variable name and no file name anywhere inside it. Run it under a debugger and stop it mid-call, and the processor can tell you exactly one thing about where you are: the program counter, a number. Everything else a debugger prints, a backtrace with function names, a line highlighted in a source file, a local variable's value read out by name, comes from a second file the compiler also wrote, read alongside the first.

That second file is not a metaphor. On every target Vortex compiles for, it is a set of sections inside the same object file as the code: `.debug_info`, `.debug_line`, `.debug_abbrev`, `.debug_str`, and on Apple's targets a `__compact_unwind` section alongside a `.eh_frame`. **Debug information** is the general name for all of it: data, produced by the compiler, that describes the compiled program in the vocabulary of its source rather than the vocabulary of its instructions. The dominant format for it, and the one every example in this chapter uses, is **DWARF**, a format standardized independently of any one compiler, currently at version 5[^dwarf5].

This chapter covers two of DWARF's jobs, the two the research notes behind it call out as the ones to learn first: mapping an address back to a source line (the **line number table**), and describing a function's tree of debugging information entries (the **DIE tree**, `.debug_info`). It also covers a job that sits next to DWARF rather than inside it on Apple's targets: **call frame information**, the data an unwinder reads to walk a stack it did not build, which is what turns a raw list of return addresses into the named backtrace a debugger prints. A fourth DWARF job, describing exactly where a variable's value lives at each point in a function (`DW_AT_location`), is deliberately out of scope here: it depends on register allocation and spilling in ways [C5](c5-spilling.md) has not yet introduced, and the research notes behind this chapter put it later for the same reason. Line tables and frames come first because a compiler needs them the moment it can produce a stack trace at all; variable locations come once the back end tracks where a value lives well enough to describe it.

## The line number table: an opcode program, not a lookup table

Start with the smallest question a debugger has to answer: given an address, what source line was the compiler translating when it emitted the instruction there? The naive answer is a table with one row per address: 4 bytes of address, a line number, repeat for every instruction in the program. For a program with thousands of instructions, most of which share a line with their neighbors, that table would be almost entirely redundant.

DWARF's answer, walked through in plain language in the DWARF committee's own introductory guide, is a small **state machine**[^eager]. The compiler does not write rows directly; it writes a short program of opcodes that, when run, rebuilds the rows. The machine keeps a handful of registers, the two that matter here are `address` (where the next instruction the debugger should attribute to a source line begins) and `line` (which line in which file that instruction came from), and the opcode program does three things to them: advance `address` forward by some amount, add a signed delta to `line` (which can move it backward as well as forward), and **copy** the current `(address, line)` pair into the table as a finished row. A run of consecutive instructions on the same line costs one `advance_pc` and one `copy`, not one row per instruction.

The following example is a toy model of exactly that machine, small enough to read end to end, built to run and print its own output rather than being taken on faith:

--8<-- "includes/examples/backend/d1-debug-info/line_number_program.cpp.md"

The six rows it prints come from a fabricated loop: line 10 is the function's first statement, line 11 is a loop condition checked twice, line 12 is the loop body, and line 14 runs once the loop is done. Notice that `address` only ever increases, 0x0, 0x8, 0xc, 0x10, 0x18, 0x1c, because instructions are laid out one after another in memory and the compiler never moves one earlier to make room for another. `line`, by contrast, goes 10, 11, 12, 11, 14: it drops back to 11 at the loop's back edge, because the fourth block of instructions came from the same source line as the second. A table indexed only by line number could not represent this; a table built by a state machine that advances `address` and `line` independently represents it for free.

<figure class="vx-figure">
<svg viewBox="0 0 620 300" role="img" aria-labelledby="d1-line-title d1-line-desc">
<title id="d1-line-title">The line-number state machine stepping through line_number_program.cpp's toy opcode stream</title>
<desc id="d1-line-desc">Six opcode groups on the left, each advancing the address register, and usually the line register, before a copy opcode. Each opcode group produces one row on the right: address 0x0 line 10, address 0x8 line 11, address 0xc line 12, address 0x10 line 11 (the loop's back edge, where line falls from 12 back to 11 while address keeps rising), address 0x18 line 14, and address 0x1c marked end_sequence.</desc>
<defs>
<marker id="d1-line-head" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker>
</defs>
<text class="vx-text-muted" x="150" y="18" text-anchor="middle">opcodes run</text>
<text class="vx-text-muted" x="470" y="18" text-anchor="middle">row written</text>
<g>
<rect class="vx-box" x="20" y="30" width="260" height="34" rx="4" style="--vx-i:0;--vx-n:6" /><text class="vx-mono vx-seq" x="30" y="52" style="--vx-i:0;--vx-n:6">line+=9, copy</text>
<line class="vx-line" x1="282" y1="47" x2="336" y2="47" marker-end="url(#d1-line-head)"/>
<rect class="vx-box" x="340" y="30" width="200" height="34" rx="4" style="--vx-i:0;--vx-n:6" /><text class="vx-mono vx-seq" x="350" y="52" style="--vx-i:0;--vx-n:6">0x0, line 10</text>
</g>
<g>
<rect class="vx-box" x="20" y="72" width="260" height="34" rx="4" style="--vx-i:1;--vx-n:6" /><text class="vx-mono vx-seq" x="30" y="94" style="--vx-i:1;--vx-n:6">pc+=8, line+=1, copy</text>
<line class="vx-line" x1="282" y1="89" x2="336" y2="89" marker-end="url(#d1-line-head)"/>
<rect class="vx-box" x="340" y="72" width="200" height="34" rx="4" style="--vx-i:1;--vx-n:6" /><text class="vx-mono vx-seq" x="350" y="94" style="--vx-i:1;--vx-n:6">0x8, line 11</text>
</g>
<g>
<rect class="vx-box" x="20" y="114" width="260" height="34" rx="4" style="--vx-i:2;--vx-n:6" /><text class="vx-mono vx-seq" x="30" y="136" style="--vx-i:2;--vx-n:6">pc+=4, line+=1, copy</text>
<line class="vx-line" x1="282" y1="131" x2="336" y2="131" marker-end="url(#d1-line-head)"/>
<rect class="vx-box" x="340" y="114" width="200" height="34" rx="4" style="--vx-i:2;--vx-n:6" /><text class="vx-mono vx-seq" x="350" y="136" style="--vx-i:2;--vx-n:6">0xc, line 12</text>
</g>
<g>
<rect class="vx-box-accent" x="20" y="156" width="260" height="34" rx="4" style="--vx-i:3;--vx-n:6" /><text class="vx-mono vx-seq" x="30" y="178" style="--vx-i:3;--vx-n:6">pc+=4, line-=1, copy</text>
<line class="vx-line" x1="282" y1="173" x2="336" y2="173" marker-end="url(#d1-line-head)"/>
<rect class="vx-box-accent" x="340" y="156" width="200" height="34" rx="4" style="--vx-i:3;--vx-n:6" /><text class="vx-mono vx-seq" x="350" y="178" style="--vx-i:3;--vx-n:6">0x10, line 11</text>
</g>
<g>
<rect class="vx-box" x="20" y="198" width="260" height="34" rx="4" style="--vx-i:4;--vx-n:6" /><text class="vx-mono vx-seq" x="30" y="220" style="--vx-i:4;--vx-n:6">pc+=8, line+=3, copy</text>
<line class="vx-line" x1="282" y1="215" x2="336" y2="215" marker-end="url(#d1-line-head)"/>
<rect class="vx-box" x="340" y="198" width="200" height="34" rx="4" style="--vx-i:4;--vx-n:6" /><text class="vx-mono vx-seq" x="350" y="220" style="--vx-i:4;--vx-n:6">0x18, line 14</text>
</g>
<g>
<rect class="vx-box-strong" x="20" y="240" width="260" height="34" rx="4" style="--vx-i:5;--vx-n:6" /><text class="vx-mono vx-seq" x="30" y="262" style="--vx-i:5;--vx-n:6">pc+=4, end_sequence</text>
<line class="vx-line" x1="282" y1="257" x2="336" y2="257" marker-end="url(#d1-line-head)"/>
<rect class="vx-box-strong" x="340" y="240" width="200" height="34" rx="4" style="--vx-i:5;--vx-n:6" /><text class="vx-mono vx-seq" x="350" y="262" style="--vx-i:5;--vx-n:6">0x1c, end_seq</text>
</g>
</svg>
<figcaption>Figure 1. The line-number state machine running <code>line_number_program.cpp</code>'s toy opcode stream. The highlighted row is the loop's back edge: <code>address</code> keeps rising while <code>line</code> falls from 12 back to 11. The last row, an <code>end_sequence</code>, closes the range of addresses this table covers; DWARF requires one to end every contiguous run of code.</figcaption>
</figure>

The toy interpreter above proves the idea; the next example shows the real thing. `.file` and `.loc` are assembler directives, understood by both GNU `as` and the LLVM integrated assembler clang uses, that ask the assembler to build an actual DWARF `.debug_line` section as it assembles ordinary instructions; a real compiler emits the same directives automatically as it walks each statement's source position, which LLVM's own guide to generating this kind of information describes end to end[^llvm-sld]:

--8<-- "includes/examples/backend/d1-debug-info/line_table.s.md"

Assembling this file and reading its `.debug_line` section back with `llvm-dwarfdump --debug-line` (Apple clang 21, `llvm-dwarfdump`, arm64-apple-macosx, 2026-09-24) produces a DWARF version 5 line table whose rows are exactly the five `.loc` lines this file wrote, plus one `end_sequence` row the assembler adds on its own at the function's last address:

```text
Address            Line   Column File   ISA Discriminator OpIndex Flags
------------------ ------ ------ ------ --- ------------- ------- -------------
0x0000000000000000     10      5      1   0             0       0  is_stmt
0x0000000000000008     11      9      1   0             0       0  is_stmt
0x0000000000000010     13      5      1   0             0       0  is_stmt
0x0000000000000018     14      9      1   0             0       0  is_stmt
0x0000000000000020     16      5      1   0             0       0  is_stmt
0x0000000000000024     16      5      1   0             0       0  is_stmt end_sequence
```

`clamp`'s branches, `b.ge`, `b.le` and the unconditional `b`s, are exactly what make this more than a straight-line example: the assembler had to place `.loc 1 16 5` once, on the shared exit label `3:`, yet the table above shows that same line and column at two different addresses, 0x20 and 0x24. The first is the real instruction, `ret`; the second is the synthetic `end_sequence` row the assembler always appends to close out the range, at the address one past the function's last byte. This is the same idea as the toy interpreter's row for address 0x1c: an `end_sequence` marks where a contiguous run of code, this function, stops, so a debugger stepping past the function's last instruction does not silently attribute the address after it to line 16 as well.

??? check "Why does the line-number program move the address and the line number with two separate opcodes, rather than writing one finished row for every unique source line up front?"

    Because the two do not move together. Address always increases, since instructions sit one after another in memory, but the same line can be visited from more than one place (a loop's back edge, in Figure 1) or skipped over entirely (an inlined call, a line with no code of its own). A table built line-by-line cannot represent an address that revisits an earlier line; a state machine that advances each register independently, and only writes a row when `copy` runs, represents it directly and, for the common case of many instructions sharing one line, needs far fewer opcodes than one row per instruction would.

## The DIE tree: `.debug_info` and why entries share abbreviations

The line table answers "what line is this address," a question about code. `.debug_info` answers questions about the program's own structure: what functions exist, what parameters and locals each one declares, what types those locals have. It stores that as a tree of **debugging information entries**, or **DIEs**: one DIE for the compile unit at the root, one for each function, one for each of that function's parameters and locals, one for each type. Every DIE has a **tag** naming what kind of thing it describes, `DW_TAG_subprogram` for a function, `DW_TAG_formal_parameter` for a parameter, and a list of **attributes**, name and type and, eventually, location, that describe it.

Writing every DIE's tag and full attribute list out in the file would repeat the same shape constantly: a `DW_TAG_formal_parameter` with a name, a type and a location looks structurally identical to the next parameter that also has a name, a type and a location, even though the actual name and type differ. DWARF factors that shape out once, into a separate `.debug_abbrev` section, and lets many DIEs reuse it: an **abbreviation** records a tag plus the ordered list of (attribute, form) pairs a DIE with that shape carries; a DIE in `.debug_info` then writes only a small abbreviation code, followed by the attribute *values* in the order the abbreviation promised[^dwarf5]. Two DIEs that happen to have the same tag and the same set of attributes present, even with completely different values, share one abbreviation.

The next example builds a tiny DIE tree by hand, for a two-function toy program, and counts how many distinct abbreviations it actually needs:

--8<-- "includes/examples/backend/d1-debug-info/abbrev_dedup.cpp.md"

Eight DIEs, five distinct abbreviations. The compile unit is its own shape. Both functions, `average3` and `clamp`, share one abbreviation, because both are a `DW_TAG_subprogram` with a name, a type and a location, even though their names and low-PC values differ. The two parameters share another, the two ordinary locals (`sum` and `result`) share a third, and `unused_tmp`, whose type and location were optimized away, needs a fourth because its attribute list is genuinely a different shape, not because its value differs. A DIE's shape is decided entirely by which attributes it carries, never by what those attributes say.

??? check "Two `DW_TAG_variable` DIEs in the same function both have a `DW_AT_name`. One also has a `DW_AT_location` because it survives to run time; the other was eliminated entirely and has neither a type nor a location. Can the two share an abbreviation code?"

    No. An abbreviation is keyed on the tag and the exact set of attributes present, not on what the attributes say. The surviving variable's abbreviation lists `DW_AT_location` among its attributes; the eliminated one's does not, so its attribute list is a different shape even though both share the tag `DW_TAG_variable` and both have a name. They need two different abbreviation codes, the same way `unused_tmp` needed its own abbreviation in the example above.

## Call frame information: unwinding a stack the unwinder did not build

[A5](a5-stack-frames.md#key-ideas) already answered how one function calls another: a **frame record**, x29 and x30 saved together with x29 pointing at them, chained frame to frame, so a debugger can walk from the innermost call back to the first. That is enough to print a backtrace's list of return addresses when nothing has gone wrong. It is not enough for every case a real unwinder has to handle: a signal that arrives mid-instruction, a language with exception handling unwinding past several frames to find a handler, or a profiler sampling a stack at an arbitrary, unpredictable point in a function's body. All three need to answer "where is the saved return address, and where is the caller's frame pointer, at *this exact instruction*," not only "at the function's first instruction." A function's prologue does not finish its saves in one atomic step: between `stp x29, x30, [sp, #-32]!` and the following `mov x29, sp`, the frame record exists but x29 does not point at it yet, and an unwinder asked to work at that one instruction needs to know that too.

**Call frame information**, or **CFI**, is DWARF's answer: a second, per-instruction opcode program, structurally the twin of the line-number program but describing register locations instead of source lines. Two record kinds carry it. A **CIE** (Common Information Entry) holds the parts shared by every function using one calling convention: which register number is the return address, what the very first instruction's state looks like, before any of a function's own prologue has run. An **FDE** (Frame Description Entry) points at one CIE and then carries the opcode program for one specific function's prologue and epilogue, saying at which instruction offset each register save happens and where it went[^taylor-ehframe]. Assemble a function with `.cfi_startproc`, `.cfi_def_cfa_offset` and `.cfi_offset` directives, the same kind of directive family as `.loc`, and the assembler builds exactly this: a CIE shared by the object file's functions and one FDE per function.

--8<-- "includes/examples/backend/d1-debug-info/cfi_unwind.s.md"

Assembling this and reading its `.eh_frame` section (Apple clang 21, `llvm-dwarfdump --eh-frame`, arm64-apple-macosx, 2026-09-24) shows the CIE and FDE the three `.cfi_*` directives produced:

```text
00000000 00000010 00000000 CIE
  Format:                DWARF32
  Augmentation:          "zR"
  Code alignment factor: 1
  Data alignment factor: -8
  Return address column: 30
  DW_CFA_def_cfa: WSP +0

00000014 00000020 00000018 FDE cie=00000000 pc=00000040...00000060
  DW_CFA_advance_loc: 4
  DW_CFA_def_cfa_offset: +32
  DW_CFA_offset: W30 -24
  DW_CFA_offset: W29 -32

  0x40: CFA=WSP
  0x44: CFA=WSP+32: W29=[CFA-32], W30=[CFA-24]
```

Read the bottom two lines the way an unwinder does, not the way an assembly listing reads. At address 0x40, the function's first instruction, the **canonical frame address** (the address one past the incoming stack pointer, DWARF's fixed reference point for a frame) is simply the stack pointer: nothing has been saved yet. At 0x44, one instruction later, `stp x29, x30, [sp, #-32]!` has run, and the row changes on every count: the CFA is now 32 bytes above sp, and both w29 and w30 have addresses at which they were saved. `DW_CFA_advance_loc: 4` is what makes this address-indexed: it is CFI's version of the line table's `advance_pc`, telling the unwinder these facts hold starting four bytes into the function, not from the very first instruction. An unwinder stopped at address 0x40 and one stopped at 0x44 get different, correct answers for where x30 lives, from the same FDE.

Apple's targets keep a second, independent answer to the same question alongside the DWARF one. `count_word_char`'s object file also carries a `__compact_unwind` section, a fixed-size, four-byte-per-function encoding for the small set of prologue shapes clang's own back end knows how to compile down (a frame-pointer save at a fixed offset is one of them), one entry per function, decoded without running any opcode program at all[^compact-unwind]. The two coexist deliberately: compact unwind is what `libunwind` reads first, because a table lookup is cheaper than interpreting a byte-code program, and it falls back to walking the `.eh_frame` DWARF program only for a prologue shape compact unwind's small format cannot express. Neither is a substitute for the frame record from [A5](a5-stack-frames.md); the frame record is what a debugger reads directly out of live memory to walk a stack it is not unwinding formally, while CFI and compact unwind exist because a stack that is *not* live, one being unwound after the fact, offers no register to read x29 out of at all, and needs the equivalent facts recovered from data instead.

This also explains the split between `.debug_frame` and `.eh_frame`, two sections that hold the same CIE/FDE structure for two different readers. `.eh_frame` ships with the executable, because exception handling and signal unwinding need it whether or not a debugger is attached; `.debug_frame`, when a compiler emits it at all, is stripped along with the rest of `.debug_*` before shipping, kept only for an offline debugger. AAPCS64's own DWARF register mapping, which register number means x29 and which means the vector registers, is standardized separately from the frame-information format itself, so that a CIE built for AArch64 and one built for x86-64 agree on what "register 29" even refers to[^aadwarf64].

On a fully stripped Mach-O executable, one more piece finishes the picture: `dsymutil` reads the executable's **debug map**, a table connecting the stripped binary's remaining symbols back to the `.o` files the linker consumed, and uses it to gather every one of those object files' `.debug_info` and `.debug_line` sections into a single `.dSYM` bundle next to the executable, so a debugger can symbolicate a crash from a shipped binary without the original build directory still existing[^dsymutil].

??? check "A profiler samples a running program's stack once every millisecond, at whatever instruction each sample happens to land on. Why can it not simply read x29 out of the CPU and walk the frame-record chain from A5, the way lldb does at a breakpoint?"

    It can, and on a live, running thread that is often exactly what it does: x29 is a real register with a real value at every instant, so the frame-record chain works at any instruction, not only at function boundaries. Call frame information earns its keep in the case that trips this up: a signal handler, or an unwinder working from a *saved* copy of the registers taken at an arbitrary instant rather than from the live thread, needs to know not only what x29 holds now but which memory location backs it up, in case the sample landed inside a prologue before x29 was set. CFI's per-instruction program answers that question at any address; a single frame-record read only answers it for the register file the CPU happens to hold at the moment of the read.

## Line tables first, locations later

Everything in this chapter builds toward one destination the chapter deliberately stops short of: `DW_AT_location`, the attribute that tells a debugger where a variable's *value* lives, in a register, on the stack, or nowhere at all because it was optimized away, at each point in a function. That attribute needs a location-list opcode program of its own, `DW_OP_fbreg` for a stack slot relative to the frame base, `DW_OP_regN` for a register, and it needs the compiler's register allocator and spiller to already track, precisely, where every value lives at every instruction, which [C5](c5-spilling.md) has not yet covered. Line tables and call frame information do not have that dependency: a line table only needs to know which instructions came from which source position, and CFI only needs to know where the *fixed* set of callee-saved registers and the return address were saved, both decided once, early, in code generation. That is why the research behind this chapter, and DWARF producers in general, build line tables and frame information first and treat variable locations as later work.

The connection back to Vortex's own runtime is closer than it looks. [Stage 9's runtime error](../compiler/guide/stage-9-runtime-safety.md#stopping-with-a-clear-runtime-error) already reports `runtime error[<kind>]: <message> at <file>:<line>:<column>` for every trap a Vortex program hits, bounds, overflow, running out of stack. That message is built by the compiler carrying source positions through to the point where a check fails at run time; a `.debug_line` table is the same idea turned outward, letting a tool outside the running program, `lldb`, a profiler, a crash reporter, recover the same file:line:column for an address it observed from outside. The compiler already tracks the position; debug information is what makes that tracking legible to something other than the compiler itself.

## For Vortex

!!! vortex "Exercise"

    **Build** the smallest debug-information pipeline that makes `lldb` useful on a compiled Vortex program: enough of a line table to set a breakpoint by file and line, and enough call frame information to walk a stack from anywhere.

    1. A line-table emitter that walks your compiler's existing source-position tracking, the same positions [stage 9](../compiler/guide/stage-9-runtime-safety.md#stopping-with-a-clear-runtime-error) already carries to build its `at <file>:<line>:<column>` message, and emits one `.loc` directive (or the equivalent LLVM IR debug metadata, if your back end goes through LLVM) at the start of each Vortex statement, keyed by the file and line your parser already recorded.
    2. Call frame information for every prologue and epilogue your back end from [A5](a5-stack-frames.md#for-vortex) emits: `.cfi_startproc`, one `.cfi_offset` per register your prologue saves, `.cfi_def_cfa_offset` at the point the frame is allocated, `.cfi_endproc`. If you assemble through `as` or the LLVM integrated assembler, the directives alone are enough; the assembler builds the CIE and FDE.
    3. A minimal `.debug_info`: a compile-unit DIE and one `DW_TAG_subprogram` DIE per function, with a name and a low/high PC range, so `lldb` can print a function's name in a backtrace instead of a bare address.

    **Not yet:** `DW_AT_location` for variables (it needs the register and spill tracking [C5](c5-spilling.md) has not covered yet); type DIEs beyond what a function signature needs; anything for a JIT'd function that has no object file at all, that is [D2](d2-jit.md)'s problem, not this one's.

    **Proof that it works:**

    - `llvm-dwarfdump --debug-line` on a compiled Vortex object file shows one row per statement your compiler emitted, at the file and line your own source records, checked against the source by hand for at least one function with a loop.
    - `lldb`, given a compiled Vortex program, accepts `breakpoint set --file <name> --line <N>` for a line inside a function body and actually stops there when the program runs past it; a line with no code of its own (a closing brace, a comment) should fail to set, or lldb should say so, rather than stopping somewhere misleading.
    - `lldb bt`, run with the program stopped at least three calls deep, prints every frame's function name, not only its address, the same test [A5](a5-stack-frames.md#for-vortex) asked for with the frame-record chain alone, now checked again with the DIEs from this exercise in place.
    - A table, filled in from your own compiler, with the date and its version: whether your build emits `.debug_frame`, `.eh_frame`, or (on Apple's targets) compact unwind, and which one `lldb bt` actually used to produce the backtrace above.

## Key ideas

!!! recap "Questions you can now answer"

    - **Why is a DWARF line-number table stored as an opcode program instead of one row per address?** Because most instructions share a line with their neighbors; the program advances `address` and `line` independently and writes a row only when a `copy` opcode runs, so a long straight-line run of code costs a handful of opcodes, not one row per instruction.
    - **What is a DIE, and why do many DIEs share one abbreviation code?** A debugging information entry: a tag plus a list of attributes describing one program construct. Its abbreviation is keyed on the tag and which attributes are present, not their values, so any two DIEs with the same shape, two parameters, two ordinary locals, share one abbreviation no matter what their names or types say.
    - **What does call frame information add beyond the frame record from A5?** A per-instruction program, the CFI twin of the line table, that says where the return address and saved registers live at any exact instruction, not only at a function's first one; a live frame-record read only answers that question for the register file the CPU holds right now.
    - **What is the difference between a CIE and an FDE?** A CIE holds what every function sharing one calling convention has in common; an FDE points at a CIE and carries one specific function's own prologue and epilogue as a sequence of address-indexed opcodes.
    - **Why does Apple's `__compact_unwind` coexist with `.eh_frame` in the same object file, instead of replacing it?** Compact unwind is a fixed-size, table-lookup encoding for the small set of prologue shapes the back end knows how to compile down; it is read first because it is cheap, and an unwinder falls back to interpreting the DWARF `.eh_frame` program only for a shape compact unwind cannot express.
    - **Why does this chapter build line tables and call frame information before variable locations?** Both depend only on facts decided once, early: which source position an instruction came from, and where the fixed set of callee-saved registers were spilled. A variable's location can change at every instruction as the register allocator and spiller move it, which needs machinery this chapter does not yet have.

## Where this comes back

!!! next "You will use this again in"

    - [B4. Linking and loading](b4-linking-and-loading.md): *the debug map*, *what `dsymutil` gathers from separate object files*
    - [C5. Spilling, splitting and rematerialization](c5-spilling.md): *why a spilled variable's DWARF location must change where the spill moved it*
    - [D2. JIT compilation](d2-jit.md): *debug information for code with no object file to hold it*
    - [D3. Reading real back ends](d3-real-backends.md): *a production compiler's actual DWARF and CFI emitters, read end to end*
    - [E3. LLVM's allocator, scheduler and MC layer](e3-llvm-allocator-scheduler-mc.md): *how LLVM's MC layer turns `.loc` and `.cfi_*` directives, or their IR metadata equivalents, into the sections this chapter read*

## Sources and further reading

Read the DWARF 5 standard's own line-number program and abbreviations sections first; both are short and every other source here assumes them. Eager's introduction is the clearest plain-language walk through the same material, written for someone reading the standard for the first time. Taylor's post on `.eh_frame` is the best explanation of why call frame information exists as its own format rather than being folded into the line table. The compact unwind header is source code, not prose, but its comments state the encoding's tradeoffs directly. AADWARF64 is the register-number mapping every AArch64 CIE and FDE in this chapter relies on without saying so.

[^dwarf5]: DWARF Debugging Information Format Committee, "DWARF Debugging Information Format, Version 5", 2017, sections on the line number program and the abbreviations tables, read 2026-09-24. <https://dwarfstd.org/dwarf5std.html>
[^eager]: Michael J. Eager (ed.), "Introduction to the DWARF Debugging Format", DWARF Standards Committee, April 2012, read 2026-09-24. <https://dwarfstd.org/doc/Debugging-using-DWARF-2012.pdf>
[^llvm-sld]: LLVM Project, "Source Level Debugging with LLVM", read 2026-09-24. <https://llvm.org/docs/SourceLevelDebugging.html>
[^dsymutil]: LLVM Project, "dsymutil", LLVM Command Guide, the description of the debug map and `.dSYM` bundle, read 2026-09-24. <https://llvm.org/docs/CommandGuide/dsymutil.html>
[^taylor-ehframe]: Ian Lance Taylor, ".eh_frame", 10 January 2011. <https://www.airs.com/blog/archives/460>
[^compact-unwind]: LLVM Project, libunwind, `compact_unwind_encoding.h`, read 2026-09-24. <https://github.com/llvm/llvm-project/blob/main/libunwind/include/mach-o/compact_unwind_encoding.h>
[^aadwarf64]: ARM, "DWARF for the Arm 64-bit Architecture (AADWARF64)", read 2026-09-24. <https://github.com/ARM-software/abi-aa/blob/main/aadwarf64/aadwarf64.rst>

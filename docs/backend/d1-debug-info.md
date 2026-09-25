# D1. Debug information

<p class="page-intro">A compiled function is a run of bytes with no source code left in it. Debug information is a second encoding, written beside the machine code, that maps those bytes back to files, lines, functions and stack frames. This chapter decodes the three parts a young compiler needs first (the line table, the tree of entries and the unwind tables) byte by byte, so that Vortex programs can be stepped, backtraced and profiled in the terms their author wrote.</p>

<p class="vx-meta" markdown="1">Level: Advanced · Reading time: about 55 minutes · Builds on: [A5. Stack frames](a5-stack-frames.md), [B3. Object files and assemblers](b3-object-files.md)</p>

???+ remember "Before you start, remember"

    ??? question "What is a frame record, and how does a debugger walk a chain of them?"

        The saved x29 (FP) and x30 (LR) of one call, stored together, with x29 pointing at them. Walking the chain means following each saved FP to the record below it, down to a zero that marks the first call.

        Introduced in [A5. Stack frames](a5-stack-frames.md#the-frame-record-and-the-chain).

    ??? question "Which functions may skip building a frame record under Apple's arm64 ABI?"

        Leaf functions (functions that call nothing) and tail calls. Every other function keeps x29 pointing at a valid frame record, which is why a backtrace works on Apple's platforms even without debug information.

        Introduced in [A5. Stack frames](a5-stack-frames.md#the-frame-record-and-the-chain).

    ??? question "What is a relocation, and what three things does it record?"

        The object file's record of one unfinished patch: which bytes are wrong, which symbol will fix them, and how to combine the symbol's final address with whatever already sits in those bytes.

        Introduced in [B3. Object files and assemblers](b3-object-files.md#relocations-the-holes-themselves).

    ??? question "When a Vortex runtime check fails, what line does the program write to standard error?"

        `runtime error[<kind>]: <message> at <file>:<line>:<column>`, and then it exits with status 101. The kind names the failed check, such as `bounds` or `stack`.

        Introduced in [9. Runtime safety](../compiler/guide/stage-9-runtime-safety.md#stopping-with-a-clear-runtime-error).

!!! goals "In this chapter"

    - Decode a DWARF line-number program by hand, byte by byte, including a special opcode, and explain why DWARF stores a program instead of a table.
    - Read a `.debug_info` tree of entries and its abbreviation table, and predict which entries share an abbreviation code.
    - Read call frame information as rows of unwinding rules, and explain what it gives an unwinder that the frame record from A5 does not.
    - Predict whether an Apple arm64 function gets a compact unwind encoding or falls back to a DWARF entry, and check the prediction with `llvm-objdump`.
    - Trace where each kind of debug information goes between the assembler and the debugger, including the `.dSYM` bundle on macOS.

## A run of bytes cannot name itself

Stop a running program in a debugger and the processor can tell you one thing about where you are: the program counter, a number. Everything else the debugger prints (a backtrace with function names, a highlighted source line, a local variable read out by name) comes from data the compiler wrote next to the code and the debugger reads alongside it.

That data is not hidden. Compile a six-line C function with `clang -g -O0 -c clamp.c` and list the sections of the object file (Apple clang 21, macOS 27 on an M4 Pro, 2026-09-24; the source appears in the section on `.debug_info` below):

```text
Idx Name             Size     Type
  0 __text           00000064 TEXT
  1 __debug_abbrev   00000061 DATA, DEBUG
  2 __debug_info     00000066 DATA, DEBUG
  3 __debug_str_offs 00000034 DATA, DEBUG
  4 __debug_str      0000011a DATA, DEBUG
  5 __debug_addr     00000010 DATA, DEBUG
  6 __debug_names    00000070 DATA, DEBUG
  7 __compact_unwind 00000020 DATA
  8 __debug_line     0000008d DATA, DEBUG
  9 __debug_line_str 00000068 DATA, DEBUG
```

One hundred bytes of code arrive with nine sections that describe them. **Debug information** is the general name for this data: facts produced by the compiler that describe the compiled program in the vocabulary of its source rather than of its instructions. Every section in that list except `__text` and `__compact_unwind` holds **DWARF**, the debugging format these tools use, defined by the DWARF committee's standard, whose current version 5 was published in 2017[^dwarf5]. Mach-O spells the section names `__debug_line` and so on; ELF spells the same sections `.debug_line`. This chapter uses the ELF spelling in prose.

This chapter covers three jobs, in the order a new back end needs them. The **line table** maps an address to a file, line and column. The **tree of entries** in `.debug_info` names the functions, their parameters and their types. **Call frame information** tells an unwinder how to step from one frame to its caller at any instruction. Describing where a variable's value lives at every instruction after optimization is a fourth job; it waits for [C5](c5-spilling.md), for reasons the last section explains.

<figure class="vx-figure">
<svg viewBox="0 0 760 300" role="img" aria-label="Where debug information goes from the compiler to the debugger on macOS" aria-describedby="d1-flow-desc">
<desc id="d1-flow-desc">Left to right. The compiler writes assembly containing .loc and .cfi directives. The assembler turns them into an object file with __debug_line, __debug_info and __debug_abbrev sections, plus __compact_unwind and, for some functions, __eh_frame. The linker builds the executable: it keeps the unwind data as __unwind_info and __eh_frame, drops the debug sections, and writes a debug map into the symbol table naming each object file. dsymutil follows the debug map back to the object files and links their DWARF into a .dSYM bundle. lldb reads the executable and the .dSYM.</desc>
<defs><marker id="d1-flow-head" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker></defs>
<g class="vx-seq" style="--vx-i: 0; --vx-n: 5">
<rect class="vx-box" x="10" y="40" width="130" height="110" rx="4"/>
<text class="vx-text" x="75" y="62" text-anchor="middle">compiler</text>
<text class="vx-mono" x="22" y="88">.loc 1 11 9</text>
<text class="vx-mono" x="22" y="108">.cfi_offset</text>
<text class="vx-text-muted" x="22" y="134">assembly text</text>
</g>
<line class="vx-line" x1="140" y1="95" x2="168" y2="95" marker-end="url(#d1-flow-head)"/>
<g class="vx-seq" style="--vx-i: 1; --vx-n: 5">
<rect class="vx-box" x="170" y="20" width="170" height="160" rx="4"/>
<text class="vx-text" x="255" y="42" text-anchor="middle">object file (.o)</text>
<text class="vx-mono" x="182" y="68">__text</text>
<text class="vx-mono" x="182" y="88">__debug_line</text>
<text class="vx-mono" x="182" y="108">__debug_info</text>
<text class="vx-mono" x="182" y="128">__debug_abbrev</text>
<text class="vx-mono" x="182" y="148">__compact_unwind</text>
<text class="vx-mono" x="182" y="168">__eh_frame</text>
</g>
<line class="vx-line" x1="340" y1="95" x2="378" y2="95" marker-end="url(#d1-flow-head)"/>
<g class="vx-seq" style="--vx-i: 2; --vx-n: 5">
<rect class="vx-box-strong" x="380" y="20" width="170" height="160" rx="4"/>
<text class="vx-text" x="465" y="42" text-anchor="middle">executable</text>
<text class="vx-mono" x="392" y="68">__text</text>
<text class="vx-mono" x="392" y="88">__unwind_info</text>
<text class="vx-mono" x="392" y="108">__eh_frame</text>
<text class="vx-text-accent" x="392" y="134">debug map:</text>
<text class="vx-text-muted" x="392" y="152">"clamp.o holds</text>
<text class="vx-text-muted" x="392" y="170">DWARF for _clamp"</text>
</g>
<line class="vx-line" x1="550" y1="95" x2="608" y2="95" marker-end="url(#d1-flow-head)"/>
<g class="vx-seq" style="--vx-i: 4; --vx-n: 5">
<rect class="vx-box" x="610" y="40" width="140" height="110" rx="4"/>
<text class="vx-text" x="680" y="62" text-anchor="middle">lldb</text>
<text class="vx-text-muted" x="622" y="88">backtraces,</text>
<text class="vx-text-muted" x="622" y="106">line breakpoints,</text>
<text class="vx-text-muted" x="622" y="124">names</text>
</g>
<g class="vx-seq" style="--vx-i: 3; --vx-n: 5">
<rect class="vx-box-accent" x="380" y="220" width="170" height="60" rx="4"/>
<text class="vx-text" x="465" y="244" text-anchor="middle">dsymutil</text>
<text class="vx-mono" x="465" y="266" text-anchor="middle">prog.dSYM</text>
<path class="vx-line" d="M255 180 L255 250 L378 250" fill="none" marker-end="url(#d1-flow-head)"/>
<text class="vx-text-muted" x="262" y="240">DWARF stays here</text>
<path class="vx-line" d="M550 250 L680 250 L680 152" fill="none" marker-end="url(#d1-flow-head)"/>
</g>
</svg>
<figcaption>Figure 1. Where each kind of debug information goes on macOS. The unwind tables travel into the executable, because the running program needs them. The DWARF debug sections stay in the object files; the executable records only a debug map naming them, and <code>dsymutil</code> gathers them into a <code>.dSYM</code> bundle.</figcaption>
</figure>

## The line table: a program, not a table

Start with the smallest question a debugger must answer: given an address, which source line was the compiler translating when it emitted the instruction there? The obvious answer is a table with one row per instruction. Most neighbouring instructions share a line, so almost every row would repeat the one above it.

Here is the question on a real function. `line_table.s` is `clamp(v, lo, hi)` written by hand. Each `.loc` directive says "the instructions after this came from file 1, line L, column C"; the assembler collects them into a `.debug_line` section, the same way a compiler's own `.loc` lines are collected when it compiles with `-g`[^llvm-sld].

--8<-- "includes/examples/backend/d1-debug-info/line_table.s.md"

Assembling it with `cc -c` and reading the result with `llvm-dwarfdump --debug-line` (LLVM 18.1.8 tools, same machine and date) gives six rows:

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

Each row says "from this address on, until the next row, the code belongs to this line and column". The row at 0x8 covers the `mov` and the `b` at 0x8 and 0xc. The last row, at 0x24, is one byte past the final `ret`. Its **end_sequence** flag marks the first address after a contiguous run of code, so a debugger does not attribute whatever follows the function to line 16[^dwarf5].

### The state machine

DWARF does not store these rows. It stores a short program that rebuilds them when run on a small, imagined **state machine**: a set of registers with defined starting values and a list of byte-sized instructions that change them[^dwarf5][^eager]. The registers that matter here are `address`, `file`, `line` and `column`, plus the flags `is_stmt` (this instruction is a good place for a line breakpoint) and `end_sequence`. At the start of each sequence, `address` is 0, `line` is 1 and `is_stmt` takes a default from the table's header[^dwarf5].

Instructions come in three kinds. **Standard opcodes** each do one thing: `DW_LNS_copy` (opcode 1) appends the current registers as a row, `DW_LNS_advance_pc` (2) adds to `address`, `DW_LNS_advance_line` (3) adds a signed amount to `line`, and `DW_LNS_set_column` (5) sets `column`. **Extended opcodes** start with a zero byte, then a length, then a sub-opcode; `DW_LNE_end_sequence` and `DW_LNE_set_address` are the two used here. **Special opcodes**, every byte value from the header's `opcode_base` upward, advance `address` and `line` together and append a row, all in one byte[^dwarf5].

The operands are **LEB128** numbers ("little-endian base 128"): each byte carries seven bits of the value, low bits first, and a set top bit means another byte follows. Small numbers, which are most numbers in debug information, take one byte[^dwarf5]. The signed form, SLEB128, sign-extends from the last byte, so a line step of −1 is the single byte `0x7f`.

### Decoding it by hand

`llvm-dwarfdump --debug-line --verbose` prints the bytes of the program for `line_table.s`, and the table's header, which says `line_base = -5`, `line_range = 14` and `opcode_base = 13`. The whole program is 33 bytes, 8 of them the starting address:

```text
05 05                          set_column 5
00 09 02 00 00 00 00 00 00 00 00   extended, 9 bytes: set_address 0x0
03 09                          advance_line 9        line 1 -> 10
01                             copy                  row 0x00, line 10, col 5
05 09                          set_column 9
83                             special opcode
05 05                          set_column 5
84                             special opcode
05 09  83  05 05  84           the same pattern again
02 04                          advance_pc 4
00 01 01                       extended, 1 byte: end_sequence
```

Take the first special opcode, `0x83`, which is 131. DWARF defines its meaning with three lines of arithmetic[^dwarf5]:

$$\text{adjusted} = 131 - \text{opcode\_base} = 118$$

$$\Delta\text{address} = \lfloor 118 / \text{line\_range} \rfloor = \lfloor 118 / 14 \rfloor = 8$$

$$\Delta\text{line} = \text{line\_base} + (118 \bmod 14) = -5 + 6 = 1$$

So `0x83` moves `address` from 0 to 8 and `line` from 10 to 11, then appends the row `0x8, line 11`, which is the second row of the table. (The header's `minimum_instruction_length` is 1 here, so the address step is in bytes.) Try `0x84` yourself before reading on: adjusted 119, address step 8, line step −5 + 7 = 2, which takes line 11 at 0x8 to line 13 at 0x10.

<figure class="vx-figure">
<svg viewBox="0 0 760 290" role="img" aria-label="The line-number state machine decoding the bytes of line_table.s's program" aria-describedby="d1-line-desc">
<desc id="d1-line-desc">Three columns. Left: the opcode bytes in order, grouped. Middle: the address and line registers after each group. Right: the rows appended. 03 09 then 01 give address 0 line 10 and append a row. 83 adds 8 to the address and 1 to the line, giving 0x8 line 11 and a row. 84 adds 8 and 2, giving 0x10 line 13. 83 gives 0x18 line 14. 84 gives 0x20 line 16. 02 04 then 00 01 01 add 4 to the address and append the end_sequence row at 0x24.</desc>
<defs><marker id="d1-line-head" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker></defs>
<text class="vx-text-muted" x="120" y="18" text-anchor="middle">opcode bytes</text>
<text class="vx-text-muted" x="395" y="18" text-anchor="middle">registers after</text>
<text class="vx-text-muted" x="650" y="18" text-anchor="middle">row appended</text>
<g class="vx-seq" style="--vx-i: 0; --vx-n: 6">
<rect class="vx-box" x="10" y="30" width="220" height="34" rx="4"/><text class="vx-mono" x="20" y="52">03 09, 01 (line += 9, copy)</text>
<line class="vx-line" x1="230" y1="47" x2="298" y2="47" marker-end="url(#d1-line-head)"/>
<text class="vx-mono" x="300" y="52">address 0x00, line 10</text>
<line class="vx-line" x1="505" y1="47" x2="568" y2="47" marker-end="url(#d1-line-head)"/>
<rect class="vx-box" x="570" y="30" width="180" height="34" rx="4"/><text class="vx-mono" x="580" y="52">0x00  10</text>
</g>
<g class="vx-seq" style="--vx-i: 1; --vx-n: 6">
<rect class="vx-box-accent" x="10" y="72" width="220" height="34" rx="4"/><text class="vx-mono" x="20" y="94">83 (+8 bytes, +1 line)</text>
<line class="vx-line" x1="230" y1="89" x2="298" y2="89" marker-end="url(#d1-line-head)"/>
<text class="vx-mono" x="300" y="94">address 0x08, line 11</text>
<line class="vx-line" x1="505" y1="89" x2="568" y2="89" marker-end="url(#d1-line-head)"/>
<rect class="vx-box-accent" x="570" y="72" width="180" height="34" rx="4"/><text class="vx-mono" x="580" y="94">0x08  11</text>
</g>
<g class="vx-seq" style="--vx-i: 2; --vx-n: 6">
<rect class="vx-box" x="10" y="114" width="220" height="34" rx="4"/><text class="vx-mono" x="20" y="136">84 (+8 bytes, +2 lines)</text>
<line class="vx-line" x1="230" y1="131" x2="298" y2="131" marker-end="url(#d1-line-head)"/>
<text class="vx-mono" x="300" y="136">address 0x10, line 13</text>
<line class="vx-line" x1="505" y1="131" x2="568" y2="131" marker-end="url(#d1-line-head)"/>
<rect class="vx-box" x="570" y="114" width="180" height="34" rx="4"/><text class="vx-mono" x="580" y="136">0x10  13</text>
</g>
<g class="vx-seq" style="--vx-i: 3; --vx-n: 6">
<rect class="vx-box" x="10" y="156" width="220" height="34" rx="4"/><text class="vx-mono" x="20" y="178">83 (+8 bytes, +1 line)</text>
<line class="vx-line" x1="230" y1="173" x2="298" y2="173" marker-end="url(#d1-line-head)"/>
<text class="vx-mono" x="300" y="178">address 0x18, line 14</text>
<line class="vx-line" x1="505" y1="173" x2="568" y2="173" marker-end="url(#d1-line-head)"/>
<rect class="vx-box" x="570" y="156" width="180" height="34" rx="4"/><text class="vx-mono" x="580" y="178">0x18  14</text>
</g>
<g class="vx-seq" style="--vx-i: 4; --vx-n: 6">
<rect class="vx-box" x="10" y="198" width="220" height="34" rx="4"/><text class="vx-mono" x="20" y="220">84 (+8 bytes, +2 lines)</text>
<line class="vx-line" x1="230" y1="215" x2="298" y2="215" marker-end="url(#d1-line-head)"/>
<text class="vx-mono" x="300" y="220">address 0x20, line 16</text>
<line class="vx-line" x1="505" y1="215" x2="568" y2="215" marker-end="url(#d1-line-head)"/>
<rect class="vx-box" x="570" y="198" width="180" height="34" rx="4"/><text class="vx-mono" x="580" y="220">0x20  16</text>
</g>
<g class="vx-seq" style="--vx-i: 5; --vx-n: 6">
<rect class="vx-box-strong" x="10" y="240" width="220" height="34" rx="4"/><text class="vx-mono" x="20" y="262">02 04, 00 01 01 (end)</text>
<line class="vx-line" x1="230" y1="257" x2="298" y2="257" marker-end="url(#d1-line-head)"/>
<text class="vx-mono" x="300" y="262">address 0x24</text>
<line class="vx-line" x1="505" y1="257" x2="568" y2="257" marker-end="url(#d1-line-head)"/>
<rect class="vx-box-strong" x="570" y="240" width="180" height="34" rx="4"/><text class="vx-mono" x="580" y="262">0x24  end_sequence</text>
</g>
</svg>
<figcaption>Figure 2. The state machine running the real program from <code>line_table.s</code> (the <code>set_column</code> bytes are left out). Each one-byte special opcode moves two registers and appends a row; the highlighted <code>0x83</code> is the one decoded by hand above.</figcaption>
</figure>

The next example is the same decoding done by a program. It holds the 33 bytes, applies the three rules for special opcodes and the handful of standard and extended opcodes this program uses, and prints a trace. Its rows match `llvm-dwarfdump`'s, which is the check that the arithmetic above is right.

--8<-- "includes/examples/backend/d1-debug-info/line_number_program.cpp.md"

### Why a program

Two reasons, both visible in the example. First, size: the whole program is 33 bytes, while a table holding an 8-byte address for each of its six rows would spend 48 bytes on the addresses alone. Each one-byte special opcode does the work of a row. Second, freedom: `address` only moves forward, but `line` may move back. The standard's own discussion of special opcodes gives the reason a negative `line_base` exists: on a machine where the scheduler interleaves instructions from different lines, a later instruction may belong to an earlier line[^dwarf5]. A loop's back edge does the same thing.

The same freedom covers code that belongs to no line. The standard allows **line 0** for instructions that cannot be attributed to any source line[^dwarf5], such as a shared error path the compiler invented. The `is_stmt` flag marks the rows that are recommended breakpoint locations, the places that represent a line or statement[^dwarf5]. In the `.ll` example later in this chapter, LLVM marks the second row of a one-line function `is_stmt 0`: that row continues line 3 rather than starting a statement.

??? check "Which single byte encodes \"address += 4, line -= 1\" in this table's header, and why could no special opcode encode \"line += 9\"?"

    Work the formula backwards: opcode = (Δline − line_base) + line_range × Δaddress + opcode_base = (−1 + 5) + 14 × 4 + 13 = 73, which is `0x49`. Decoding checks it: 73 − 13 = 60, 60 / 14 = 4, −5 + 60 mod 14 = −5 + 4 = −1. The largest line step a special opcode can express is line_base + line_range − 1 = 8, so a jump of 9 lines needs `DW_LNS_advance_line`, which is why the program above reaches line 10 with `03 09` rather than a special opcode.

## The tree of entries: `.debug_info`

The line table answers "which line is this address?", a question about code. `.debug_info` answers questions about the program's structure: which functions exist, what each one's parameters and locals are called, what their types are, and where they live. Here is the source that produced the section list at the top of this chapter:

```c
int clamp(int v, int lo, int hi) {
    int r = v;
    if (r < lo) r = lo;
    if (r > hi) r = hi;
    return r;
}
```

`llvm-dwarfdump --debug-info clamp.o` prints this tree (same machine and date; long paths and a few compile-unit attributes cut):

```text
0x0000000c: DW_TAG_compile_unit
              DW_AT_producer    ("Apple clang version 21.0.0 (clang-2100.3.30.1)")
              DW_AT_name        ("clamp.c")
              DW_AT_stmt_list   (0x00000000)
              DW_AT_low_pc      (0x0000000000000000)
              DW_AT_high_pc     (0x0000000000000064)
0x00000025:   DW_TAG_subprogram
                DW_AT_low_pc    (0x0000000000000000)
                DW_AT_high_pc   (0x0000000000000064)
                DW_AT_frame_base (DW_OP_reg31 WSP)
                DW_AT_name      ("clamp")
                DW_AT_type      (0x00000061 "int")
0x00000034:     DW_TAG_formal_parameter
                  DW_AT_location (DW_OP_fbreg +12)
                  DW_AT_name    ("v")
                  DW_AT_type    (0x00000061 "int")
0x0000003f:     DW_TAG_formal_parameter      ... "lo", DW_OP_fbreg +8
0x0000004a:     DW_TAG_formal_parameter      ... "hi", DW_OP_fbreg +4
0x00000055:     DW_TAG_variable              ... "r",  DW_OP_fbreg +0
0x00000060:     NULL
0x00000061:   DW_TAG_base_type
                DW_AT_name      ("int")
                DW_AT_encoding  (DW_ATE_signed)
                DW_AT_byte_size (0x04)
```

Each block is a **debugging information entry**, or **DIE**: one record describing one thing in the program[^dwarf5][^eager]. A DIE has a **tag** that says what kind of thing it is (`DW_TAG_subprogram` for a function, `DW_TAG_formal_parameter` for a parameter) and a list of **attributes**, each a name and a value. Indentation shows the tree: the parameters are children of the function, the function a child of the compile unit, and a `NULL` entry closes a list of children.

Three attributes connect this tree to the rest of the chapter. `DW_AT_stmt_list` points at this unit's line table. `DW_AT_low_pc` and `DW_AT_high_pc` give the function's address range, which is how a debugger turns a program counter into the name `clamp` for a backtrace. `DW_AT_type` is a reference to another DIE, the `int` at offset 0x61, so a type is described once and shared.

The parameters' `DW_AT_location` values are small **DWARF expressions**: `DW_OP_fbreg +12` means "12 bytes past the frame base", and the function's `DW_AT_frame_base` says the frame base is the stack pointer. At `-O0` clang keeps each variable in one stack slot for the whole function, as here, so one expression per variable is enough.

### Abbreviations

Writing every DIE's tag and attribute names in full would repeat the same shape constantly: the three parameters carry the same five attributes in the same order. DWARF factors the shape out into `.debug_abbrev`. An **abbreviation** declares a tag, whether the entry has children, and an ordered list of (attribute, **form**) pairs, where the form says how the value is encoded (`DW_FORM_strx1` is a one-byte index into the unit's list of string offsets, `DW_FORM_ref4` a four-byte offset to another DIE). Each DIE in `.debug_info` then begins with an abbreviation code, as a ULEB128, followed only by the values[^dwarf5].

Here is the abbreviation clang wrote for the parameters (`llvm-dwarfdump --debug-abbrev clamp.o`):

```text
[3] DW_TAG_formal_parameter DW_CHILDREN_no
        DW_AT_location  DW_FORM_exprloc
        DW_AT_name      DW_FORM_strx1
        DW_AT_decl_file DW_FORM_data1
        DW_AT_decl_line DW_FORM_data1
        DW_AT_type      DW_FORM_ref4
```

The whole unit uses five abbreviations for seven DIEs. All three parameters use code 3. The local `r` has exactly the same attribute list, yet it gets its own code 4, because its tag differs. The next example assigns codes the way a producer does, first for the seven DIEs above and then for two that `clamp.c` did not have.

--8<-- "includes/examples/backend/d1-debug-info/abbrev_dedup.cpp.md"

The two extra entries each need a new code for a different reason. `t` has no `DW_AT_location`: when a variable's entry has no location, DWARF says the variable exists in the source but not in the running program[^dwarf5], which is how a debugger knows to print "optimized out". `zero` has the same attributes as `clamp` but no children, and the children flag is part of the abbreviation.

<figure class="vx-figure">
<svg viewBox="0 0 760 280" role="img" aria-label="The DIE tree for clamp.c with each entry's abbreviation code" aria-describedby="d1-die-desc">
<desc id="d1-die-desc">A tree. The compile unit, abbreviation 1, has two children: the subprogram clamp, abbreviation 2, and the base type int, abbreviation 5. clamp has four children: parameters v, lo and hi, all abbreviation 3, and the variable r, abbreviation 4. Dashed arrows from every type attribute point at the int entry. On the right, the abbreviation table lists codes 1 to 5 with their tags; code 3 is shared by three entries.</desc>
<defs><marker id="d1-die-head" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path class="vx-arrowhead" d="M0 0 L10 5 L0 10 z"/></marker></defs>
<rect class="vx-box-strong" x="170" y="14" width="190" height="34" rx="4"/><text class="vx-mono" x="180" y="36">[1] compile_unit</text>
<line class="vx-line" x1="220" y1="48" x2="120" y2="84"/>
<line class="vx-line" x1="320" y1="48" x2="420" y2="84"/>
<rect class="vx-box" x="30" y="84" width="190" height="34" rx="4"/><text class="vx-mono" x="40" y="106">[2] subprogram clamp</text>
<rect class="vx-box" x="340" y="84" width="170" height="34" rx="4"/><text class="vx-mono" x="350" y="106">[5] base_type int</text>
<line class="vx-line" x1="60" y1="118" x2="60" y2="244"/>
<g class="vx-seq" style="--vx-i: 0; --vx-n: 4">
<line class="vx-line" x1="60" y1="156" x2="80" y2="156"/>
<rect class="vx-box-accent" x="80" y="140" width="170" height="30" rx="4"/><text class="vx-mono" x="90" y="160">[3] parameter v</text>
</g>
<g class="vx-seq" style="--vx-i: 1; --vx-n: 4">
<line class="vx-line" x1="60" y1="190" x2="80" y2="190"/>
<rect class="vx-box-accent" x="80" y="176" width="170" height="30" rx="4"/><text class="vx-mono" x="90" y="196">[3] parameter lo</text>
</g>
<g class="vx-seq" style="--vx-i: 2; --vx-n: 4">
<line class="vx-line" x1="60" y1="224" x2="80" y2="224"/>
<rect class="vx-box-accent" x="80" y="212" width="170" height="30" rx="4"/><text class="vx-mono" x="90" y="232">[3] parameter hi</text>
</g>
<g class="vx-seq" style="--vx-i: 3; --vx-n: 4">
<line class="vx-line" x1="60" y1="258" x2="80" y2="258"/>
<rect class="vx-box" x="80" y="246" width="170" height="30" rx="4"/><text class="vx-mono" x="90" y="266">[4] variable r</text>
</g>
<path class="vx-line" d="M250 190 C 320 190, 400 160, 420 120" fill="none" stroke-dasharray="4 4" marker-end="url(#d1-die-head)"/>
<text class="vx-text-muted" x="300" y="210">DW_AT_type</text>
<rect class="vx-box" x="560" y="14" width="190" height="150" rx="4"/>
<text class="vx-text" x="570" y="36">.debug_abbrev</text>
<text class="vx-mono" x="570" y="62">1  compile_unit</text>
<text class="vx-mono" x="570" y="84">2  subprogram</text>
<text class="vx-text-accent" x="570" y="106">3  formal_parameter</text>
<text class="vx-mono" x="570" y="128">4  variable</text>
<text class="vx-mono" x="570" y="150">5  base_type</text>
<text class="vx-text-muted" x="560" y="192">each DIE stores a code,</text>
<text class="vx-text-muted" x="560" y="210">then only its values</text>
</svg>
<figcaption>Figure 3. The DIE tree clang wrote for <code>clamp.c</code>, with each entry's abbreviation code. Shape, not content, decides the code: the three parameters share code 3, and <code>r</code> needs code 4 only because its tag is different.</figcaption>
</figure>

One more detail ties this section to [B3](b3-object-files.md#relocations-the-holes-themselves). The object file's DWARF names addresses in `__text`, and those addresses are not final until the linker places the code. `llvm-objdump -r clamp.o` shows one relocation in `__debug_addr` and one in `__debug_line`, both against `__text`: the `DW_AT_low_pc` values and the line table's `set_address` operand are holes like any other.

??? check "A function has two locals, `a` and `b`, both `int`, both named, both declared in the same file. At `-O2`, `a` stays in a register and `b` is folded away entirely. Can their two DIEs share an abbreviation?"

    No. `a` keeps a `DW_AT_location` attribute and `b` has none, so their attribute lists differ, and the abbreviation is keyed on the tag, the children flag and the exact (attribute, form) list. Their values, such as the names `a` and `b`, never matter.

## Call frame information: unwinding at any instruction

[A5](a5-stack-frames.md#the-frame-record-and-the-chain) gave every calling function a frame record, and a debugger stopped at a breakpoint can follow x29 from record to record to list the return addresses. That covers the common case and misses three others.

First, an unwinder may stop anywhere. A profiler's sample or a signal can land on the first instruction of a function, after `bl` has put the return address in x30 but before the prologue has stored anything; at that moment x29 still points at the caller's record, and a pure x29 walk skips a frame. Second, a leaf function may build no record at all. Third, unwinding for exceptions must do more than list return addresses: it must put back every callee-saved register (x19 to x28, d8 to d15) that each frame saved, and a frame record says nothing about those.

DWARF describes the general problem precisely. A debugger **virtually unwinds** the stack: starting from the current frame, it computes the registers the caller would see and the caller's frame address, without changing the running program, then repeats[^dwarf5]. To do that it needs, for every instruction address, where each saved register is and how to find the caller's frame.

### The table and its rows

DWARF's answer is **call frame information** (**CFI**). Conceptually it is a large table: one row per code address, one column for the **canonical frame address** (**CFA**) and one column per register[^dwarf5]. The CFA is a fixed reference point for a frame, which the standard says is typically the stack pointer's value at the call site in the caller[^dwarf5]. On AArch64, `bl` does not move sp, so the CFA is the value sp had on entry. Each register column holds a rule, such as "saved at CFA − 24" or "unchanged".

Stored in full, that table would be larger than the code. As with the line table, DWARF stores a program instead. A **Common Information Entry** (**CIE**) holds what many functions share: the code and data alignment factors, which column holds the return address, and the opening rules. A **Frame Description Entry** (**FDE**) names one function's address range, points at its CIE, and carries opcodes that change the rules at chosen offsets into the function[^dwarf5][^taylor-ehframe].

The compiler does not write CIEs and FDEs itself. It writes **CFI directives** into its assembly, and the assembler builds the entries. `.cfi_startproc` and `.cfi_endproc` bracket a function; `.cfi_def_cfa_offset 32` says "the CFA is now sp + 32"; `.cfi_offset w30, -24` says "x30's value for the caller is saved at CFA − 24"; `.cfi_def_cfa w29, 16` says "compute the CFA from x29 + 16 from now on". A directive takes effect at the address where it appears, so it goes after the instruction that made it true.

### Decoding an FDE by hand

The next example holds three small functions with three prologue shapes. The middle one, `_odd_frame`, stores its frame record at the bottom of a 32-byte frame and describes the CFA from sp.

--8<-- "includes/examples/backend/d1-debug-info/cfi_unwind.s.md"

Assembled and linked into a small library with `cc -dynamiclib` (same machine and date), `llvm-dwarfdump --eh-frame` prints one CIE and one FDE (padding opcodes and format lines left out):

```text
00000000 00000010 00000000 CIE
  Version:               1
  Augmentation:          "zR"
  Code alignment factor: 1
  Data alignment factor: -8
  Return address column: 30
  Augmentation data:     10
  DW_CFA_def_cfa: WSP +0

00000014 00000020 00000018 FDE cie=00000000 pc=000003a8...000003c8
  DW_CFA_advance_loc: 4
  DW_CFA_def_cfa_offset: +32
  DW_CFA_offset: W30 -24
  DW_CFA_offset: W29 -32

  0x3a8: CFA=WSP
  0x3ac: CFA=WSP+32: W29=[CFA-32], W30=[CFA-24]
```

The FDE covers 0x3a8 to 0x3c8, which `nm` confirms is `_odd_frame`. Its opcodes are seven bytes, `44 0e 20 9e 03 9d 04`, and each can be read with DWARF's encoding table[^dwarf5]:

- `44`: the top two bits are `01`, which is `DW_CFA_advance_loc`, and the low six bits are the delta, 4. Multiplied by the code alignment factor 1, it moves the current location 4 bytes into the function, past the `stp`.
- `0e 20`: `DW_CFA_def_cfa_offset` with ULEB128 operand 32. The CFA is now sp + 32, because the `stp` moved sp down by 32.
- `9e 03`: top bits `10` are `DW_CFA_offset`, low six bits are register 30. The operand 3 is a *factored* offset: 3 × the data alignment factor −8 = −24. So x30 is saved at CFA − 24.
- `9d 04`: register 29, 4 × −8 = −32. x29 is saved at CFA − 32.

The register numbers come from Arm's DWARF supplement for AArch64, which numbers x0 to x30 as 0 to 30 and sp as 31[^aadwarf64]; the CIE's "return address column: 30" therefore means x30. The two rows at the bottom are the table these opcodes rebuild. An unwinder stopped at 0x3a8 learns that nothing is saved yet and the return address is still in x30. One stopped at 0x3ac or later learns where both were stored.

<figure class="vx-figure">
<svg viewBox="0 0 760 300" role="img" aria-label="The stack of _odd_frame before and after its first instruction, with the CFI row that describes each" aria-describedby="d1-cfa-desc">
<desc id="d1-cfa-desc">Two stack pictures side by side, addresses growing upward. Left, at offset 0 before stp: sp and the CFA are the same address; x29 and x30 are still in registers; the CFI row reads CFA = sp. Right, at offset 4 after stp x29, x30, [sp, #-32]!: sp has moved 32 bytes down; the CFA is still the old address, now sp + 32; the saved x29 sits at CFA - 32, which is sp, and the saved x30 at CFA - 24; the slot at CFA - 16 holds the spilled w0. The CFI row reads CFA = sp + 32, x29 at CFA - 32, x30 at CFA - 24.</desc>
<text class="vx-text" x="170" y="20" text-anchor="middle">at +0 (before stp)</text>
<text class="vx-text" x="560" y="20" text-anchor="middle">at +4 and later</text>
<rect class="vx-box" x="100" y="40" width="140" height="40" rx="2"/><text class="vx-text-muted" x="170" y="65" text-anchor="middle">caller's frame</text>
<line class="vx-line" x1="60" y1="80" x2="250" y2="80"/>
<text class="vx-text-accent" x="20" y="84">CFA</text>
<text class="vx-mono" x="256" y="84">= sp</text>
<rect class="vx-box" x="100" y="80" width="140" height="160" rx="2" stroke-dasharray="4 4"/>
<text class="vx-text-muted" x="170" y="165" text-anchor="middle">not yet claimed</text>
<g class="vx-seq" style="--vx-i: 0; --vx-n: 2">
<rect class="vx-box" x="490" y="40" width="140" height="40" rx="2"/><text class="vx-text-muted" x="560" y="65" text-anchor="middle">caller's frame</text>
<line class="vx-line" x1="440" y1="80" x2="640" y2="80"/>
<text class="vx-text-accent" x="400" y="84">CFA</text>
<text class="vx-mono" x="646" y="84">= sp + 32</text>
<rect class="vx-box" x="490" y="80" width="140" height="40" rx="2"/><text class="vx-mono" x="500" y="105">(unused)</text>
<rect class="vx-box" x="490" y="120" width="140" height="40" rx="2"/><text class="vx-mono" x="500" y="145">w0 at +16</text>
</g>
<g class="vx-seq" style="--vx-i: 1; --vx-n: 2">
<rect class="vx-box-accent" x="490" y="160" width="140" height="40" rx="2"/><text class="vx-mono" x="500" y="185">saved x30</text>
<text class="vx-mono" x="646" y="185">CFA - 24</text>
<rect class="vx-box-accent" x="490" y="200" width="140" height="40" rx="2"/><text class="vx-mono" x="500" y="225">saved x29</text>
<text class="vx-mono" x="646" y="225">CFA - 32 = sp</text>
</g>
<text class="vx-mono" x="20" y="275">row: CFA = sp</text>
<text class="vx-mono" x="400" y="275">row: CFA = sp+32, x29 [CFA-32], x30 [CFA-24]</text>
</svg>
<figcaption>Figure 4. What the two CFI rows of <code>_odd_frame</code> describe. The CFA does not move; sp moves away from it, and the rule for finding the CFA changes to keep pointing at the same place. Each saved register is found relative to the CFA, never relative to sp.</figcaption>
</figure>

### `.eh_frame` and `.debug_frame`

The same CIE and FDE design appears in two sections. `.debug_frame` is DWARF's own, for debuggers. `.eh_frame` is the copy the running program uses to unwind for exceptions, so it is kept in the shipped binary. Taylor describes the differences: an FDE in `.eh_frame` points at its CIE with a relative offset instead of a section offset, and the CIE's **augmentation** string adds fields, such as `R` for how the FDE's addresses are encoded[^taylor-ehframe]. The CIE above says `"zR"` with augmentation data `10`, which is `DW_EH_PE_pcrel`: the FDE's start address is stored relative to its own position[^taylor-ehframe].

How much code the table covers is a choice. Taylor notes that by default an FDE only has to be right where an exception can occur, at calls and throws, and that GCC's `-fasynchronous-unwind-tables` makes it cover every instruction so that a signal handler can unwind[^taylor-ehframe]. A profiler needs the second kind.

??? check "A sampling profiler walks the stack by following x29 alone. A sample lands on the first instruction of `_odd_frame`, before the `stp`. Which function goes missing from its backtrace, and what does the CFI row for that address tell an unwinder instead?"

    `_odd_frame`'s caller. At that instruction x29 still points at the caller's own frame record, so the walk starts from the caller's saved values and continues to the caller's caller; the caller's return address, which sits in x30 and nowhere in memory, is never read. The CFI row at offset 0 says CFA = sp with no registers saved, which tells an unwinder that the return address into the caller is still in x30.

## Apple's compact unwind, and when it falls back

Most functions have one of a few prologue shapes, and an FDE spends several bytes of opcodes to describe each one. Apple's toolchain instead gives each function a 32-bit **compact unwind encoding** that names its shape directly, and keeps DWARF for the functions whose shape the compact format cannot express[^compact-unwind]. The assembler writes one row per function into a `__compact_unwind` section. The linker turns those rows into a `__unwind_info` section, which libunwind's header calls a small and fast way for the runtime to find unwind information; for a function with only DWARF unwind information, that table holds the offset of its FDE in `__eh_frame`[^compact-unwind].

For arm64 the encoding's mode field has three common values[^compact-unwind]. **Frame mode** (0x04000000) is the standard prologue: x29 and x30 pushed, x29 pointing at them, and any callee-saved pairs stored right below in register order, one bit per pair. **Frameless mode** (0x02000000) is a leaf that saves no x29 and x30 and leaves the return address in x30, with its stack size in the encoding. **DWARF mode** (0x03000000) means "no compact encoding; the low 24 bits give the FDE's offset in `__eh_frame`".

`llvm-objdump --unwind-info` on the example's object file shows one encoding per function (same machine and date):

| Function | Prologue shape | Object file | Linked library |
| --- | --- | --- | --- |
| `_with_frame` | record at CFA − 16, CFA from x29 | `0x04000000` | `0x04000000` |
| `_odd_frame` | record at CFA − 32, CFA from sp | `0x03000000` | `0x03000014` |
| `_leaf` | nothing saved | `0x02000000` | `0x02000000` |

Only `_odd_frame` got an FDE, and in the linked library its encoding's low bits, 0x14, are the FDE's offset in `__eh_frame`: exactly where the dump above found it. The header describes DWARF mode as something only the linker writes; on this machine the assembler also wrote it, with a zero offset, and the linker filled the offset in. The encodings are derived from the CFI directives, not from the instructions: `_odd_frame` builds a perfectly good frame record, but its directives describe the CFA from sp with the record 32 bytes below it, a shape frame mode has no code for.

Changing `_with_frame` to also save x19 and x20 as a pair right below its frame record, with matching `.cfi_offset w19, -24` and `.cfi_offset w20, -32`, gives `0x04000001` when assembled on the same machine: frame mode with the bit for the x19/x20 pair set.

??? check "A back end emits `stp x29, x30, [sp, #-16]!`, then `mov x29, sp`, then `.cfi_def_cfa w29, 16` and the two `.cfi_offset` lines, and allocates its locals below that with `sub sp, sp, #48`. Which mode do you expect, and what would you run to find out?"

    Frame mode, 0x04000000: the frame record sits right below the CFA, x29 points at it, and the CFA is described from x29, which is the standard shape. Locals below the record do not matter, because an unwinder in frame mode needs only x29. Assemble the function and run `llvm-objdump --unwind-info` on the object file; if it prints 0x03000000, the directives describe some other shape, and `llvm-dwarfdump --eh-frame` shows which.

## Where debug information goes after the assembler

Unwind tables must reach the executable, because the running program reads them. DWARF debug sections need not, and on macOS they do not. Link `clamp.o` with a `main.o` into a program `prog` and the executable has `__unwind_info` and `__eh_frame` but no `__debug_*` sections at all. The DWARF stays in the object files. The linker writes only a **debug map** into the executable's symbol table: a list naming each object file that holds debug information for it[^dsymutil]. `dsymutil --dump-debug-map prog` prints it (paths shortened, timestamps removed):

```text
objects:
  - filename:        '<dir>/clamp.o'
    symbols:
      - { sym: _clamp, objAddr: 0x0, binAddr: 0x100000328, size: 0x64 }
  - filename:        '<dir>/main.o'
    symbols:
      - { sym: _main, objAddr: 0x0, binAddr: 0x10000038C, size: 0x2C }
```

Each symbol pairs an address in its object file with its final address in the executable. `dsymutil prog` follows the map, links the DWARF from each object file into one copy with those final addresses, and writes it into a **`.dSYM` bundle** next to the executable[^dsymutil]. A debugger can then show source lines for a shipped program after the build directory is gone. Without a `.dSYM`, lldb follows the debug map itself: on the same machine, `breakpoint set --file clamp.c --line 3` on `prog` resolved to `clamp + 24` while `clamp.o` was present, and failed to resolve once `clamp.o` was moved away.

## Line tables first, locations later

A compiler that goes through LLVM does not write `.loc` or `.cfi_*` itself. It attaches **debug metadata** to its IR, and LLVM's code generator writes the directives. LLVM's design goal is that debug information should have little impact on the rest of the compiler: no transformation or code generator should have to change because of it[^llvm-sld]. The example below is a function from a toy calculator language, with a compile unit, a subprogram for `twice` and one `!DILocation` per instruction.

--8<-- "includes/examples/backend/d1-debug-info/debug_metadata.ll.md"

`llc -O0 -mtriple=arm64-apple-macosx debug_metadata.ll` (LLVM 18.1.8) turns the metadata into these lines around the two instructions:

```text
	.file	0 "/toy" "calc.toy"
	.cfi_startproc
	.loc	0 3 14 prologue_end
	add	w0, w0, w0
	.loc	0 3 1 is_stmt 0
	ret
	.cfi_endproc
```

The first `.loc` also sets `prologue_end`, the flag that marks where a breakpoint on the function's entry should stop[^dwarf5]. The second carries `is_stmt 0`, since the `ret` continues line 3 rather than starting a new statement. Removing every `!dbg` attachment and the metadata, then compiling both versions with `llc -O2`, gives the same two instructions: `add w0, w0, w0` and `ret`. The debug information describes the code without changing it.

Everything so far rests on facts the compiler decides once: which source position each instruction came from, and where the prologue saved each register. A variable's location after optimization is different. It may live in x3 for five instructions, in a stack slot after a spill, and nowhere once its last use has passed. Describing that needs **location lists**, ranges of addresses each paired with an expression, kept correct through register allocation and spilling. That is why this chapter stops at `-O0`-style stack-slot locations, and why [C5](c5-spilling.md) returns to the question.

The connection to Vortex is close. [Stage 9](../compiler/guide/stage-9-runtime-safety.md#stopping-with-a-clear-runtime-error) already requires every runtime check to report `at <file>:<line>:<column>`, so the compiler already carries a source position to each checked operation. A line table is the same knowledge made available to tools outside the program: lldb, a profiler, a crash reporter.

## For Vortex

!!! vortex "Exercise"

    **Build** the smallest debug information that makes lldb useful on a compiled Vortex program: a line table for breakpoints and stepping, a subprogram entry per function for names, and call frame information for every prologue your back end emits.

    1. A line table. For each statement, emit a `.loc` directive (or, if your back end goes through LLVM IR, a `!DILocation` on each instruction) carrying the file, line and column your parser already recorded, the same positions stage 9's runtime errors print. Code your compiler invents that belongs to no statement, such as a shared bounds-failure path, gets line 0.
    2. Names and ranges. A compile unit and one `DW_TAG_subprogram` per function with its name and address range, either from LLVM metadata or through whatever your assembler route supports.
    3. Call frame information. Every prologue from your [A5](a5-stack-frames.md#for-vortex) frame layout gets `.cfi_startproc`, `.cfi_endproc`, and one directive after each instruction that changes the CFA rule or saves a register.

    **Not yet:** locations for variables at `-O1` and above (they need the register allocation and spill tracking from [C5](c5-spilling.md)); type entries beyond what a function signature needs; debug information for code built in memory by a JIT ([D2](d2-jit.md)); a `.dSYM` workflow beyond running `dsymutil` once by hand.

    **Proof that it works:**

    - `llvm-dwarfdump --verify` reports no errors on every object file your test suite produces.
    - For one function with a loop and an `if`, the rows from `llvm-dwarfdump --debug-line` match the statements' lines and columns, checked by hand against the source.
    - In lldb, `breakpoint set --file <name> --line <N>` on a statement inside a loop stops there on every iteration; `next` steps statement by statement through the function without stopping twice on one line.
    - A program that fails a bounds check three calls deep, stopped in lldb at your runtime's reporting function, shows every Vortex frame by name in `bt`, and `frame select` on each shows the line that stage 9's message printed.
    - `llvm-objdump --unwind-info` on your objects shows a frame-mode or frameless encoding for every function; for any function that shows 0x03000000, you can say from its `.cfi_*` lines which shape forced the fallback.
    - A table, filled in with your compiler's version and the date:

        | Test program | Functions | Encodings other than frame or frameless | `__debug_line` bytes | `__text` bytes |
        | --- | --- | --- | --- | --- |
        | stage 10 `multiply` | | | | |
        | the bounds-failure program above | | | | |

## Key ideas

!!! recap "Questions you can now answer"

    - **Why is a DWARF line table stored as a program rather than a list of rows?** Most instructions share a line with their neighbours, so a one-byte special opcode that moves `address` and `line` together and appends a row is far smaller than a row per instruction, and it can move `line` backwards when code is reordered.
    - **How do you decode a special opcode?** Subtract `opcode_base`; the quotient by `line_range` is the address step, and `line_base` plus the remainder is the line step.
    - **What decides whether two DIEs share an abbreviation code?** Their tag, their children flag and their exact list of (attribute, form) pairs; never their values.
    - **What does call frame information add to the frame record from A5?** A rule for every instruction address: where the CFA is and where each saved register lives, including callee-saved registers and the moments inside a prologue when the frame record is not yet built.
    - **What is the difference between a CIE and an FDE?** A CIE holds what many functions share (alignment factors, return-address column, opening rules); an FDE covers one function's address range and carries the opcodes that change the rules as its prologue runs.
    - **When does an Apple arm64 function fall back to DWARF unwind information?** When its CFI directives describe a shape the 32-bit compact encoding cannot express; the linked `__unwind_info` then holds the offset of its FDE in `__eh_frame`.
    - **Where is the DWARF for a macOS executable?** In the object files, found through the debug map in the executable's symbol table, until `dsymutil` links it into a `.dSYM` bundle.

## Where this comes back

!!! next "You will use this again in"

    - [B4. Linking and loading](b4-linking-and-loading.md): *what the linker keeps from each object file*, *the symbol table*
    - [C5. Spilling, splitting and rematerialization](c5-spilling.md): *location lists*, *why a spilled variable's location changes*
    - [D2. JIT compilation](d2-jit.md): *unwinding and debugging code that has no object file*
    - [D3. Reading real back ends](d3-real-backends.md): *a production compiler's CFI and line-table emission*
    - [E3. LLVM's allocator, scheduler and MC layer](e3-llvm-allocator-scheduler-mc.md): *how the MC layer turns `.loc` and `.cfi_*` into sections*
    - [P4. Seeing inside the CPU: counters and tools](../optimize/p4-counters-and-tools.md): *profilers that attribute samples to source lines*, *unwinding a sampled stack*

## Sources and further reading

Read Eager's introduction first: it walks through the DIE tree, abbreviations and the line-number state machine in plain language with pictures. Then read the DWARF 5 standard's section 6.2 (line numbers), 6.4 (call frame information) and 7.5.3 (abbreviations), which are short and exact. Taylor's post is the clearest account of how `.eh_frame` differs from `.debug_frame`. The compact unwind header is source code, but its comments define every arm64 mode.

[^dwarf5]: DWARF Debugging Information Format Committee, "DWARF Debugging Information Format, Version 5", 13 February 2017: section 2.6 and 4.1 (locations, and a variable with no location), 6.2 (line number information, including 6.2.5.1 special opcodes), 6.4 (call frame information), 7.5.3 (abbreviations tables), 7.6 (LEB128), table 7.29 (call frame instruction encodings). <https://dwarfstd.org/dwarf5std.html> (PDF: <https://dwarfstd.org/doc/DWARF5.pdf>)
[^eager]: Michael J. Eager, "Introduction to the DWARF Debugging Format", DWARF Standards Committee, April 2012. <https://dwarfstd.org/doc/Debugging-using-DWARF-2012.pdf>
[^llvm-sld]: LLVM Project, "Source Level Debugging with LLVM", sections "Philosophy behind LLVM debugging information" and "Debug information format". <https://llvm.org/docs/SourceLevelDebugging.html>
[^dsymutil]: LLVM Project, "dsymutil - manipulate archived DWARF debug symbol files", LLVM Command Guide, description and `--dump-debug-map`. <https://llvm.org/docs/CommandGuide/dsymutil.html>
[^taylor-ehframe]: Ian Lance Taylor, ".eh_frame", Airs, 10 January 2011. <https://www.airs.com/blog/archives/460>
[^compact-unwind]: LLVM Project, libunwind, `include/mach-o/compact_unwind_encoding.h`: the file comment, the arm64 modes, and the section on `__LD,__compact_unwind`. <https://github.com/llvm/llvm-project/blob/main/libunwind/include/mach-o/compact_unwind_encoding.h>
[^aadwarf64]: Arm, "DWARF for the Arm 64-bit Architecture (AADWARF64)", section "DWARF register names". <https://github.com/ARM-software/abi-aa/blob/main/aadwarf64/aadwarf64.rst>

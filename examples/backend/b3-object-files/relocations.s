// touches_globals(): one function that needs two kinds of help from the
// linker before it can run.
//
// `bl _vx_log_i32` calls a function this file never defines: its address is
// a hole the assembler leaves for the linker to fill once it has seen every
// object file. `adrp` / `add ... @PAGEOFF` computes the address of `_counts`
// in two pieces (a 4 KiB page, then an offset inside it), because AArch64
// has no single instruction that loads a 64-bit address as one immediate;
// both pieces are holes too, filled with the same address once `_counts`'s
// final page is known. Assembling this file (the only thing the check does)
// leaves all three holes open; nothing here runs, and nothing calls
// `vx_log_i32` for real.
//
// Apple's clang assembler spellings (`@PAGE`, `@PAGEOFF`) are Darwin-only;
// GNU as for Linux AArch64 spells the second piece `:lo12:_counts` instead
// and drops the leading underscore. This file is checked on macOS only.
//
// Follows: Apple, "Writing ARM64 code for Apple platforms", section
// "Access Global Variables Through PC-Relative Addressing"
// (https://developer.apple.com/documentation/xcode/writing-arm64-code-for-apple-platforms).

        .text
        .globl  _touches_globals
        .p2align 2
_touches_globals:
        bl      _vx_log_i32             // hole: needs vx_log_i32's address
        adrp    x1, _counts@PAGE        // hole: needs _counts's page
        add     x1, x1, _counts@PAGEOFF // hole: needs _counts's offset in it
        ldr     w0, [x1]
        ret

        .data
        .p2align 2
_counts:
        .word   0

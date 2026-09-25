// touches_globals(): one function that needs two kinds of help from the
// linker before it can run.
//
// `bl _vx_log_i32` calls a function this file never defines, so the
// assembler cannot know how far away it is. `adrp` and `add ... @PAGEOFF`
// build the address of `_counts` in two pieces, a 4 KiB page and an offset
// inside it. `_counts` is defined below, but in a different section, and
// only the linker decides how far `__DATA` will sit from `__TEXT`. So all
// three instructions leave holes, and the object file carries one relocation
// for each. Assembling this file is the whole check; nothing here runs.
//
// `@PAGE` and `@PAGEOFF` are Apple's spellings. GNU as for Linux writes the
// page as the plain symbol and the offset as `:lo12:counts`, and drops the
// leading underscore, so this file is checked on macOS only.
//
// Follows: HelloSilicon, README, the ADRP/@PAGE/@PAGEOFF note
// (https://github.com/below/HelloSilicon); Apple XNU,
// EXTERNAL_HEADERS/mach-o/arm64/reloc.h, for the relocation kinds.

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

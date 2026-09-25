// Follows: DWARF 5, section 6.2 (line number information), and LLVM's
// "Source Level Debugging with LLVM". A hand-written clamp(v, lo, hi):
// each .loc names the file, line and column of the instructions after
// it, and the assembler turns them into a __debug_line (.debug_line)
// section. Read it back with: llvm-dwarfdump --debug-line line_table.o
    .file 1 "clamp.c"
    .globl _clamp
    .p2align 2
_clamp:
    .loc 1 10 5
    cmp     w0, w1
    b.ge    1f
    .loc 1 11 9
    mov     w0, w1
    b       3f
1:
    .loc 1 13 5
    cmp     w0, w2
    b.le    2f
    .loc 1 14 9
    mov     w0, w2
    b       3f
2:
    .loc 1 16 5
3:
    ret

    .globl _count_word_char
    .p2align 2
_count_word_char:
    .cfi_startproc
    stp     x29, x30, [sp, #-32]!
    .cfi_def_cfa_offset 32
    .cfi_offset w30, -24
    .cfi_offset w29, -32
    mov     x29, sp
    str     w0, [sp, #16]
    bl      _classify_char
    ldr     w1, [sp, #16]
    add     w0, w1, w0
    ldp     x29, x30, [sp], #32
    ret
    .cfi_endproc

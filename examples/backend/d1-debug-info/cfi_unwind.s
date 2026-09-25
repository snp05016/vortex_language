// Follows: DWARF 5, section 6.4 (call frame information), and libunwind's
// compact_unwind_encoding.h. Three prologue shapes, one CFI description
// each. On Apple arm64 the assembler gives each function a 32-bit compact
// unwind encoding; only the shape the compact format cannot express also
// gets a DWARF FDE in __eh_frame. Inspect with:
//   llvm-objdump --unwind-info cfi_unwind.o
//   llvm-dwarfdump --eh-frame cfi_unwind.o
    .globl _with_frame
    .p2align 2
_with_frame:                    // the usual shape: frame record right below the CFA
    .cfi_startproc
    sub     sp, sp, #32
    stp     x29, x30, [sp, #16]
    add     x29, sp, #16
    .cfi_def_cfa w29, 16
    .cfi_offset w30, -8
    .cfi_offset w29, -16
    str     w0, [sp, #12]
    bl      _leaf
    ldr     w1, [sp, #12]
    add     w0, w1, w0
    ldp     x29, x30, [sp, #16]
    add     sp, sp, #32
    ret
    .cfi_endproc

    .globl _odd_frame
    .p2align 2
_odd_frame:                     // frame record 32 bytes below the CFA, tracked from sp
    .cfi_startproc
    stp     x29, x30, [sp, #-32]!
    .cfi_def_cfa_offset 32
    .cfi_offset w30, -24
    .cfi_offset w29, -32
    mov     x29, sp
    str     w0, [sp, #16]
    bl      _leaf
    ldr     w1, [sp, #16]
    add     w0, w1, w0
    ldp     x29, x30, [sp], #32
    ret
    .cfi_endproc

    .globl _leaf
    .p2align 2
_leaf:                          // calls nothing: the return address stays in x30
    .cfi_startproc
    add     w0, w0, #1
    ret
    .cfi_endproc

// sum_three_and_call(a, b, c) -> helper(a) + b + c + a*b
//
// helper() is a leaf: it calls nothing, so it needs no frame at all.
// sum_three_and_call() does call, so it must keep its own return address
// (bl overwrites x30), and it must keep b, c and a*b alive while helper()
// runs. Its frame is 48 bytes, listed from sp upward:
//
//   [sp, #0]    saved x29: the caller's frame pointer   } the frame record,
//   [sp, #8]    saved x30: this function's return address } x29 points here
//   [sp, #16]   the caller's x19, saved because b is about to live in x19
//   [sp, #24]   the caller's d8, saved because c is about to live in d8
//   [sp, #32]   a*b, spilled: 4 bytes used, 12 bytes of padding that keep
//               the frame a multiple of 16
//
// Three ways to survive a call: b in a callee-saved general register (x19),
// c in a callee-saved FP register (d8; AAPCS64 promises only its low 64
// bits, which is all a double needs), and a*b in memory this function owns.
//
// This layout puts the frame record at the bottom of the frame. AAPCS64
// leaves the record's position open; Apple clang puts it at the top of the
// saved-register area instead. Both are valid.
//
// Follows: AAPCS64, sections "General-purpose Registers", "SIMD and
// Floating-Point Registers" and "The Frame Pointer".

        .text
        .globl  helper
        .globl  _helper
        .p2align 2
helper:
_helper:
        add     w0, w0, #1              // a leaf: nothing to save, no frame
        ret

        .globl  sum_three_and_call
        .globl  _sum_three_and_call
        .p2align 2
sum_three_and_call:
_sum_three_and_call:
        stp     x29, x30, [sp, #-48]!   // claim 48 bytes and store the frame
        mov     x29, sp                 // record at the bottom; point x29 at it
        str     x19, [sp, #16]          // two register files, so two stores,
        str     d8,  [sp, #24]          // not one stp
        mul     w9, w0, w1              // product = a * b, computed before the
        str     w9, [sp, #32]           // call, spilled so it survives helper()
        mov     w19, w1                 // keep b live across the call, in x19
        scvtf   d8, w2                  // keep c live across the call, in d8
        bl      helper                  // overwrites x30; helper may clobber
                                        // x0-x18, but must return x19 and d8
        add     w0, w0, w19             // helper(a) + b
        fcvtzs  w9, d8
        add     w0, w0, w9              // + c
        ldr     w9, [sp, #32]
        add     w0, w0, w9              // + product
        ldr     x19, [sp, #16]          // restore from the same slots, while
        ldr     d8,  [sp, #24]          // the frame still belongs to us
        ldp     x29, x30, [sp], #48     // reload the record, release 48 bytes
        ret

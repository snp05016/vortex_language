// sum_three_and_call(a, b, c) -> helper(a) + b + c + a*b
//
// helper() is a leaf: it calls nothing, so it needs no frame at all.
// sum_three_and_call() does call, so it needs somewhere to keep b and c
// while helper() runs, and it must not lose its own return address when
// its own bl overwrites x30. Its frame, 48 bytes, sp upward:
//
//   [sp, #32]   product = a * b: a genuine local, computed before the call
//               and read again after it (only 4 of these 16 bytes are used;
//               the rest is padding that keeps the frame a multiple of 16)
//   [sp, #24]   d8:  the caller's value, saved because this function is
//               about to put c into d8 across the call
//   [sp, #16]   x19: the caller's value, saved because this function is
//               about to put b into x19 across the call
//   [sp, #8]    x30: the return address helper()'s own bl would overwrite
//   [sp, #0]    x29: the caller's frame pointer
//
// b survives the call in x19, a callee-saved general register. c survives
// it as a double in d8, a callee-saved FP/SIMD register of which AAPCS64
// guarantees only the low 64 bits. product survives it the third way, spilled
// to the stack, the technique every value uses once the callee-saved
// registers run out.
//
// Follows: AAPCS64 sections "Callee-saved" (r19-r29, v8-v15) and
// "The Frame Pointer"; Apple, "Writing ARM64 code for Apple platforms",
// section "The Frame Pointer".

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
        stp     x29, x30, [sp, #-48]!  // push the frame record, and reserve
        mov     x29, sp                // the rest of the frame in the same step
        str     x19, [sp, #16]         // save what this function is about to
        str     d8,  [sp, #24]         // clobber: a callee-saved GPR and FP reg
        mul     w9, w0, w1              // product = a * b, computed before the
        str     w9, [sp, #32]           // call, spilled so it survives helper()
        mov     w19, w1                 // keep b live across the call, in x19
        scvtf   d8, w2                  // keep c live across the call, in d8
        bl      helper                  // may clobber x0-x17; x29 and x30 are
                                         // safe, because they are in the frame
                                         // record, not in a register the callee
                                         // is free to use
        add     w0, w0, w19             // helper(a) + b
        fcvtzs  w9, d8
        add     w0, w0, w9              // + c
        ldr     w9, [sp, #32]
        add     w0, w0, w9              // + product
        ldr     x19, [sp, #16]          // restore what we saved, in reverse
        ldr     d8,  [sp, #24]
        ldp     x29, x30, [sp], #48     // pop the frame record, deallocate
        ret

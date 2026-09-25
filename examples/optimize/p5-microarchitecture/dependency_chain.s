// Two loops that do the same kind of work, one multiply and one add per
// output element per trip, with one loop-carried chain and with four.
// Compile-checked only: assembled, never run or timed here. The
// LLVM-MCA-BEGIN and LLVM-MCA-END comments mark each loop body as a region,
// so `llvm-mca -mcpu=<core> dependency_chain.s` reports each loop separately.
//
// dot_one_chain(a = x0, b = x1, n = x2 > 0) returns sum of a[k] * b[k] in s0.
// Each fadd needs the previous fadd's result: one chain through s0. The fmul
// is off the chain, because the next trip's product does not wait for it.
//
// four_columns(a = x0, b = x1, n = x2 > 0, c = x3) computes four dot
// products at once: c[j] = sum over k of a[k] * b[k][j], j = 0..3, where b
// has rows of four floats. Each c[j] is still summed in k order, one rounded
// multiply and one rounded add at a time, so every result has the same bits
// as a separate dot_one_chain call; the four sums are four chains that never
// read each other.
//
// Follows: LLVM, "llvm-mca - LLVM Machine Code Analyzer", section "Using
// Markers"; AAPCS64 for the argument and result registers.

        .text
        .globl  dot_one_chain
        .globl  _dot_one_chain
        .p2align 2
dot_one_chain:
_dot_one_chain:
        fmov    s0, wzr                 // sum = 0.0
# LLVM-MCA-BEGIN one_chain
one_chain_loop:
        ldr     s1, [x0], #4            // a[k], then advance a
        ldr     s2, [x1], #4            // b[k], then advance b
        fmul    s1, s1, s2              // rounded product
        fadd    s0, s0, s1              // rounded sum: waits for the last fadd
        subs    x2, x2, #1
        b.ne    one_chain_loop
# LLVM-MCA-END
        ret

        .globl  four_columns
        .globl  _four_columns
        .p2align 2
four_columns:
_four_columns:
        fmov    s16, wzr                // four sums, each 0.0
        fmov    s17, wzr
        fmov    s18, wzr
        fmov    s19, wzr
# LLVM-MCA-BEGIN four_columns
four_columns_loop:
        ldr     s1, [x0], #4            // a[k], shared by all four columns
        ldp     s2, s3, [x1]            // b[k][0], b[k][1]
        ldp     s4, s5, [x1, #8]        // b[k][2], b[k][3]
        add     x1, x1, #16             // next row of b
        fmul    s2, s1, s2
        fmul    s3, s1, s3
        fmul    s4, s1, s4
        fmul    s5, s1, s5
        fadd    s16, s16, s2            // four chains: none reads another's sum
        fadd    s17, s17, s3
        fadd    s18, s18, s4
        fadd    s19, s19, s5
        subs    x2, x2, #1
        b.ne    four_columns_loop
# LLVM-MCA-END
        stp     s16, s17, [x3]          // c[0], c[1]
        stp     s18, s19, [x3, #8]      // c[2], c[3]
        ret

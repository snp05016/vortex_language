// The innermost loop of matrix multiplication in two loop orders, written
// by hand so that the instructions are fixed. Compile-checked only:
// assembled, never run or timed here. The LLVM-MCA-BEGIN and LLVM-MCA-END
// comments mark each loop body as a region for `llvm-mca`.
//
// ijk_inner(a = x0, b = x1, n = x2 > 0, stride = x3) returns in s0 the sum
// over k of a[k] * b[k * stride]: a row of A times a column of B. The step
// from one element of the column to the next is `stride` bytes, and it
// lives in a register, so these instructions are the same bytes whether
// the matrix is 64 or 1024 columns wide.
//
// ikj_inner(c = x0, b = x1, n = x2 > 0, a_ik = s0) does c[j] += a_ik * b[j]
// for j = 0 .. n-1: one element of A times a row of B, added into a row of
// C. Every access moves 4 bytes forward.
//
// Each multiply and add is rounded separately (no fused multiply-add), and
// every c[j] still receives its products in increasing k, so both orders
// give the same bits.
//
// Follows: LLVM, "llvm-mca - LLVM Machine Code Analyzer", section "Using
// Markers"; AAPCS64 for the argument and result registers.

        .text
        .globl  ijk_inner
        .globl  _ijk_inner
        .p2align 2
ijk_inner:
_ijk_inner:
        fmov    s0, wzr                 // sum = 0.0
# LLVM-MCA-BEGIN ijk
ijk_loop:
        ldr     s1, [x0], #4            // a[k]: the next 4 bytes
        ldr     s2, [x1]                // b[k][j]: one element of a column
        add     x1, x1, x3              // down one row of B: stride bytes
        fmul    s1, s1, s2
        fadd    s0, s0, s1              // sum waits for the previous add
        subs    x2, x2, #1
        b.ne    ijk_loop
# LLVM-MCA-END
        ret

        .globl  ikj_inner
        .globl  _ikj_inner
        .p2align 2
ikj_inner:
_ikj_inner:
# LLVM-MCA-BEGIN ikj
ikj_loop:
        ldr     s1, [x1], #4            // b[k][j]: the next 4 bytes of a row
        ldr     s2, [x0]                // c[i][j]
        fmul    s1, s0, s1              // a_ik * b[k][j]
        fadd    s2, s2, s1              // no chain: each j has its own c
        str     s2, [x0]                // store c[i][j]
        add     x0, x0, #4              // move to c[i][j+1]
        subs    x2, x2, #1
        b.ne    ikj_loop
# LLVM-MCA-END
        ret

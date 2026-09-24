// Two ways to sum four products of 8 floats pointed to by x0: one running
// accumulator (chain_single), and four independent accumulators combined at
// the end (chain_four). Compile-checked only (assembled, not run, not timed):
// this file exists to be read, and to be fed to a static analyzer such as
// llvm-mca or measured on real hardware, the way this chapter's "reference
// shelf" describes. chain_single makes every fmadd wait for the one before
// it: on a core whose FMA latency is greater than its reciprocal throughput,
// that chain cannot issue faster than once per latency cycles. chain_four
// gives the same core four unrelated chains to interleave, so it can issue
// on every cycle its ports allow instead of waiting on one dependency.
//
// Follows: Apple, "Tuning your code's performance for Apple silicon", on
// giving the processor independent work instead of one long dependency
// chain; AAPCS64 for the calling convention.

        .text
        .globl  chain_single
        .globl  _chain_single
        .p2align 2
chain_single:
_chain_single:
        fmov    s0, wzr         // accumulator, starts at 0.0
        ldp     s1, s2, [x0]        // p[0], p[1]
        fmadd   s0, s1, s2, s0      // s0 = s0 + p[0]*p[1], waits for the fmov
        ldp     s1, s2, [x0, #8]    // p[2], p[3]
        fmadd   s0, s1, s2, s0      // waits for the fmadd above
        ldp     s1, s2, [x0, #16]   // p[4], p[5]
        fmadd   s0, s1, s2, s0      // waits for the fmadd above
        ldp     s1, s2, [x0, #24]   // p[6], p[7]
        fmadd   s0, s1, s2, s0      // waits for the fmadd above: one long chain
        ret

        .globl  chain_four
        .globl  _chain_four
        .p2align 2
chain_four:
_chain_four:
        fmov    s0, wzr         // four accumulators, each starts at 0.0
        fmov    s1, wzr
        fmov    s2, wzr
        fmov    s3, wzr
        ldp     s4, s5, [x0]         // p[0], p[1]
        fmadd   s0, s4, s5, s0       // depends only on this load, not on s1..s3
        ldp     s4, s5, [x0, #8]     // p[2], p[3]
        fmadd   s1, s4, s5, s1       // independent of the fmadd above
        ldp     s4, s5, [x0, #16]    // p[4], p[5]
        fmadd   s2, s4, s5, s2       // independent of both fmadds above
        ldp     s4, s5, [x0, #24]    // p[6], p[7]
        fmadd   s3, s4, s5, s3       // independent of all three fmadds above
        fadd    s0, s0, s1           // only now do the four chains meet
        fadd    s2, s2, s3
        fadd    s0, s0, s2
        ret

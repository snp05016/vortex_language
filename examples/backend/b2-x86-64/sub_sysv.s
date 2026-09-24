// int sub_ints(int a, int b): the x86-64 System V counterpart of A2's
// add_ints, written to subtract instead of add on purpose.
//
// Addition can dodge the cost of a two-operand machine: clang turns
// "a + b" into one instruction, leal (%rdi,%rsi), %eax, using an
// addressing-mode trick that happens to compute a sum. Subtraction is not
// commutative, so no such trick applies, and the real shape of a
// destructive instruction set shows through: the destination must already
// hold one operand, so the first argument is copied into the result
// register before the operation.
//
// The System V AMD64 ABI passes the first two integer arguments in edi and
// esi and returns an int result in eax, so this file has no prologue, no
// stack frame and no calls: two instructions do the whole job.
//
// GNU as for ELF wants no leading underscore on C symbol names (Mach-O
// wants one; see A2's add.s). .type and .size are conventional on ELF and
// are not read by Apple's assembler at all.

        .text
        .globl  sub_ints
        .type   sub_ints, @function
        .p2align 4
sub_ints:
        movl    %edi, %eax     // eax = a: the destination must hold an operand first
        subl    %esi, %eax     // eax -= b: subl is destructive, unlike AArch64's sub
        ret
        .size   sub_ints, . - sub_ints

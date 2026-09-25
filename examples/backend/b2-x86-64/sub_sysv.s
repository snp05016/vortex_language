# int sub_ints(int a, int b): the x86-64 System V counterpart of A2's
# add_ints, written to subtract instead of add on purpose.
#
# Addition can dodge the cost of a two-operand machine: clang turns
# "a + b" into one leal (%rdi,%rsi), %eax, an address computation that
# happens to be a sum. Subtraction has no such trick, so the shape of a
# destructive instruction set shows through: the destination must already
# hold the left operand before subl overwrites it.
#
# System V AMD64 passes the first two integer arguments in edi and esi and
# returns an int in eax, so this leaf needs no frame and no calls.
#
# ELF symbol names have no leading underscore (Mach-O adds one; see A2's
# add.s). .type and .size are ELF directives. Comments use '#', GNU as's
# comment character on x86 targets.

        .text
        .globl  sub_ints
        .type   sub_ints, @function
        .p2align 4
sub_ints:
        movl    %edi, %eax      # eax = a: the destination must hold an operand first
        subl    %esi, %eax      # eax = eax - esi: the old eax is gone
        ret
        .size   sub_ints, . - sub_ints

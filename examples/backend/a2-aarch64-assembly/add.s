// int add_ints(int a, int b): the smallest complete assembly file.
//
// The procedure call standard puts the first two int arguments in w0 and w1
// and expects an int result in w0, so the whole body is one add and a return.
//
// C code that calls add_ints needs the symbol _add_ints on macOS (Mach-O adds
// a leading underscore to C names) and add_ints on Linux (ELF does not). Two
// labels at the same address let this one file serve both.
//
// Two slashes start a comment in both assemblers. Avoid the semicolon: clang's
// Mach-O listings use it for comments, but GNU as for AArch64 reads it as the
// end of a statement.

        .text                   // the bytes that follow are code
        .globl  add_ints        // without .globl the names stay private to
        .globl  _add_ints       // this file and a caller could not link to them
        .p2align 2              // every instruction is 4 bytes: start at 2^2
add_ints:
_add_ints:
        add     w0, w0, w1      // w registers: a 32-bit add, as int needs
        ret                     // branch to the address bl left in x30

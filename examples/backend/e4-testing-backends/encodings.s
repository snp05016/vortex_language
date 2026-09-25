// An encoding test in the style of LLVM's MC tests. lit runs the RUN line;
// llvm-mc prints each instruction with its bytes, and FileCheck compares
// them with the CHECK lines. To an assembler every line starting with //
// is a comment, so the file also assembles as ordinary code.
//
// RUN: llvm-mc -triple=aarch64 --show-encoding < %s | FileCheck %s

    .text
    .p2align 2

// A 12-bit immediate sits in bits 21 to 10, Rn in bits 9 to 5, Rd in 4 to 0.
    add     x0, x1, #16
// CHECK: add x0, x1, #16 // encoding: [0x20,0x40,0x00,0x91]

// 4096 does not fit in 12 bits; the assembler shifts 1 left by 12 instead,
// and prints the form it chose.
    add     x0, x0, #4096
// CHECK: add x0, x0, #1, lsl #12 // encoding: [0x00,0x04,0x40,0x91]

// A scaled register-offset load, the form array indexing uses.
    ldr     w0, [x8, x2, lsl #2]
// CHECK: ldr w0, [x8, x2, lsl #2] // encoding: [0x00,0x79,0x62,0xb8]

    csel    w0, w2, w8, gt
// CHECK: csel w0, w2, w8, gt // encoding: [0x40,0xc0,0x88,0x1a]

    ret
// CHECK: ret // encoding: [0xc0,0x03,0x5f,0xd6]

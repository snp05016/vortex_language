// Four ways to reach one element of an array of f32, all starting from
// the same base register x0 (the array's address) and, where used, the
// same index register x1 (an element index already in bounds). None of
// this runs; the check only assembles it, which is enough to show that
// each addressing mode is real AArch64 syntax, not prose.
//
// Follows: Arm A64 ISA (DDI 0602), "Index by Encoding": LDR (immediate),
// LDR (register), and the pre-index/post-index addressing forms.

.text
.globl _addressing_modes
_addressing_modes:
    // Base + immediate offset: a[4], with the byte offset (16 = 4 * 4)
    // folded into the instruction. The base register is unchanged after.
    ldr s0, [x0, #16]

    // Base + scaled register offset: a[i], with i in x1. The shift by 2
    // multiplies the index by the element size, 4 bytes for f32.
    ldr s0, [x0, x1, lsl #2]

    // Pre-index: move the base to a[1] first (x0 += 4), then load from
    // the new address. Useful when a loop walks forward one element at
    // a time and keeps using the same base register.
    ldr s0, [x0, #4]!

    // Post-index: load from the current address, then move the base to
    // a[1] (x0 += 4) afterward. The load sees the old address.
    ldr s0, [x0], #4

    ret

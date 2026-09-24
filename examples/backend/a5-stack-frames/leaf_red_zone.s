// average_red_zone(a, b, c) -> (a + b + c) / 3, spilling two temporaries
// below sp instead of adjusting sp to make room for them.
//
// This function calls nothing, so it is free to leave sp untouched: nothing
// else, not even a signal handler, may write to the 128 bytes below it while
// it runs. Apple's arm64 ABI reserves exactly that: the red zone. A function
// that does call something must give up the red zone first, because the
// callee is free to use its own and the two would collide.
//
// AAPCS64, the ABI Linux arm64 uses, makes no such promise: nothing in it
// says the bytes below sp are safe to touch, which is why this file is
// gated to macos-arm64 only. A leaf function compiled for Linux arm64 that
// needs scratch space still has to move sp, or keep everything in registers.
//
// stur and ldur are the unscaled load/store forms: the offsets here are
// negative, so the plain, scaled ldr/str cannot encode them.
//
// Follows: Apple, "Writing ARM64 code for Apple platforms", section
// "Respect the stack's red zone".

        .text
        .globl  average_red_zone
        .globl  _average_red_zone
        .p2align 2
average_red_zone:
_average_red_zone:
        stur    w0, [sp, #-4]   // spill a and b into the red zone: no frame,
        stur    w1, [sp, #-8]   // because sp itself never moves
        ldur    w9, [sp, #-4]
        ldur    w10, [sp, #-8]
        add     w9, w9, w10
        add     w9, w9, w2       // a + b + c
        mov     w10, #3
        sdiv    w0, w9, w10      // (a + b + c) / 3
        ret

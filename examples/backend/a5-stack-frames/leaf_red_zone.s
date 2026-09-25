// average_red_zone(a, b, c) -> (a + b + c) / 3, spilling two temporaries
// below sp instead of adjusting sp to make room for them.
//
// Apple's arm64 ABI promises that the system does not modify the 128 bytes
// just below sp, the red zone, so a function that calls nothing can keep
// temporaries there without moving sp. A function that calls something
// cannot: the callee starts at the same sp and may use those bytes itself.
//
// AAPCS64 forbids any access below sp, so this file is checked on
// macos-arm64 only. On Linux arm64 a leaf that needs scratch memory moves sp.
// Nothing here needs memory at all (three registers would do); the spills
// exist only to show the addressing.
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

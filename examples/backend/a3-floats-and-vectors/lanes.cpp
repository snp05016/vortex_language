// Summing eight floats two ways. sum_serial adds them one at a time into s0,
// in the order written. sum_lanes keeps four partial sums, one per lane of
// v0, and combines them at the end with faddp (add pairwise): first lane 0 +
// lane 1 and lane 2 + lane 3, then those two. Same numbers, same kind of
// instruction, different grouping, and floating-point addition is not
// associative, so the two answers differ.
//
// The data is 2^24 followed by seven 1s. At 2^24 a float can step only by 2,
// so 2^24 + 1 rounds back to 2^24 (the tie goes to the even neighbour).
// The exact sum is 16777223.

#if !defined(__aarch64__)
#error "This example is AArch64 assembly: build it for an arm64 target."
#endif

#include <print>

asm(R"(
        .text
        .p2align 2
        .globl  sum_serial
        .globl  _sum_serial
sum_serial:                             // (const float *p, long n) -> float
_sum_serial:
        movi    d0, #0                  // s0 = 0.0 (and the rest of v0)
1:      ldr     s1, [x0], #4
        fadd    s0, s0, s1              // one rounding per element, in order
        subs    x1, x1, #1
        b.ne    1b
        ret

        .p2align 2
        .globl  sum_lanes
        .globl  _sum_lanes
sum_lanes:                              // (const float *p, long n), n % 4 == 0
_sum_lanes:
        movi    v0.4s, #0               // four partial sums
1:      ldr     q1, [x0], #16
        fadd    v0.4s, v0.4s, v1.4s     // lane j adds elements j, j + 4, ...
        subs    x1, x1, #4
        b.ne    1b
        faddp   v0.4s, v0.4s, v0.4s     // (l0 + l1), (l2 + l3), ...
        faddp   s0, v0.2s               // (l0 + l1) + (l2 + l3)
        ret
)");

extern "C" float sum_serial(const float *p, long n);
extern "C" float sum_lanes(const float *p, long n);

int main() {
  const float data[8] = {0x1p24f, 1, 1, 1, 1, 1, 1, 1};
  std::println("serial: {}", sum_serial(data, 8));
  std::println("lanes:  {}", sum_lanes(data, 8));
}

// The same a * b + c two ways, as the processor sees it: fmul then fadd (two
// roundings) and one fmadd (one rounding). The inputs are chosen so that the
// exact product lies exactly halfway between two floats: rounding it on its
// own loses the low bit that fmadd keeps.
//
// Then the four fused scalar forms, on small integers so the signs are easy to
// read. For all four, the operands are written Sd, Sn, Sm, Sa: the product
// Sn * Sm, and the addend Sa last.
//
// f32 arguments arrive in s0, s1 and s2 and the result leaves in s0.

#if !defined(__aarch64__)
#error "This example is AArch64 assembly: build it for an arm64 target."
#endif

#include <print>

asm(R"(
        .text
        .p2align 2
        .globl  separate
        .globl  _separate
separate:                               // (a, b, c) -> c + a * b, two roundings
_separate:
        fmul    s0, s0, s1              // a * b, rounded to float
        fadd    s0, s2, s0              // c + that, rounded again
        ret

        .p2align 2
        .globl  fused_madd
        .globl  _fused_madd
fused_madd:                             // c + a * b, one rounding
_fused_madd:
        fmadd   s0, s0, s1, s2
        ret

        .p2align 2
        .globl  fused_msub
        .globl  _fused_msub
fused_msub:                             // c - a * b
_fused_msub:
        fmsub   s0, s0, s1, s2
        ret

        .p2align 2
        .globl  fused_nmadd
        .globl  _fused_nmadd
fused_nmadd:                            // -(a * b) - c
_fused_nmadd:
        fnmadd  s0, s0, s1, s2
        ret

        .p2align 2
        .globl  fused_nmsub
        .globl  _fused_nmsub
fused_nmsub:                            // a * b - c
_fused_nmsub:
        fnmsub  s0, s0, s1, s2
        ret
)");

extern "C" float separate(float a, float b, float c);
extern "C" float fused_madd(float a, float b, float c);
extern "C" float fused_msub(float a, float b, float c);
extern "C" float fused_nmadd(float a, float b, float c);
extern "C" float fused_nmsub(float a, float b, float c);

int main() {
  const float a = 0x1.001p+0f;   // 1 + 2^-12
  const float c = -0x1.002p+0f;  // -(1 + 2^-11)
  std::println("fmul, fadd: {:a}", separate(a, a, c));
  std::println("fmadd:      {:a}", fused_madd(a, a, c));

  std::println("a = 2, b = 3, c = 1:");
  std::println("  fmadd  {:>3}", fused_madd(2, 3, 1));
  std::println("  fmsub  {:>3}", fused_msub(2, 3, 1));
  std::println("  fnmadd {:>3}", fused_nmadd(2, 3, 1));
  std::println("  fnmsub {:>3}", fused_nmsub(2, 3, 1));
}

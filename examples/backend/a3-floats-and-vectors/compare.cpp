// Where a float leaves the floating-point side: a comparison writes the
// integer condition flags, and a conversion writes a general-purpose register.
//
// fcmp_flags runs fcmp and returns N, Z, C and V. A comparison has four
// outcomes, not three: when either operand is NaN the pair is "unordered",
// and fcmp reports that as N=0 Z=0 C=1 V=1. The table shows which condition
// codes hold for each outcome. "lt" (N differs from V) is true for unordered,
// so a compiler uses "mi" (N set) for a < b, which is false when a NaN is
// involved.
//
// to_i32 runs fcvtzs, which rounds toward zero. The instruction never fails:
// out-of-range values saturate to the nearest i32 and NaN gives 0, so a
// language that must reject such casts needs its own check first.

#if !defined(__aarch64__)
#error "This example is AArch64 assembly: build it for an arm64 target."
#endif

#include <cstdint>
#include <limits>
#include <print>

asm(R"(
        .text
        .p2align 2
        .globl  fcmp_flags
        .globl  _fcmp_flags
fcmp_flags:                             // (float a, float b) -> NZCV
_fcmp_flags:
        fcmp    s0, s1
        mrs     x0, nzcv                // N, Z, C and V are bits 31 to 28
        lsr     x0, x0, #28
        ret

        .p2align 2
        .globl  to_i32
        .globl  _to_i32
to_i32:                                 // (float) -> int32, toward zero
_to_i32:
        fcvtzs  w0, s0
        ret
)");

extern "C" std::uint32_t fcmp_flags(float a, float b);
extern "C" std::int32_t to_i32(float x);

int main() {
  const float nan = std::numeric_limits<float>::quiet_NaN();
  const float inf = std::numeric_limits<float>::infinity();
  struct Case { const char *name; float a, b; };
  const Case cases[] = {{"1 vs 2", 1, 2}, {"2 vs 2", 2, 2}, {"3 vs 2", 3, 2}, {"NaN vs 2", nan, 2}};

  std::println("{:<9} N Z C V  a<b  mi   lt   ge   ls", "fcmp");
  for (const Case &k : cases) {
    const std::uint32_t f = fcmp_flags(k.a, k.b);
    const bool n = f & 8, z = f & 4, c = f & 2, v = f & 1;
    auto yn = [](bool x) { return x ? "yes" : "no"; };
    std::println("{:<9} {:d} {:d} {:d} {:d}  {:<4} {:<4} {:<4} {:<4} {}", k.name, n, z, c, v,
                 yn(k.a < k.b), yn(n), yn(n != v), yn(n == v), yn(!c || z));
  }

  std::println("fcvtzs w0, s0:");
  const float inputs[] = {2.75f, -2.75f, 3.0e9f, -3.0e9f, inf, nan};
  for (const float x : inputs) std::println("  {:>7} -> {}", x, to_i32(x));
}

// One dot product written three times, compiled with Clang's default
// -ffp-contract=on. Under that setting, sum += a[k] * b[k] may become one
// fused multiply-add (one rounding) or stay a multiply and an add (two),
// and the choice is left to the code generator. Other passes can decide it.
//
// Follows: Clang Compiler User's Manual, -ffp-contract; LLVM Language
// Reference, llvm.fmuladd; Clang Language Extensions, #pragma clang fp
// contract and #pragma clang loop.

#include <bit>
#include <cstdint>
#include <print>

constexpr int n = 64;

[[gnu::noinline]] float dot_scalar(const float* a, const float* b) {
  float sum = 0.0f;
#pragma clang loop vectorize(disable) interleave(disable)
  for (int k = 0; k < n; ++k) sum += a[k] * b[k];
  return sum;
}

[[gnu::noinline]] float dot_default(const float* a, const float* b) {
  float sum = 0.0f;
  for (int k = 0; k < n; ++k) sum += a[k] * b[k];
  return sum;
}

[[gnu::noinline]] float dot_strict(const float* a, const float* b) {
#pragma clang fp contract(off)
  float sum = 0.0f;
  for (int k = 0; k < n; ++k) sum += a[k] * b[k];
  return sum;
}

int main() {
  float a[n], b[n];
  std::uint32_t s = 12345;
  auto next = [&s] {  // a fixed integer sequence: the same inputs every run
    s = s * 1103515245u + 12345u;
    return static_cast<float>(s >> 8) / 16777216.0f;
  };
  for (int k = 0; k < n; ++k) {
    a[k] = next();
    b[k] = next();
  }

  auto show = [](const char* name, float x) {
    std::println("{:<22} {:a}  bits {:08x}", name, x,
                 std::bit_cast<std::uint32_t>(x));
  };
  show("vectorizing disabled", dot_scalar(a, b));
  show("default", dot_default(a, b));
  show("contract(off)", dot_strict(a, b));
}

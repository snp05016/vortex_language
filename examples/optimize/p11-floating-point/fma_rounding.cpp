// a*b+c computed two ways: as two separate f32 roundings (one for the
// multiply, one for the add), and as a fused multiply-add, one rounding for
// the whole expression. IEEE 754 defines fusedMultiplyAdd to round only the
// final result, as if the product were computed with unbounded range and
// precision first.
//
// Follows: IEEE 754-2019 (fusedMultiplyAdd); cppreference,
// std::fma. <https://en.cppreference.com/w/cpp/numeric/math/fma>

#include <bit>
#include <cmath>
#include <cstdint>
#include <print>

std::uint32_t ulps(float x, float y) {
  auto bx = std::bit_cast<std::int32_t>(x), by = std::bit_cast<std::int32_t>(y);
  return static_cast<std::uint32_t>(bx > by ? bx - by : by - bx);
}

int main() {
  float a = std::nextafter(1.0f, 2.0f);  // the f32 just above 1.0
  float b = std::nextafter(a, 2.0f);     // the f32 just above that
  float c = -a;

  volatile float p = a * b;          // rounds on its own, one instruction
  float two_step = p + c;            // a second, independent rounding
  float one_step = std::fma(a, b, c);  // one rounding for the whole expression

  std::println("a*b + c, two roundings = {:a}", two_step);
  std::println("fma(a, b, c), one rounding = {:a}", one_step);
  std::println("equal: {}", two_step == one_step);
  std::println("ulps apart: {}", ulps(two_step, one_step));
}

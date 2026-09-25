// Real addition is associative: (a+b)+c always equals a+(b+c). Each f32
// addition below rounds its result once, to the nearest value f32 can hold,
// so the two groupings can round differently and land on different floats.
//
// Follows: Goldberg, "What Every Computer Scientist Should Know About
// Floating-Point Arithmetic", ACM Computing Surveys 23(1), 1991 (rounding
// error and the failure of familiar algebraic laws under rounding).

#include <print>

int main() {
  float a = 16777216.0f;   // 2^24: f32's mantissa holds 24 bits (23 stored,
  float b = 1.0f;          // 1 implicit), so this is the first integer whose
  float c = -16777216.0f;  // successor, 16777217, is not exactly representable

  float left = (a + b) + c;   // a + b rounds to a (a tie, broken to even)
  float right = a + (b + c);  // b + c is exact: -16777215.0

  std::println("(a + b) + c = {}", left);
  std::println("a + (b + c) = {}", right);
  std::println("equal: {}", left == right);
}

// Two ways a matrix unit can change the bits of an f32 dot product.
//
// 1. Narrowed inputs. tf32 keeps f32's exponent range with at least 10
//    mantissa bits; bf16 keeps 7. The program rounds each f32 input to that
//    many bits (to nearest; the inputs are chosen so that no value sits
//    exactly halfway, which keeps the result independent of the tie rule),
//    then multiplies and sums in f32, in order.
// 2. Accumulation order. PTX leaves the order in which mma adds its products
//    unspecified. The same 16 values summed left to right and as a pairwise
//    tree give different f32 answers.
//
// Follows: https://docs.nvidia.com/cuda/parallel-thread-execution/index.html#alternate-floating-point-data-formats
//          https://docs.nvidia.com/cuda/parallel-thread-execution/index.html#warp-level-matrix-instructions-mma

#include <bit>
#include <cstdint>
#include <print>
#include <span>
#include <vector>

// Keep `bits` of f32's 23 mantissa bits, rounding to nearest. Finite, normal
// inputs only; enough for this demonstration.
float narrow(float x, int bits) {
  std::uint32_t u = std::bit_cast<std::uint32_t>(x);
  int drop = 23 - bits;
  std::uint32_t half = 1u << (drop - 1);
  u = (u + half) & ~((1u << drop) - 1);
  return std::bit_cast<float>(u);
}

float dot16(float a, int bits) {  // a[k] = a, b[k] = 1, k = 0..15
  float sum = 0.0f;
  for (int k = 0; k < 16; ++k) sum += (bits < 23 ? narrow(a, bits) : a) * 1.0f;
  return sum;
}

float sequential(std::span<const float> v) {
  float s = 0.0f;
  for (float x : v) s += x;
  return s;
}

float pairwise(std::span<const float> v) {
  if (v.size() == 1) return v[0];
  std::size_t h = v.size() / 2;
  return pairwise(v.first(h)) + pairwise(v.subspan(h));
}

int main() {
  std::println("input a[k]         strict f32    tf32 inputs   bf16 inputs");
  const float twelve = 1.0f / 4096.0f, nine = 1.0f / 512.0f;
  for (float a : {1.0f + twelve, 1.0f + nine, 1.0f + 3.0f * nine})
    std::println("{:.9f}   {:.8f}   {:.8f}   {:.8f}", a, dot16(a, 23), dot16(a, 10), dot16(a, 7));

  std::vector<float> v(16, 1.0f);  // 2^24, fourteen ones, -2^24: exact sum 14
  v.front() = 16777216.0f;
  v.back() = -16777216.0f;
  std::println("same 16 values: left to right = {}, pairwise tree = {}, exact = 14",
               sequential(v), pairwise(v));
}

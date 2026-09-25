// Before a tuner compares candidates by speed, every candidate must print
// the same bits. This sweeps kc, the length of the k panel, for one element
// of c = a * b under the two micro-kernel forms P12 describes, and counts
// how many different f32 results each form can produce.
//
// C-initialized: the running sum continues from one panel to the next, so
//   every kc performs the naive loop's additions in the naive loop's order.
// Zero-initialized: each panel is summed from zero and then added to c, so
//   kc decides where the sum is cut into groups.
//
// Follows: Low et al., "Analytical Modeling Is Enough for High-Performance
// BLIS", ACM TOMS 43(2), 2016, section 4.3 (kc as a tuning parameter), and
// IEEE 754 addition, which rounds after every operation.

#include <algorithm>
#include <bit>
#include <cstdint>
#include <print>
#include <set>
#include <vector>

constexpr int k_len = 512;

float c_initialized(const std::vector<float>& a, const std::vector<float>& b, int kc) {
  float c = 0.0f;
  for (int pc = 0; pc < k_len; pc += kc) {
    float acc = c;  // load c into the accumulator
    for (int k = pc; k < std::min(pc + kc, k_len); ++k) acc += a[k] * b[k];
    c = acc;  // store it back
  }
  return c;
}

float zero_initialized(const std::vector<float>& a, const std::vector<float>& b, int kc) {
  float c = 0.0f;
  for (int pc = 0; pc < k_len; pc += kc) {
    float acc = 0.0f;  // a fresh panel total
    for (int k = pc; k < std::min(pc + kc, k_len); ++k) acc += a[k] * b[k];
    c += acc;  // joined here: a new grouping of the same products
  }
  return c;
}

int main() {
  std::vector<float> a(k_len), b(k_len);
  for (int k = 0; k < k_len; ++k) {
    a[k] = float((k * 37) % 101) / 7.0f - 5.0f;
    b[k] = 1.0f / float(1 + k % 13);
  }
  float naive = 0.0f;
  for (int k = 0; k < k_len; ++k) naive += a[k] * b[k];
  auto bits = [](float x) { return std::bit_cast<std::uint32_t>(x); };

  std::println("kc     C-initialized  zero-initialized");
  std::set<std::uint32_t> from_c, from_zero;
  for (int kc : {16, 32, 64, 128, 256, 512}) {
    float c1 = c_initialized(a, b, kc), c0 = zero_initialized(a, b, kc);
    from_c.insert(bits(c1));
    from_zero.insert(bits(c0));
    std::println("{:<6} {:#010x}     {:#010x}", kc, bits(c1), bits(c0));
  }
  std::println("naive  {:#010x}", bits(naive));
  std::println("distinct results: C-initialized {}, zero-initialized {}",
               from_c.size(), from_zero.size());
}

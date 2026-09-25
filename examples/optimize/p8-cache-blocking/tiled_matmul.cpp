// The blocked ikj kernel checked, bit for bit, against the untiled one.
// Tiling is strip-mining plus permutation: it never changes which products
// are added into an element of c, only when. The bits stay the same as long
// as every element still adds its products in increasing k.
//
// The blocked loop order is (k0, j0, i, k, j): k and j strip-mined by B and
// their strip loops moved outside i. Three things are checked:
// 1. Several B, including 5, which does not divide n = 12, so the last block
//    on each axis is shorter (an edge tile).
// 2. j-blocks visited from high to low: still the same bits, because each
//    j-block owns a disjoint set of c's elements.
// 3. k-blocks visited from high to low: legal strip-mining, wrong order.
//    Each element sums the same products in a different order, and
//    floating-point addition is not associative.
//
// Built with -ffp-contract=off: each * and + rounds once, as in Vortex.
//
// Follows: the fully-permutable-band condition in Wolf and Lam, "A Data
// Locality Optimizing Algorithm", PLDI 1991, section 4, and the blocked
// loop of Lam, Rothberg and Wolf, ASPLOS 1991, section 1.1.

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <print>

constexpr int n = 12;
using Grid = std::array<std::array<float, n>, n>;

// Fractions with different denominators round differently, so a change in
// the order of additions shows up in the last bits.
float value(int i, int j) {
  return static_cast<float>((7 * i + 3 * j) % 13 + 1) / static_cast<float>(j + 5);
}

bool same_bits(const Grid &x, const Grid &y) {
  for (int i = 0; i < n; ++i)
    for (int j = 0; j < n; ++j)
      if (std::bit_cast<std::uint32_t>(x[i][j]) != std::bit_cast<std::uint32_t>(y[i][j]))
        return false;
  return true;
}

Grid untiled(const Grid &a, const Grid &b) {
  Grid c{};
  for (int i = 0; i < n; ++i)
    for (int k = 0; k < n; ++k)
      for (int j = 0; j < n; ++j) c[i][j] += a[i][k] * b[k][j];
  return c;
}

// Block start number `step` of `count`, visited forward or backward.
int start(int step, int count, int block, bool backward) {
  return (backward ? count - 1 - step : step) * block;
}

Grid blocked(const Grid &a, const Grid &b, int block, bool k_backward, bool j_backward) {
  Grid c{};
  const int count = (n + block - 1) / block;
  for (int ks = 0; ks < count; ++ks) {
    const int k0 = start(ks, count, block, k_backward), k1 = std::min(k0 + block, n);
    for (int js = 0; js < count; ++js) {
      const int j0 = start(js, count, block, j_backward), j1 = std::min(j0 + block, n);
      for (int i = 0; i < n; ++i)
        for (int k = k0; k < k1; ++k)
          for (int j = j0; j < j1; ++j) c[i][j] += a[i][k] * b[k][j];
    }
  }
  return c;
}

int main() {
  Grid a{}, b{};
  for (int i = 0; i < n; ++i)
    for (int j = 0; j < n; ++j) {
      a[i][j] = value(i, j);
      b[i][j] = value(j, i);
    }
  const Grid reference = untiled(a, b);
  auto report = [&](const char *what, int block, bool k_backward, bool j_backward) {
    bool same = same_bits(reference, blocked(a, b, block, k_backward, j_backward));
    std::println("B = {:>2}, {}: {}", block, what, same ? "same bits" : "different bits");
  };
  for (int block : {3, 4, 5, 12}) report("k-blocks forward", block, false, false);
  report("j-blocks backward", 4, false, true);
  report("k-blocks backward", 4, true, false);
}

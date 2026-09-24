// A tiled matrix multiplication checked, bit for bit, against the ikj
// kernel from P7. Tiling is strip-mining plus permutation (P7): it never
// changes which products are added, only when each is added, so a Vortex
// compiler keeps identical bits as long as every C element still sums its
// products in increasing k. Three things are checked:
//
// 1. Square tiles of several sizes, including one (5) that does not divide
//    N = 12, so the sweep touches remainder tiles on every axis. Same bits
//    as the reference in every case: the band (i, j, k) is fully
//    permutable, so tiling it is legal (P7), and k-tiles run in increasing
//    order.
// 2. The same tiling with its k-tiles visited from high to low: legal
//    strip-mining, illegal order. Each C element now sums the same set of
//    products in a different order, so the rounded result usually differs.
//
// Built with -ffp-contract=off: each * and + rounds once, as Vortex
// requires, so the output is the same on every platform.
//
// Follows: the fully-permutable-band condition in Wolf and Lam, "A Data
// Locality Optimizing Algorithm", PLDI 1991, section 4, and the legality
// argument for tiling matrix multiplication in Lam, Rothberg and Wolf,
// "The Cache Performance and Optimizations of Blocked Algorithms",
// ASPLOS 1991, section 3.

#include <array>
#include <bit>
#include <cstdint>
#include <print>

constexpr int n = 12;
using Grid = std::array<std::array<float, n>, n>;

// Fractions with different denominators round differently, so the order of
// additions shows up in the last bits (same idea as P7's interchange.cpp).
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

// The reference: ikj order, one running accumulation per element, k
// increasing. P7 shows this is itself bit-identical to the naive ijk form.
Grid reference(const Grid &a, const Grid &b) {
  Grid c{};
  for (int i = 0; i < n; ++i)
    for (int k = 0; k < n; ++k)
      for (int j = 0; j < n; ++j) c[i][j] += a[i][k] * b[k][j];
  return c;
}

// tile: side T, edge tiles clipped to n. When descending is false the
// k-tiles run 0, T, 2T, ...; when true they run in the opposite order.
// Either way, i-tiles and j-tiles may run in any order: each covers a
// disjoint set of C elements, so their order never touches another
// element's accumulation.
Grid tiled(const Grid &a, const Grid &b, int t, bool descending) {
  Grid c{};
  std::array<int, n> k_starts{};
  int k_tiles = 0;
  for (int k0 = 0; k0 < n; k0 += t) k_starts[k_tiles++] = k0;
  for (int kt = 0; kt < k_tiles; ++kt) {
    int k0 = k_starts[descending ? k_tiles - 1 - kt : kt];
    int k1 = std::min(k0 + t, n);
    for (int i0 = 0; i0 < n; i0 += t) {
      int i1 = std::min(i0 + t, n);
      for (int j0 = 0; j0 < n; j0 += t) {
        int j1 = std::min(j0 + t, n);
        for (int i = i0; i < i1; ++i)
          for (int k = k0; k < k1; ++k)
            for (int j = j0; j < j1; ++j) c[i][j] += a[i][k] * b[k][j];
      }
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
  Grid ref = reference(a, b);

  for (int t : {3, 4, 5, 12}) {
    bool ok = same_bits(ref, tiled(a, b, t, /*descending=*/false));
    std::println("tile {:>2}, k increasing: {}", t, ok ? "same bits" : "different bits");
  }
  bool ok = same_bits(ref, tiled(a, b, 4, /*descending=*/true));
  std::println("tile  4, k decreasing: {}", ok ? "same bits" : "different bits");
}

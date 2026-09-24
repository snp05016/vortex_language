// Loop interchange is legal exactly when no dependence changes direction.
// Three loop nests run in their written order and with their two loops
// swapped, and the program reports whether the results match.
//
// 1. C = A * B, as ijk with a running sum and as ikj adding into c. The only
//    dependence is on one element of c along k, and both orders keep k
//    increasing for each element: the same additions in the same order.
// 2. One float accumulator for a whole grid. Every iteration reads and writes
//    it, so swapping the loops changes the order of the additions, and float
//    addition is not associative: the rounded sums differ.
// 3. g[i][j] = g[i-1][j+1] + 1, distance (1, -1). Swapped, each read runs
//    before the write it depends on, so the grid differs even with integers.
//
// Built with -ffp-contract=off: each * and + rounds once, as Vortex requires,
// so the output is the same on every platform.
//
// Follows: the legality theorem stated in LLVM 18's LoopInterchange.cpp, and
// Wolf and Lam, "A Data Locality Optimizing Algorithm", PLDI 1991, section 2.

#include <array>
#include <bit>
#include <cstdint>
#include <print>

constexpr int n = 8;
template <typename T> using Grid = std::array<std::array<T, n>, n>;

// Fractions with different denominators round differently, so the order of
// additions shows up in the last bits.
float value(int i, int j) {
  return static_cast<float>((5 * i + 3 * j) % 11 + 1) / static_cast<float>(j + 3);
}

bool same_bits(const Grid<float> &x, const Grid<float> &y) {
  for (int i = 0; i < n; ++i)
    for (int j = 0; j < n; ++j)
      if (std::bit_cast<std::uint32_t>(x[i][j]) != std::bit_cast<std::uint32_t>(y[i][j]))
        return false;
  return true;
}

int main() {
  Grid<float> a{}, b{};
  for (int i = 0; i < n; ++i)
    for (int j = 0; j < n; ++j) {
      a[i][j] = value(i, j);
      b[i][j] = value(j, i);
    }

  Grid<float> ijk{}, ikj{};  // ikj starts at +0.0, like the running sum
  for (int i = 0; i < n; ++i)
    for (int j = 0; j < n; ++j) {
      float sum = 0.0f;
      for (int k = 0; k < n; ++k) sum += a[i][k] * b[k][j];
      ijk[i][j] = sum;
    }
  for (int i = 0; i < n; ++i)
    for (int k = 0; k < n; ++k)
      for (int j = 0; j < n; ++j) ikj[i][j] += a[i][k] * b[k][j];
  std::println("matmul, ijk vs ikj:     {}", same_bits(ijk, ikj) ? "same bits" : "different bits");

  float rows_first = 0.0f, columns_first = 0.0f;
  for (int i = 0; i < n; ++i)
    for (int j = 0; j < n; ++j) rows_first += a[i][j];
  for (int j = 0; j < n; ++j)
    for (int i = 0; i < n; ++i) columns_first += a[i][j];
  std::println("grid sum, ij vs ji:     {} vs {}", rows_first, columns_first);

  Grid<int> g_ij{}, g_ji{};
  for (int i = 1; i < n; ++i)
    for (int j = 0; j + 1 < n; ++j) g_ij[i][j] = g_ij[i - 1][j + 1] + 1;
  for (int j = 0; j + 1 < n; ++j)
    for (int i = 1; i < n; ++i) g_ji[i][j] = g_ji[i - 1][j + 1] + 1;
  std::println("shifted copy, ij vs ji: {}", g_ij == g_ji ? "same values" : "different values");
}

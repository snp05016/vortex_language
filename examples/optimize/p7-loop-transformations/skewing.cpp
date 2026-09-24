// Loop skewing on a wavefront. Each interior cell of h is computed from the
// cell above it and the cell to its left: dependence distances (1, 0) and
// (0, 1) in (i, j) order. Each loop carries a dependence, so neither loop's
// iterations can run at the same time.
//
// Skewing j by i gives a new index w = i + j. With w outer and i inner the
// distances become (1, 1) and (1, 0): the w loop carries every dependence,
// so the iterations of the inner loop, one anti-diagonal of h, are
// independent of one another. The program runs both orders, compares the
// bits, and prints how many cells each anti-diagonal holds.
//
// Follows: Wolf and Lam, "A Data Locality Optimizing Algorithm", PLDI 1991,
// section 2 (skewing as a unimodular transformation, and its legality test).

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <print>

constexpr int rows = 5, cols = 7;
using Grid = std::array<std::array<float, cols>, rows>;

float cell(const Grid &h, int i, int j) {
  return 0.5f * h[i - 1][j] + 0.25f * h[i][j - 1] + 1.0f / static_cast<float>(i + j);
}

Grid edges() {
  Grid h{};
  for (int j = 0; j < cols; ++j) h[0][j] = static_cast<float>(j) / 3.0f;
  for (int i = 0; i < rows; ++i) h[i][0] = static_cast<float>(i) / 5.0f;
  return h;
}

int main() {
  Grid by_rows = edges();
  for (int i = 1; i < rows; ++i)
    for (int j = 1; j < cols; ++j) by_rows[i][j] = cell(by_rows, i, j);

  // Skewed and interchanged: w = i + j runs outside, i inside, j = w - i.
  Grid by_waves = edges();
  std::print("cells per wave:");
  for (int w = 2; w <= (rows - 1) + (cols - 1); ++w) {
    const int first = std::max(1, w - (cols - 1)), last = std::min(rows - 1, w - 1);
    for (int i = first; i <= last; ++i) by_waves[i][w - i] = cell(by_waves, i, w - i);
    std::print(" {}", last - first + 1);
  }
  std::println("");

  bool same = true;
  for (int i = 0; i < rows; ++i)
    for (int j = 0; j < cols; ++j)
      same = same && std::bit_cast<std::uint32_t>(by_rows[i][j]) ==
                         std::bit_cast<std::uint32_t>(by_waves[i][j]);
  std::println("same bits:     {}", same);
}

// Unroll-and-jam with scalar replacement, on a kernel that takes the dot
// product of every row of x with every row of y (d = x times y transposed).
//
// The plain nest keeps one running sum per element of d in a register, which
// is scalar replacement for that element, and still loads two values per
// multiply-add. Unrolling the i and j loops by 2 and jamming the four copies
// into one k loop gives four running sums: each k step loads x[i][k],
// x[i+1][k], y[j][k] and y[j+1][k] once and uses each value twice.
//
// Every sum still starts at 0 and adds its products in increasing k, so the
// results are bit-identical (built with -ffp-contract=off: one rounding per
// operation). The load counter makes the reuse visible.
//
// Follows: Carr and Kennedy, "Improving the Ratio of Memory Operations to
// Floating-Point Operations in Loops", TOPLAS 1994, sections 3.1 and 3.2, and
// the loop-unroll-and-jam entry in LLVM's pass documentation.

#include <array>
#include <bit>
#include <cstdint>
#include <print>

constexpr int rows_x = 4, rows_y = 6, depth = 8;  // both row counts even
using X = std::array<std::array<float, depth>, rows_x>;
using Y = std::array<std::array<float, depth>, rows_y>;
using D = std::array<std::array<float, rows_y>, rows_x>;

long loads = 0;
float load(const float &v) { ++loads; return v; }

void plain(const X &x, const Y &y, D &d) {
  for (int i = 0; i < rows_x; ++i)
    for (int j = 0; j < rows_y; ++j) {
      float s = 0.0f;
      for (int k = 0; k < depth; ++k) s += load(x[i][k]) * load(y[j][k]);
      d[i][j] = s;
    }
}

void jammed(const X &x, const Y &y, D &d) {
  for (int i = 0; i < rows_x; i += 2)
    for (int j = 0; j < rows_y; j += 2) {
      float s00 = 0.0f, s01 = 0.0f, s10 = 0.0f, s11 = 0.0f;
      for (int k = 0; k < depth; ++k) {
        const float x0 = load(x[i][k]), x1 = load(x[i + 1][k]);
        const float y0 = load(y[j][k]), y1 = load(y[j + 1][k]);
        s00 += x0 * y0;
        s01 += x0 * y1;
        s10 += x1 * y0;
        s11 += x1 * y1;
      }
      d[i][j] = s00;
      d[i][j + 1] = s01;
      d[i + 1][j] = s10;
      d[i + 1][j + 1] = s11;
    }
}

int main() {
  X x{};
  Y y{};
  for (int k = 0; k < depth; ++k) {
    for (int i = 0; i < rows_x; ++i) x[i][k] = static_cast<float>(i + 2 * k + 1) / 7.0f;
    for (int j = 0; j < rows_y; ++j) y[j][k] = static_cast<float>(3 * j + k + 1) / 9.0f;
  }
  constexpr long multiply_adds = long{rows_x} * rows_y * depth;

  D d_plain{}, d_jammed{};
  loads = 0;
  plain(x, y, d_plain);
  std::println("plain:          {} loads for {} multiply-adds", loads, multiply_adds);
  loads = 0;
  jammed(x, y, d_jammed);
  std::println("unroll-and-jam: {} loads for {} multiply-adds", loads, multiply_adds);

  bool same = true;
  for (int i = 0; i < rows_x; ++i)
    for (int j = 0; j < rows_y; ++j)
      same = same && std::bit_cast<std::uint32_t>(d_plain[i][j]) ==
                         std::bit_cast<std::uint32_t>(d_jammed[i][j]);
  std::println("same bits:      {}", same);
}

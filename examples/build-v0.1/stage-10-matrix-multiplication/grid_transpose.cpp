// Transpose a fixed-size 2 by 3 grid into a 3 by 2 grid: a different
// operation from matrix multiplication, but built from the same pieces the
// stage asks for: nested loops, a shape fixed by the parameter types, the
// input taken by const reference (read only) and the output taken by
// reference and written into (the caller owns the storage), and a hand-worked
// known answer the result is checked against.
//
// Follows: cppreference std::array and its operator==.

#include <array>
#include <print>

using Grid2x3 = std::array<std::array<int, 3>, 2>;
using Grid3x2 = std::array<std::array<int, 2>, 3>;

// `in` has exactly 2 rows of 3, `out` has exactly 3 rows of 2: the types
// alone rule out passing a grid of the wrong shape.
void transpose(const Grid2x3& in, Grid3x2& out) {
  for (std::size_t row = 0; row < in.size(); ++row) {
    for (std::size_t col = 0; col < in[row].size(); ++col) {
      out[col][row] = in[row][col];
    }
  }
}

int main() {
  const Grid2x3 grid{{{1, 2, 3}, {4, 5, 6}}};
  Grid3x2 result{};
  transpose(grid, result);

  const Grid3x2 known_answer{{{1, 4}, {2, 5}, {3, 6}}};

  for (const auto& row : result) {
    std::println("{} {}", row[0], row[1]);
  }
  std::println("{}", result == known_answer ? "ok" : "wrong answer");
}

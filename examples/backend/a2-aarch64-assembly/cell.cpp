// Read what a compiler makes of one array access. cell() returns element
// [row][col] of a fixed-shape 2 x 3 matrix of int32, stored row by row. The
// shape is part of the type, so the row stride (3 elements, 12 bytes) is a
// constant the compiler can write straight into the instructions: the address
// is m + row * 12 + col * 4, with no stride to look up while the program runs.
// Compile this file with -S at -O0 and at -O2 and compare the two listings.
//
// cell() is not static, so it keeps its own symbol and appears in the
// assembly listing even when main() inlines a copy of it.

#include <cstdint>
#include <print>

extern "C" std::int32_t cell(const std::int32_t (&m)[2][3], std::uint64_t row,
                             std::uint64_t col) {
  return m[row][col];
}

int main() {
  const std::int32_t m[2][3] = {{1, 2, 3}, {4, 5, 6}};
  for (std::uint64_t row = 0; row < 2; ++row)
    for (std::uint64_t col = 0; col < 3; ++col)
      std::println("m[{}][{}] = {}", row, col, cell(m, row, col));
}

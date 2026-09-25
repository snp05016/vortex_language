// Check a two-dimensional index against its own row and column extents,
// separately: the trap this stage warns about is checking only the
// flattened position instead of each dimension on its own.
//
// Follows: cppreference std::optional and std::pair.

#include <array>
#include <optional>
#include <print>
#include <utility>

constexpr int kRows = 2;
constexpr int kCols = 3;

// Returns the cell only when both indices fit their own dimension.
std::optional<int> checked_at(const int (&grid)[kRows][kCols], int row,
                               int col) {
  if (row < 0 || row >= kRows) {
    return std::nullopt;
  }
  if (col < 0 || col >= kCols) {
    return std::nullopt;
  }
  return grid[row][col];
}

int main() {
  const int grid[kRows][kCols] = {{1, 2, 3}, {4, 5, 6}};

  // (row, col) pairs: some in bounds, some not. The third pair's flattened
  // position, row 0 times 3 columns plus column 5, is 5, a valid position in
  // a 6-cell block of memory, but column 5 does not exist in a 3-column row.
  const std::array<std::pair<int, int>, 5> tries{{
      {0, 0},
      {1, 2},
      {0, 5},
      {-1, 0},
      {2, 0},
  }};

  for (const auto& [row, col] : tries) {
    const auto value = checked_at(grid, row, col);
    if (value) {
      std::println("[{}, {}]: {}", row, col, *value);
    } else {
      std::println("[{}, {}]: out of bounds", row, col);
    }
  }
}

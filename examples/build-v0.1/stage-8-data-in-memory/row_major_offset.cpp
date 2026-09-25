// A spreadsheet-style grid of scores, stored as one flat buffer instead of
// a grid of grids. Row-major order puts every cell of a row before the next
// row starts, so cell (row, column) sits at row * columns + column.
//
// Follows: cppreference's page on std::vector, whose elements are stored
// contiguously, which is what lets one flat buffer stand in for a grid.

#include <cstddef>
#include <print>
#include <vector>

std::size_t offset(std::size_t row, std::size_t column, std::size_t columns) {
  // Moving one column is one step; moving one row skips a whole row.
  return row * columns + column;
}

int main() {
  // Two rows and three columns: a non-square grid, so swapping row and
  // column in the formula would give visibly wrong offsets.
  constexpr std::size_t rows = 2;
  constexpr std::size_t columns = 3;
  std::vector<int> scores(rows * columns);

  int next = 1;
  for (std::size_t row = 0; row < rows; ++row) {
    for (std::size_t column = 0; column < columns; ++column) {
      scores[offset(row, column, columns)] = next++;
    }
  }

  for (std::size_t row = 0; row < rows; ++row) {
    for (std::size_t column = 0; column < columns; ++column) {
      const std::size_t at = offset(row, column, columns);
      std::println("scores[{}, {}] = {} (flat offset {})", row, column,
                   scores[at], at);
    }
  }
}

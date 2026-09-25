// The warp-level WMMA API hides which lane holds which element of a tile.
// PTX's lower-level mma instruction does not: for each shape it gives a
// formula from (lane, register index) to (row, column). This program applies
// the formula PTX documents for the f32 accumulator of mma.m16n8k16, prints
// which of the 32 lanes owns each of the 16 x 8 = 128 elements, checks that
// every element has exactly one owner, and lists the four elements lane 5
// holds. A compiler that emits mma has to follow this map exactly: the
// instruction reads each lane's registers as those elements.
//
// Follows: https://docs.nvidia.com/cuda/parallel-thread-execution/index.html#warp-level-matrix-fragment-mma-16816-float

#include <array>
#include <cstddef>
#include <print>

constexpr std::size_t rows = 16, cols = 8, lanes = 32, per_lane = 4;

struct Element { std::size_t row, col; };

// The accumulator formula: lanes form 8 groups of 4; a group owns two rows
// (g and g + 8), and its 4 lanes split each row into pairs of columns.
Element accumulator_element(std::size_t lane, std::size_t i) {
  std::size_t group = lane >> 2;
  std::size_t in_group = lane % 4;
  std::size_t row = i < 2 ? group : group + 8;
  std::size_t col = in_group * 2 + (i & 1);
  return {row, col};
}

int main() {
  std::array<std::array<int, cols>, rows> owner{};
  for (auto& r : owner) r.fill(-1);
  bool one_owner_each = true;
  for (std::size_t lane = 0; lane < lanes; ++lane)
    for (std::size_t i = 0; i < per_lane; ++i) {
      auto [r, c] = accumulator_element(lane, i);
      if (owner[r][c] != -1) one_owner_each = false;
      owner[r][c] = static_cast<int>(lane);
    }
  for (const auto& r : owner)
    for (int lane : r)
      if (lane == -1) one_owner_each = false;

  std::println("owner lane of each accumulator element (16 rows x 8 columns):");
  for (std::size_t r = 0; r < rows; ++r) {
    std::print("row {:2}:", r);
    for (std::size_t c = 0; c < cols; ++c) std::print(" {:2}", owner[r][c]);
    std::println("");
  }
  std::println("every element has exactly one owner: {}", one_owner_each);
  std::print("lane 5 holds:");
  for (std::size_t i = 0; i < per_lane; ++i) {
    auto [r, c] = accumulator_element(5, i);
    std::print(" c{}=({}, {})", i, r, c);
  }
  std::println("");
}

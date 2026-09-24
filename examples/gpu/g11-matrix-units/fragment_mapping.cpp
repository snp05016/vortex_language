// A matrix unit's "fragment" holds one lane's share of a tile, but which
// lane holds which element is not specified: the Metal Shading Language
// specification leaves the element-to-lane mapping of a SIMD-group matrix
// unspecified, and CUDA's WMMA fragment is likewise opaque, documented only
// through load and store functions, not through a layout a programmer may
// assume. The only contract is: load a tile into a fragment, then store it
// back, and the tile you get out is the tile you put in.
//
// This builds two different mappings from an 8 x 8 tile's 64 cells onto 32
// lanes with 2 slots each, checks that each is a legal fragment (a
// bijection: every slot holds exactly one cell), and checks that both
// round-trip correctly, even though they disagree about which lane holds
// any given cell.
//
// Follows: https://developer.apple.com/metal/Metal-Shading-Language-Specification.pdf (6.8)
//          https://docs.nvidia.com/cuda/cuda-programming-guide/05-appendices/cpp-language-extensions.html#warp-matrix-functions

#include <array>
#include <cstddef>
#include <print>
#include <string_view>

constexpr std::size_t side = 8;                     // an 8 x 8 tile
constexpr std::size_t lanes = 32;
constexpr std::size_t slots = side * side / lanes;   // 2 cells per lane

using Cell = std::pair<std::size_t, std::size_t>;  // (lane, slot)

// Flattens the tile row by row, then splits the flat index into lane and slot.
Cell row_major(std::size_t r, std::size_t c) {
  std::size_t flat = r * side + c;
  return {flat / slots, flat % slots};
}

// Flattens the tile column by column instead: a different, equally legal
// fragment layout, disagreeing with row_major about almost every cell.
Cell column_major(std::size_t r, std::size_t c) {
  std::size_t flat = c * side + r;
  return {flat / slots, flat % slots};
}

// A fragment is legal when its map is a bijection from the tile's cells onto
// every (lane, slot) pair: nothing is dropped, nothing collides.
bool round_trips(Cell (*to_fragment)(std::size_t, std::size_t), std::string_view name) {
  std::array<std::array<int, slots>, lanes> fragment{};
  std::array<std::array<bool, slots>, lanes> filled{};
  for (std::size_t r = 0; r < side; ++r)
    for (std::size_t c = 0; c < side; ++c) {
      auto [lane, slot] = to_fragment(r, c);
      if (filled[lane][slot]) {
        std::println("{}: lane {} slot {} written twice", name, lane, slot);
        return false;
      }
      filled[lane][slot] = true;
      fragment[lane][slot] = static_cast<int>(r * side + c);
    }
  for (std::size_t r = 0; r < side; ++r)
    for (std::size_t c = 0; c < side; ++c) {
      auto [lane, slot] = to_fragment(r, c);
      if (fragment[lane][slot] != static_cast<int>(r * side + c)) return false;
    }
  return true;
}

int main() {
  std::println("row-major fragment round-trips: {}", round_trips(row_major, "row-major"));
  std::println("column-major fragment round-trips: {}", round_trips(column_major, "column-major"));
  auto [rlane, rslot] = row_major(3, 5);
  auto [clane, cslot] = column_major(3, 5);
  std::println("cell (row 3, col 5): row-major lane {} slot {}, column-major lane {} slot {}",
               rlane, rslot, clane, cslot);
  std::println("same cell, different lane: {}", rlane != clane);
}

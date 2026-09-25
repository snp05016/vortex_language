// A tile compiler also chooses the order in which programs (one per
// output tile of C = A * B) are launched. Each program needs one row strip
// of A tiles and one column strip of B tiles, k_tiles of each. This program
// counts how many distinct A and B tiles the first `wave` programs need
// between them, for row-major order and for "grouped" orders that sweep a
// band of `group` tile rows column by column. Counting only: no cache, no
// timing. With a 9 x 9 grid and a wave of 9 it reproduces the 90 and 54
// that the Triton matmul tutorial gives for its own example.

#include <algorithm>
#include <cstddef>
#include <print>
#include <set>

struct Tile { std::size_t row, col; };

// Program id -> output tile. group == 1 is plain row-major order.
Tile grouped_tile(std::size_t pid, std::size_t m_tiles, std::size_t n_tiles,
                  std::size_t group) {
  const std::size_t per_band = group * n_tiles;         // programs in one band
  const std::size_t first_row = (pid / per_band) * group;
  const std::size_t rows = std::min(group, m_tiles - first_row);  // last band may be short
  const std::size_t local = pid % per_band;
  return {first_row + local % rows, local / rows};      // walk down, then right
}

// A tiles needed: k_tiles per distinct row; B tiles: k_tiles per distinct column.
std::size_t tiles_loaded(std::size_t wave, std::size_t m_tiles,
                         std::size_t n_tiles, std::size_t k_tiles,
                         std::size_t group) {
  std::set<std::size_t> rows, cols;
  for (std::size_t pid = 0; pid < wave; ++pid) {
    const Tile t = grouped_tile(pid, m_tiles, n_tiles, group);
    rows.insert(t.row);
    cols.insert(t.col);
  }
  return (rows.size() + cols.size()) * k_tiles;
}

int main() {
  constexpr std::size_t m_tiles = 9, n_tiles = 9, k_tiles = 9, wave = 9;

  std::println("program id -> (tile row, tile col), group of 3:");
  for (std::size_t pid = 0; pid < 12; ++pid) {
    const Tile t = grouped_tile(pid, m_tiles, n_tiles, 3);
    std::print("{:2}->({},{}) ", pid, t.row, t.col);
    if (pid % 6 == 5) std::println("");
  }

  std::println("\ntiles of A and B loaded by the first {} programs:", wave);
  for (std::size_t group : {1, 2, 3, 4, 9})
    std::println("  group {}: {:3}", group,
                 tiles_loaded(wave, m_tiles, n_tiles, k_tiles, group));

  // Every order must still launch each output tile exactly once.
  for (std::size_t group : {1, 2, 3, 4, 9}) {
    std::set<std::size_t> seen;
    for (std::size_t pid = 0; pid < m_tiles * n_tiles; ++pid) {
      const Tile t = grouped_tile(pid, m_tiles, n_tiles, group);
      seen.insert(t.row * n_tiles + t.col);
    }
    if (seen.size() != m_tiles * n_tiles) std::println("group {}: NOT a permutation", group);
  }
  std::println("every order covers all {} tiles once", m_tiles * n_tiles);
}

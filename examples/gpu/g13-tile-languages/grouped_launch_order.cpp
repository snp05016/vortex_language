// A tile compiler also chooses the order in which it launches output
// tiles, not only who computes each one. The Triton matmul tutorial groups
// a fixed number of row-tiles together and sweeps columns inside the group,
// instead of finishing one row of tiles before starting the next, so that
// fewer distinct tiles of A and B must be resident at once to serve any
// short run of the launch. This program counts that "working set" for both
// orders over an M-tile x N-tile grid: no timing or cache is measured, only
// how many distinct row-tile and column-tile ids appear in a sliding window
// of the launch order.

#include <algorithm>
#include <cstddef>
#include <print>
#include <set>
#include <utility>
#include <vector>

using Tile = std::pair<std::size_t, std::size_t>;  // (row-tile, col-tile)

std::vector<Tile> row_major(std::size_t m_tiles, std::size_t n_tiles) {
  std::vector<Tile> order;
  for (std::size_t row = 0; row < m_tiles; ++row)
    for (std::size_t col = 0; col < n_tiles; ++col) order.push_back({row, col});
  return order;
}

// Groups of `group_m` row-tiles at a time; within a group, sweep every
// column before moving to the next row, so a column's B-tile is reused
// across the whole group instead of once.
std::vector<Tile> grouped(std::size_t m_tiles, std::size_t n_tiles, std::size_t group_m) {
  std::vector<Tile> order;
  for (std::size_t base = 0; base < m_tiles; base += group_m) {
    const std::size_t rows = std::min(group_m, m_tiles - base);
    for (std::size_t col = 0; col < n_tiles; ++col)
      for (std::size_t row = base; row < base + rows; ++row) order.push_back({row, col});
  }
  return order;
}

// The distinct row-tiles plus distinct col-tiles seen in a window of
// `width` consecutive launches, averaged over every window of that width:
// how many tiles must stay resident, on average, to serve the launches
// that recently ran, if nothing outside the window is cached.
double average_working_set(const std::vector<Tile> &order, std::size_t width) {
  std::size_t total = 0, windows = 0;
  for (std::size_t start = 0; start + width <= order.size(); ++start) {
    std::set<std::size_t> rows, cols;
    for (std::size_t i = start; i < start + width; ++i) {
      rows.insert(order[i].first);
      cols.insert(order[i].second);
    }
    total += rows.size() + cols.size();
    ++windows;
  }
  return static_cast<double>(total) / static_cast<double>(windows);
}

int main() {
  constexpr std::size_t m_tiles = 8, n_tiles = 12, group_m = 4, width = 8;
  const auto naive = row_major(m_tiles, n_tiles);
  const auto swizzled = grouped(m_tiles, n_tiles, group_m);
  std::println("{} x {} tile grid, window {}, group {} rows:", m_tiles, n_tiles, width, group_m);
  std::println("row-major average working set:  {:.2f} tiles", average_working_set(naive, width));
  std::println("grouped   average working set:  {:.2f} tiles", average_working_set(swizzled, width));
  std::print("first {} tiles, row-major: ", width);
  for (std::size_t i = 0; i < width; ++i) std::print("({},{}) ", naive[i].first, naive[i].second);
  std::println("");
  std::print("first {} tiles, grouped:   ", width);
  for (std::size_t i = 0; i < width; ++i) std::print("({},{}) ", swizzled[i].first, swizzled[i].second);
  std::println("");
}

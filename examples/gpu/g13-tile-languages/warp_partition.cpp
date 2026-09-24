// Who computes which output element of a tile, for a chosen (warp count,
// lane count) split. A tile language's compiler works this out from a few
// integers, such as Triton's `num_warps`, instead of asking the programmer
// to write index arithmetic; G10 names the three levels it splits a block
// tile into: block tile, warp tile, thread tile. The sizes below are
// illustrative, not a real warp width (32 on NVIDIA and Apple GPUs, G2): a
// small grid keeps the printed map readable. This is one deterministic
// split, not a claim about how any particular compiler's own split works.

#include <cstddef>
#include <print>

constexpr std::size_t block_m = 8, block_n = 8;   // the whole tile
constexpr std::size_t warps_m = 2, warps_n = 2;    // warp grid over the tile
constexpr std::size_t lanes_m = 2, lanes_n = 2;    // lane grid over a warp
static_assert(block_m % warps_m == 0 && block_n % warps_n == 0);
constexpr std::size_t warp_tile_m = block_m / warps_m;
constexpr std::size_t warp_tile_n = block_n / warps_n;
static_assert(warp_tile_m % lanes_m == 0 && warp_tile_n % lanes_n == 0);
constexpr std::size_t thread_tile_m = warp_tile_m / lanes_m;
constexpr std::size_t thread_tile_n = warp_tile_n / lanes_n;

struct Owner { std::size_t warp, lane; };

// Computed once per kernel by the code generator, not once per element by
// the programmer: which warp and which lane of that warp produce (row, col).
Owner owner(std::size_t row, std::size_t col) {
  const std::size_t warp_row = row / warp_tile_m, warp_col = col / warp_tile_n;
  const std::size_t in_row = row % warp_tile_m, in_col = col % warp_tile_n;
  const std::size_t lane_row = in_row / thread_tile_m, lane_col = in_col / thread_tile_n;
  return {warp_row * warps_n + warp_col, lane_row * lanes_n + lane_col};
}

void print_grid(const char *title, std::size_t Owner::*field) {
  std::println("{}", title);
  for (std::size_t row = 0; row < block_m; ++row) {
    for (std::size_t col = 0; col < block_n; ++col)
      std::print("{} ", owner(row, col).*field);
    std::println("");
  }
}

int main() {
  std::println("{}x{} block tile, {}x{} warps of {}x{} lanes:", block_m,
               block_n, warps_m, warps_n, lanes_m, lanes_n);
  std::println("");
  print_grid("warp id, by output element:", &Owner::warp);
  std::println("");
  print_grid("lane id within its warp:", &Owner::lane);
  std::println("");
  const std::size_t threads = warps_m * warps_n * lanes_m * lanes_n;
  std::println("{} threads own {} elements, {} each", threads,
               block_m * block_n, thread_tile_m * thread_tile_n);
}

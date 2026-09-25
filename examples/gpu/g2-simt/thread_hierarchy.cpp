// A grid of blocks, sliced into warps.
//
// The hardware groups a block's threads into warps of 32 consecutive
// thread ids. A block whose size is not a multiple of 32 still gets whole
// warps, and its last warp has fewer threads than lanes. In a 2-D block the
// id is x + y * width, so x varies fastest, and a block narrower than 32
// packs several of its rows into one warp. Nothing here runs on a GPU.

#include <cstddef>
#include <print>

constexpr std::size_t warp_size = 32;

std::size_t warps_for(std::size_t threads) {
  return (threads + warp_size - 1) / warp_size;  // ceil(threads / 32)
}

// A 1-D grid: `blocks` blocks of `threads` threads each.
void grid_1d(std::size_t blocks, std::size_t threads) {
  std::println("{} blocks of {} threads:", blocks, threads);
  std::println("{:>6} {:>5} {:>10} {:>8}", "block", "warp", "global-ids",
               "threads");
  for (std::size_t b = 0; b < blocks; ++b) {
    for (std::size_t w = 0; w < warps_for(threads); ++w) {
      const std::size_t first = w * warp_size;
      const std::size_t count =
          threads - first < warp_size ? threads - first : warp_size;
      const std::size_t global = b * threads + first;  // block * size + id
      std::println("{:>6} {:>5} {:>4} to {:<3} {:>6}", b, w, global,
                   global + count - 1, count);
    }
  }
}

// One 2-D block of width x height threads: which (x, y) share a warp.
void block_2d(std::size_t width, std::size_t height) {
  std::println("one {} x {} block:", width, height);
  for (std::size_t w = 0; w < warps_for(width * height); ++w) {
    const std::size_t first = w * warp_size;
    std::size_t last = first + warp_size - 1;
    if (last >= width * height) last = width * height - 1;
    std::println("  warp {}: ids {:>3} to {:<3} = (x {}, y {}) to (x {}, y {})",
                 w, first, last, first % width, first / width, last % width,
                 last / width);
  }
}

int main() {
  grid_1d(2, 40);
  block_2d(16, 4);
  block_2d(32, 2);
}

// A grid of blocks, sliced into warps.
//
// A GPU does not run a block's threads one at a time: it groups every 32
// consecutive threads (NVIDIA's warp width) into one warp and issues one
// instruction for the whole group. When a block's thread count is not a
// multiple of the warp size, the block still launches whole warps, and the
// last one has fewer real threads than lane slots. This program computes
// that accounting for a small grid; nothing here runs on a GPU.

#include <algorithm>
#include <cstddef>
#include <print>

constexpr std::size_t warp_size = 32;

struct Warp {
  std::size_t block;
  std::size_t warp_in_block;
  std::size_t first_global_id;
  std::size_t active_lanes;  // real threads; the rest of the warp is idle
};

// Visits every warp a grid of `blocks` blocks, `threads_per_block` threads
// each, breaks into. A block needs ceil(threads_per_block / warp_size)
// warps; its last warp is partial whenever threads_per_block % warp_size is
// not zero.
template <class Visit>
void warps_of(std::size_t blocks, std::size_t threads_per_block, Visit visit) {
  const std::size_t warps_per_block =
      (threads_per_block + warp_size - 1) / warp_size;
  for (std::size_t b = 0; b < blocks; ++b) {
    for (std::size_t w = 0; w < warps_per_block; ++w) {
      const std::size_t first = w * warp_size;
      const std::size_t active = std::min(warp_size, threads_per_block - first);
      visit(Warp{b, w, b * threads_per_block + first, active});
    }
  }
}

int main() {
  std::println("{:>6} {:>6} {:>10} {:>7}", "block", "warp", "first-id", "lanes");
  warps_of(2, 40, [](Warp x) {
    std::println("{:>6} {:>6} {:>10} {:>7}", x.block, x.warp_in_block,
                 x.first_global_id, x.active_lanes);
  });
}

// A block tile, split into warp tiles, split into thread tiles: is every
// output computed once, by exactly one thread?
//
// Rung 9 of the ladder puts a warp tile between the block tile and each
// thread's register tile: a warp of 32 lanes claims one rectangle of the
// block's outputs and divides it among its lanes. Tile sizes may change
// which thread computes an output, never whether it is computed or how
// often. This program walks the nested index arithmetic of the split and
// records the thread that owns each output, so it catches an overlap
// (two owners, a race on `c`) and a gap (no owner, a missing result).
// It uses the simplest split, contiguous rectangles; real kernels often
// give a lane several smaller pieces spread over the warp tile, and the
// same check applies. Nothing here runs on a GPU.

#include <cstddef>
#include <print>
#include <vector>

int main() {
  constexpr std::size_t block_m = 64, block_n = 64;   // block tile
  constexpr std::size_t warp_m = 32, warp_n = 32;     // one warp's tile
  constexpr std::size_t thread_m = 8, thread_n = 4;   // one lane's tile
  constexpr std::size_t warp_size = 32;

  constexpr std::size_t nobody = block_m * block_n;  // not a thread id
  std::vector<std::size_t> owner(block_m * block_n, nobody);

  std::size_t warps = 0;
  for (std::size_t wr = 0; wr < block_m; wr += warp_m) {
    for (std::size_t wc = 0; wc < block_n; wc += warp_n, ++warps) {
      std::size_t lane = 0;
      for (std::size_t lr = wr; lr < wr + warp_m; lr += thread_m) {
        for (std::size_t lc = wc; lc < wc + warp_n; lc += thread_n, ++lane) {
          std::size_t thread = warps * warp_size + lane;
          for (std::size_t r = lr; r < lr + thread_m; ++r) {
            for (std::size_t c = lc; c < lc + thread_n; ++c) {
              std::size_t& slot = owner[r * block_n + c];
              if (slot != nobody) {
                std::println("overlap at ({}, {}): threads {} and {}", r, c,
                             slot, thread);
                return 1;
              }
              slot = thread;
            }
          }
        }
      }
      if (lane != warp_size) {
        std::println("warp {} has {} thread tiles, not {}", warps, lane,
                     warp_size);
        return 1;
      }
    }
  }

  std::size_t covered = 0;
  for (std::size_t o : owner) covered += (o != nobody);
  std::println("block tile {} x {}, warp tile {} x {}, thread tile {} x {}",
               block_m, block_n, warp_m, warp_n, thread_m, thread_n);
  std::println("warps: {}, threads: {}", warps, warps * warp_size);
  std::println("outputs owned by exactly one thread: {} of {}", covered,
               block_m * block_n);
}

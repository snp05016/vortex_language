// A block tile, split into warp tiles, split into thread tiles: does every
// output get computed once, by exactly one thread?
//
// Rung 9 of the ladder adds a warp tile between the block tile ([G4]) and
// the thread's own register tile: a warp of 32 lanes claims one rectangle
// of the block's outputs and divides it among its lanes. Changing tile
// sizes must never change which outputs exist or duplicate the work on one
// of them; it only changes how the same outputs are split among threads.
// This program walks the same nested index arithmetic a compiler would use
// to lower the split, and checks that it is a partition: every output
// owned once. Nothing here runs on a GPU.

#include <cstddef>
#include <print>
#include <vector>

int main() {
  constexpr std::size_t block_m = 64, block_n = 64;  // block tile (G4)
  constexpr std::size_t warp_m = 32, warp_n = 32;     // one warp's tile
  constexpr std::size_t thread_m = 8, thread_n = 4;   // one lane's tile

  constexpr std::size_t unowned = block_m * block_n;
  std::vector<std::size_t> owner(block_m * block_n, unowned);

  std::size_t warps = 0;
  for (std::size_t wr = 0; wr < block_m; wr += warp_m) {
    for (std::size_t wc = 0; wc < block_n; wc += warp_n, ++warps) {
      std::size_t lane = 0;
      for (std::size_t lr = wr; lr < wr + warp_m; lr += thread_m) {
        for (std::size_t lc = wc; lc < wc + warp_n; lc += thread_n, ++lane) {
          for (std::size_t r = lr; r < lr + thread_m; ++r) {
            for (std::size_t c = lc; c < lc + thread_n; ++c) {
              std::size_t& slot = owner[r * block_n + c];
              if (slot != unowned) {
                std::println("conflict at ({}, {}): warp {} and warp {} "
                             "both claim it", r, c, slot, warps);
                return 1;
              }
              slot = warps;
            }
          }
        }
      }
      if (lane != 32) {
        std::println("warp {} has {} lanes, not 32", warps, lane);
        return 1;
      }
    }
  }

  std::size_t covered = 0;
  for (std::size_t o : owner) covered += (o != unowned);
  std::println("block tile {} x {}, warp tile {} x {}, thread tile {} x {}",
               block_m, block_n, warp_m, warp_n, thread_m, thread_n);
  std::println("warps: {}, outputs covered exactly once: {} of {}", warps,
               covered, block_m * block_n);
}

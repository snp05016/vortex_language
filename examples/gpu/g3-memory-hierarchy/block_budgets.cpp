// How many equal-size shared-memory tiles fit in one thread block's budget?
//
// Two budgets, both facts from named sources, not invented:
//   - NVIDIA Hopper: one thread block may address up to 227 KB of shared
//     memory, out of 228 KB physically present per streaming multiprocessor.
//     (NVIDIA, Hopper Tuning Guide.)
//   - Apple GPU (measured): MTLDevice.maxThreadgroupMemoryLength reads
//     32768 bytes on the owner's Apple M4 Pro, macOS 27.0, 2026-09-23.
//
// This counts capacity only. Whether that many tiles can actually be
// resident at once also depends on registers and thread-block limits
// (G5 covers full occupancy).

#include <cstddef>
#include <print>
#include <string_view>

struct Budget {
  std::string_view name;
  std::size_t per_block_bytes;
};

constexpr Budget budgets[] = {
    {"NVIDIA Hopper, per block", 227 * 1024},
    {"Apple GPU, per threadgroup", 32768},
};

void row(const Budget &b, std::size_t tile_bytes) {
  std::size_t tiles = b.per_block_bytes / tile_bytes;  // floor: a partial tile does not fit
  std::println("{:<28} {:>7} B budget  {:>4} tile(s) of {} B", b.name,
               b.per_block_bytes, tiles, tile_bytes);
}

int main() {
  std::println("Shared-memory tiles for a tiled matmul (an A tile and a B tile, both f32):");
  for (std::size_t side : {16, 32, 64}) {
    std::size_t tile_bytes = side * side * sizeof(float) * 2;
    std::println("-- {}x{} tile pair, {} bytes --", side, side, tile_bytes);
    for (const auto &b : budgets) row(b, tile_bytes);
  }
}

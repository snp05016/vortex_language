// The memory work of three GPU transposes of a 64 x 64 f32 matrix.
//
// Each version runs here on the CPU, warp by warp, the way a GPU would run it
// with 32 x 32 thread blocks: a warp is one row of a block, 32 threads with
// consecutive x. For each warp-wide instruction the program gathers the 32
// addresses and counts what the hardware does with them: 32-byte sectors for
// global memory, passes for shared memory (32 banks of 4-byte words). It also
// checks the answers, because a memory schedule must never change a value.

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <print>
#include <set>
#include <vector>

constexpr std::size_t n = 64, tile = 32, lanes = 32;
using Lanes = std::array<std::size_t, lanes>;  // one address per lane

std::size_t sectors(const Lanes &byte_address) {  // global: aligned 32-byte sectors
  std::set<std::size_t> s;
  for (std::size_t a : byte_address) s.insert(a / 32);
  return s.size();
}

std::size_t passes(const Lanes &word) {  // shared: word w is in bank w % 32
  std::array<std::set<std::size_t>, 32> bank;
  for (std::size_t w : word) bank[w % 32].insert(w);
  std::size_t busiest = 0;
  for (const auto &words : bank) busiest = std::max(busiest, words.size());
  return busiest;
}

struct Counts { std::size_t read = 0, written = 0, shared = 0; };
enum class Kind { copy, naive, tiled };

// row_length is the width of the shared tile: 32, or 33 when padded.
Counts run(Kind kind, std::size_t row_length, const std::vector<float> &in,
           std::vector<float> &out) {
  Counts c;
  std::vector<float> shared(tile * row_length);
  for (std::size_t by = 0; by < n / tile; ++by)
    for (std::size_t bx = 0; bx < n / tile; ++bx) {
      for (std::size_t ty = 0; ty < tile; ++ty) {  // one warp per row of the block
        Lanes src, dst;
        for (std::size_t tx = 0; tx < lanes; ++tx) {
          const std::size_t y = by * tile + ty, x = bx * tile + tx;
          src[tx] = (y * n + x) * 4;
          if (kind == Kind::tiled) {
            dst[tx] = ty * row_length + tx;  // along a row of the tile
            shared[dst[tx]] = in[y * n + x];
          } else {
            const std::size_t o = kind == Kind::copy ? y * n + x : x * n + y;
            dst[tx] = o * 4;
            out[o] = in[y * n + x];
          }
        }
        c.read += sectors(src);
        if (kind == Kind::tiled) c.shared += passes(dst);
        else c.written += sectors(dst);
      }
      if (kind != Kind::tiled) continue;
      for (std::size_t ty = 0; ty < tile; ++ty) {  // after the barrier
        Lanes src, dst;
        for (std::size_t tx = 0; tx < lanes; ++tx) {
          const std::size_t y = bx * tile + ty, x = by * tile + tx;
          src[tx] = tx * row_length + ty;  // down a column of the tile
          dst[tx] = (y * n + x) * 4;       // along a row of the output
          out[y * n + x] = shared[src[tx]];
        }
        c.shared += passes(src);
        c.written += sectors(dst);
      }
    }
  return c;
}

int main() {
  std::vector<float> in(n * n);
  for (std::size_t i = 0; i < n * n; ++i) in[i] = static_cast<float>(i) + 0.25f;
  struct Version { const char *name; Kind kind; std::size_t row_length; };
  const Version versions[] = {{"copy (the speed limit)", Kind::copy, tile},
                              {"naive transpose", Kind::naive, tile},
                              {"tiled, 32 x 32 tile", Kind::tiled, tile},
                              {"tiled, 32 x 33 tile", Kind::tiled, tile + 1}};
  std::println("{:<23} {:>12} {:>15} {:>11} {:>13}", "version", "sectors read",
               "sectors written", "bytes moved", "shared passes");
  auto bits = [](float f) { return std::bit_cast<std::uint32_t>(f); };
  bool exact = true;  // every transpose must hold the input's bit patterns
  for (const Version &v : versions) {
    std::vector<float> out(n * n);
    const Counts k = run(v.kind, v.row_length, in, out);
    std::println("{:<23} {:>12} {:>15} {:>11} {:>13}", v.name, k.read, k.written,
                 (k.read + k.written) * 32, k.shared);
    for (std::size_t i = 0; v.kind != Kind::copy && i < n; ++i)
      for (std::size_t j = 0; j < n; ++j)
        exact = exact && bits(out[j * n + i]) == bits(in[i * n + j]);
  }
  std::println("every transpose is exact, bit for bit: {}", exact);
}

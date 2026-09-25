// A matrix unit does 512 multiply-adds per 8 x 8 x 8 instruction, but only if
// its operand fragments arrive fast enough. This program tiles a 64 x 64 x 64
// product into 8 x 8 x 8 SIMD-group operations, gives each warp a tile of
// fm x fn accumulator fragments, and counts, by walking the loops, how many
// operand fragments the warps load and how many multiply-adds they get per
// element loaded. At each step of k a warp loads fm fragments of A and fn of
// B and issues fm * fn operations, so a larger warp tile reuses each loaded
// fragment more often: the register tiling of the scalar ladder, one level up.
// The price is accumulator registers: fm * fn fragments of 64 values per warp,
// spread over 32 lanes.
//
// Follows: https://developer.apple.com/metal/Metal-Shading-Language-Specification.pdf (6.8)
//          https://docs.nvidia.com/cutlass/latest/media/docs/cpp/efficient_gemm.html

#include <cstddef>
#include <print>

constexpr std::size_t n = 64;       // M = N = K = 64
constexpr std::size_t f = 8;        // one fragment is 8 x 8
constexpr std::size_t lanes = 32;

struct Counts { std::size_t loads = 0, ops = 0; };

Counts walk(std::size_t fm, std::size_t fn) {
  Counts c;
  for (std::size_t wm = 0; wm < n; wm += f * fm)      // one warp tile
    for (std::size_t wn = 0; wn < n; wn += f * fn)
      for (std::size_t k = 0; k < n; k += f) {        // one step of k
        c.loads += fm + fn;                           // A column, B row of fragments
        c.ops += fm * fn;                             // every pair meets once
      }
  return c;
}

int main() {
  std::println("warp tile  frag loads  ops   macs/element  accumulators/lane");
  const std::size_t shapes[][2] = {{1, 1}, {1, 2}, {2, 2}, {2, 4}, {4, 4}, {8, 8}};
  for (auto [fm, fn] : shapes) {
    Counts c = walk(fm, fn);
    std::size_t macs = c.ops * f * f * f;             // always 64^3
    std::size_t elements = c.loads * f * f;
    std::println("{} x {}      {:5}      {:4}  {:5.2f}         {:3}", fm, fn, c.loads, c.ops,
                 static_cast<double>(macs) / static_cast<double>(elements),
                 fm * fn * f * f / lanes);
  }
}

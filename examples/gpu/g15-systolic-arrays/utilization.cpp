// Utilization of a fixed K x K systolic array multiplying an N x N matrix
// that does not divide evenly by K.
//
// A K x K output-stationary array only computes K x K products. A bigger
// product is covered by tiling N into ceil(N/K) tiles per side and running
// one K x K multiply per tile pair; the array is loaded with the same K,
// whether or not N is a multiple of it. Tiles that fall outside the N x N
// matrix still occupy cells and cycles: they are padded with zeros. Their
// utilization is the fraction of cell-cycles that did useful work.

#include <cstddef>
#include <print>

struct Case {
  int n; // matrix side
  int k; // array side
};

int main() {
  // 64 is the side of Vortex's own stage-10 matmul kernel (decision 43's
  // row-major [f32; 64, 64]); the others show a size that never divides
  // evenly, and the K = N case, where nothing is wasted.
  const Case cases[] = {{64, 64}, {64, 16}, {64, 24}, {10, 4}};

  std::println("{:>4} {:>4} {:>6} {:>8} {:>10}", "n", "k", "tiles", "padded", "util %");
  for (const auto &c : cases) {
    const int tiles = (c.n + c.k - 1) / c.k;       // ceil(n / k)
    const int padded = tiles * c.k;                 // padded side length
    const double util = 100.0 * (double(c.n) * c.n) / (double(padded) * padded);
    std::println("{:>4} {:>4} {:>6} {:>8} {:>9.1f}%", c.n, c.k, tiles, padded, util);
  }
}

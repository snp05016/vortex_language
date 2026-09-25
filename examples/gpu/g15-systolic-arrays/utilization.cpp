// How much of a fixed K x K array does useful work on an N x N product?
//
// The array covers the N x N result in K x K tiles, ceil(N / K) per side.
// Tiles on the far edges overrun the matrix and are padded with zeros: the
// padded cells occupy the array for as long as the real ones but add nothing.
// Both sides pad, so the useful fraction is an area: N*N / (padded*padded).
// (For an output-stationary array the padded dimensions are the two output
// dimensions; for a weight-stationary one, the two dimensions of the weight
// tile. The ratio is the same for a square matrix.)

#include <print>

struct Case {
  int n; // matrix side
  int k; // array side
};

int main() {
  // 64: the [f32; 64, 64] kernel of the GPU ladder. 600 on 256 and 512: the
  // case Jouppi et al. (ISCA 2017, section 7) use to explain why a larger
  // TPU matrix unit could be slower.
  const Case cases[] = {{64, 64}, {64, 16}, {64, 24}, {10, 4}, {600, 256}, {600, 512}};

  std::println("{:>4} {:>4} {:>10} {:>10} {:>8}", "n", "k", "tiles", "padded", "util %");
  for (const auto &c : cases) {
    const int per_side = (c.n + c.k - 1) / c.k; // ceil(n / k)
    const int padded = per_side * c.k;
    const double util = 100.0 * (double(c.n) * c.n) / (double(padded) * padded);
    std::println("{:>4} {:>4} {:>6} x {:<1} {:>10} {:>7.1f}%", c.n, c.k, per_side, per_side,
                 padded, util);
  }
}

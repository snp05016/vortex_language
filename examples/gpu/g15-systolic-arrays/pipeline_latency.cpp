// How pipeline fill and drain overhead amortizes across back-to-back tiles
// on a K x K output-stationary systolic array.
//
// systolic_sim.cpp derives that one K x K times K x K product costs
// 3K - 2 cycles on a K x K array: K - 1 cycles for the first operands to
// reach the far corner, K cycles of every cell doing useful work, and
// K - 1 more to drain the last partial sums out. Feeding T products through
// the same array back-to-back overlaps their fill and drain: the array
// only needs to fill once, then does K cycles of work per tile, then
// drains once. This model (fill once, K cycles per tile, drain once) gives
// total cycles T * K + 2 * (K - 1); a single tile is the T = 1 case, which
// matches 3K - 2 exactly.

#include <cstddef>
#include <print>

constexpr int K = 8;

int main() {
  std::println("{:>4} {:>10} {:>10} {:>10}", "T", "cycles", "ideal", "util %");
  for (int t : {1, 2, 4, 8, 16, 64}) {
    const long long cycles = static_cast<long long>(t) * K + 2 * (K - 1);
    const long long ideal = static_cast<long long>(t) * K; // no fill or drain at all
    const double util = 100.0 * double(ideal) / double(cycles);
    std::println("{:>4} {:>10} {:>10} {:>9.1f}%", t, cycles, ideal, util);
  }
}

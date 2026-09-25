// Fill and drain of a K x K output-stationary systolic array, paid once for
// T tiles fed back to back.
//
// With the skewed feed of systolic_sim.cpp, cell (i, j) works on term k of
// tile q at cycle i + j + q*K + k. The next tile's operands follow the last
// term of the previous one with no gap, so each cell must hand its finished
// sum out and restart from zero between tiles (Kung's 1982 designs mark the
// first term of a new result with a tag bit for this). The program counts
// busy cells cycle by cycle, checks the cycle count against the closed form
// T*K + 2*(K - 1), and reports the fraction of cell-cycles that did work.

#include <cassert>
#include <print>

constexpr int K = 8;

int main() {
  std::println("{:>4} {:>8} {:>12} {:>8}", "T", "cycles", "busy cells", "util %");
  for (const int tiles : {1, 2, 4, 8, 16, 64}) {
    long long busy = 0;
    int last_busy_cycle = -1;
    for (int t = 0; t < tiles * K + 2 * K; ++t) {
      int active = 0;
      for (int i = 0; i < K; ++i)
        for (int j = 0; j < K; ++j) {
          const int step = t - i - j; // = q*K + k while the cell has work
          if (step >= 0 && step < tiles * K) ++active;
        }
      if (active > 0) last_busy_cycle = t;
      busy += active;
    }
    const int cycles = last_busy_cycle + 1;
    assert(cycles == tiles * K + 2 * (K - 1));
    assert(busy == static_cast<long long>(tiles) * K * K * K);
    const double util = 100.0 * double(busy) / (double(K) * K * cycles);
    std::println("{:>4} {:>8} {:>12} {:>7.1f}%", tiles, cycles, busy, util);
  }
}

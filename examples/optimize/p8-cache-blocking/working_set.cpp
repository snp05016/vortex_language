// A tile-size cost model: turn a cache budget into the largest square tile
// that fits, then check the answer by measuring the footprint directly.
//
// Blocking three square T x T tiles at once (one from each of A, B and C,
// the classic sub-matrix scheme) touches 3 * T * T elements, so an f32 tile
// fits a budget of `budget` bytes when 12 * T * T <= budget. The model
// solves that inequality for T; the sweep below then tries every T from 8
// to 256 and confirms which ones the model would have accepted.
//
// The two budgets are the owner's Apple M4 Pro P-core L1d and L2, read with
// `sysctl hw.perflevel0.l1dcachesize hw.perflevel0.l2cachesize` on
// 2026-09-24: 131072 bytes (128 KiB) and 16777216 bytes (16 MiB, shared by
// four P-cores). This model ignores everything else that competes for that
// space (the stack, other tiles in flight, prefetcher state), so its answer
// is a starting point, not a guarantee; P8 discusses why.
//
// Follows: the sub-matrix blocking scheme and its footprint accounting in
// Lam, Rothberg and Wolf, "The Cache Performance and Optimizations of
// Blocked Algorithms", ASPLOS 1991, section 3.

#include <cstdint>
#include <print>

constexpr std::int64_t bytes_per_element = 4;  // f32

std::int64_t footprint(std::int64_t tile) {
  return 3 * tile * tile * bytes_per_element;
}

// Largest tile whose footprint is at most `budget`, found by bisection
// (footprint is monotone in tile, so this always lands on the right value).
std::int64_t largest_tile(std::int64_t budget) {
  std::int64_t low = 0, high = 4096;  // footprint(4096) is far past any budget here
  while (low < high) {
    std::int64_t mid = low + (high - low + 1) / 2;
    if (footprint(mid) <= budget)
      low = mid;
    else
      high = mid - 1;
  }
  return low;
}

int main() {
  constexpr std::int64_t l1d = 131072;
  constexpr std::int64_t l2 = 16777216;

  std::int64_t best_l1 = largest_tile(l1d);
  std::int64_t best_l2 = largest_tile(l2);
  std::println("L1d budget {} B: largest tile {} ({} B, {:.1f}% of budget)",
               l1d, best_l1, footprint(best_l1), 100.0 * static_cast<double>(footprint(best_l1)) / static_cast<double>(l1d));
  std::println("L2  budget {} B: largest tile {} ({} B, {:.1f}% of budget)",
               l2, best_l2, footprint(best_l2), 100.0 * static_cast<double>(footprint(best_l2)) / static_cast<double>(l2));

  std::println("");
  std::println("{:>5} {:>10} {:>6} {:>6}", "tile", "bytes", "in L1", "in L2");
  for (std::int64_t t = 8; t <= 256; t *= 2) {
    std::int64_t f = footprint(t);
    std::println("{:>5} {:>10} {:>6} {:>6}", t, f, f <= l1d ? "yes" : "no", f <= l2 ? "yes" : "no");
  }
}

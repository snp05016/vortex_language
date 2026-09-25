// A tile-size model: turn a cache budget into the largest square block B
// whose working set fits, under two ways of counting that working set.
//
// "reused": the blocked ikj loop of Lam, Rothberg and Wolf keeps one B x B
// block of b and one B-long row segment of c in cache while a[i][k] sits in
// a register, so it needs B * B + B elements.
// "three tiles": a looser count that keeps a B x B tile of each of a, b and
// c, 3 * B * B elements, whatever the loop order inside the tile.
//
// Budgets: the toy cache of miss_sweep.cpp (4 KiB), and the owner's Apple
// M4 Pro P-core L1d and L2, read with `sysctl hw.perflevel0.l1dcachesize
// hw.perflevel0.l2cachesize` on 2026-09-24. The model knows capacity only:
// not associativity, not the matrix's row length, so it gives an upper
// bound to test, not an answer.
//
// Follows: Lam, Rothberg and Wolf, "The Cache Performance and Optimizations
// of Blocked Algorithms", ASPLOS 1991, section 1.1 (what the blocked loop
// keeps in cache).

#include <cstdint>
#include <print>

constexpr std::int64_t bytes_per_element = 4;  // f32

std::int64_t reused(std::int64_t b) { return (b * b + b) * bytes_per_element; }
std::int64_t three_tiles(std::int64_t b) { return 3 * b * b * bytes_per_element; }

// The largest B with footprint(B) <= budget. Both footprints grow with B,
// so counting up until the next B no longer fits finds it.
std::int64_t largest(std::int64_t (*footprint)(std::int64_t), std::int64_t budget) {
  std::int64_t b = 0;
  while (footprint(b + 1) <= budget) ++b;
  return b;
}

int main() {
  struct Budget {
    const char *name;
    std::int64_t bytes;
  };
  const Budget budgets[] = {{"toy cache", 4096}, {"M4 Pro L1d", 131072}, {"M4 Pro L2", 16777216}};
  std::println("{:<11} {:>9} | {:>6} {:>10} | {:>6} {:>10}", "budget", "bytes", "B", "reused",
               "B", "3 tiles");
  for (const Budget &budget : budgets) {
    std::int64_t r = largest(reused, budget.bytes);
    std::int64_t t = largest(three_tiles, budget.bytes);
    std::println("{:<11} {:>9} | {:>6} {:>10} | {:>6} {:>10}", budget.name, budget.bytes, r,
                 reused(r), t, three_tiles(t));
  }
}

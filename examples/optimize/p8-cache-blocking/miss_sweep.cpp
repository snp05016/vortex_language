// A tile-size sweep you can run anywhere: count the misses of a simulated
// cache, not the time of a real one, so the output is the same on every
// machine. The toy cache is 4 KiB, 32-byte lines, 2-way set-associative,
// least recently used: 64 sets, so addresses 2,048 bytes apart share a set.
//
// The kernel is the ikj matrix multiplication and its blocked form in the
// loop order Lam, Rothberg and Wolf study: k and j strip-mined by B, their
// strip loops moved outside i, so one B x B block of b is reused by every
// row i. "copied" first copies that block into a contiguous buffer. a[i][k]
// is touched once per (i, k), as if held in a register across the j loop.
// Matrices a, b and c sit back to back from address 0, as one allocation.
//
// Follows: Lam, Rothberg and Wolf, "The Cache Performance and Optimizations
// of Blocked Algorithms", ASPLOS 1991, sections 1, 3 and 6.

#include <algorithm>
#include <cstdint>
#include <format>
#include <print>
#include <string>
#include <vector>

constexpr std::int64_t line_bytes = 32, ways = 2, sets = 64;

struct Cache {
  std::vector<std::int64_t> tag = std::vector<std::int64_t>(sets * ways, -1);
  std::vector<std::int64_t> used = std::vector<std::int64_t>(sets * ways, 0);
  std::int64_t now = 0, misses = 0;
  void touch(std::int64_t address) {
    std::int64_t line = address / line_bytes, set = line % sets;
    std::int64_t *t = &tag[set * ways], *u = &used[set * ways];
    int victim = 0;
    ++now;
    for (int w = 0; w < ways; ++w) {
      if (t[w] == line) { u[w] = now; return; }
      if (u[w] < u[victim]) victim = w;  // least recently used way
    }
    ++misses;
    t[victim] = line;
    u[victim] = now;
  }
};

// Misses per 1,000 multiply-adds. block == 0 means the untiled ikj kernel.
std::int64_t misses(int n, int block, bool copy) {
  Cache cache;
  const std::int64_t size = std::int64_t{n} * n * 4;
  auto a = [&](int i, int k) { return (std::int64_t{i} * n + k) * 4; };
  auto b = [&](int k, int j) { return size + (std::int64_t{k} * n + j) * 4; };
  auto c = [&](int i, int j) { return 2 * size + (std::int64_t{i} * n + j) * 4; };
  const int step = block == 0 ? n : block;
  for (int k0 = 0; k0 < n; k0 += step)
    for (int j0 = 0; j0 < n; j0 += step) {
      const int k1 = std::min(k0 + step, n), j1 = std::min(j0 + step, n);
      // The buffer's row length is the block's width, chosen here, not n.
      auto buffer = [&](int k, int j) {
        return 3 * size + (std::int64_t{k - k0} * (j1 - j0) + (j - j0)) * 4;
      };
      if (copy)
        for (int k = k0; k < k1; ++k)
          for (int j = j0; j < j1; ++j) { cache.touch(b(k, j)); cache.touch(buffer(k, j)); }
      for (int i = 0; i < n; ++i)
        for (int k = k0; k < k1; ++k) {
          cache.touch(a(i, k));
          for (int j = j0; j < j1; ++j) {
            cache.touch(copy ? buffer(k, j) : b(k, j));
            cache.touch(c(i, j));
          }
        }
    }
  return cache.misses * 1000 / (std::int64_t{n} * n * n);
}

int main() {
  std::println("misses per 1000 multiply-adds, toy cache 4 KiB, 2-way, 32-byte lines");
  std::println("{:>8} | {:>13} | {:>13} | {:>13}", "", "N = 128", "N = 129", "N = 136");
  std::println("{:>8} | {:>6} {:>6} | {:>6} {:>6} | {:>6} {:>6}", "", "tiled", "copied",
               "tiled", "copied", "tiled", "copied");
  for (int block : {0, 8, 12, 16, 24, 32, 48}) {
    std::string row = block == 0 ? "untiled" : std::format("B = {}", block);
    std::print("{:>8}", row);
    for (int n : {128, 129, 136}) {
      // Copying is meaningless without blocks, so the untiled row has no copy column.
      std::string copied = block == 0 ? "-" : std::format("{}", misses(n, block, true));
      std::print(" | {:>6} {:>6}", misses(n, block, false), copied);
    }
    std::println("");
  }
}

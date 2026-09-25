// Five ways to choose two tile sizes for one blocked matrix multiplication,
// all scored by the same cost: misses in a simulated cache. The cost is a
// count, not a time, so the output is the same on every machine.
//
// Toy cache: 4 KiB, 2-way, 32-byte lines, least recently used (P8's toy).
// Kernel: c += a * b for 80 x 80 f32 matrices stored back to back, with j
// and k strip-mined by bj and bk and both strip loops outside i, so one
// bk x bj block of b is reused by every row i.
//
// Follows: Yotov et al., "Is Search Really Necessary to Generate
// High-Performance BLAS?", Proc. IEEE 2005, sections III-B and IV-B (the
// orthogonal line search, and the fit inequality the model uses).

#include <algorithm>
#include <array>
#include <cstdint>
#include <format>
#include <map>
#include <print>
#include <string_view>
#include <utility>
#include <vector>

constexpr int n = 80, line = 32, ways = 2, cache_bytes = 4096;
constexpr std::array<int, 6> sizes = {2, 4, 8, 16, 32, 64};

std::int64_t simulate(int bk, int bj) {
  constexpr int sets = cache_bytes / (ways * line);
  std::vector<std::int64_t> tag(sets * ways, -1), used(sets * ways, 0);
  std::int64_t clock = 0, misses = 0;
  auto touch = [&](std::int64_t address) {
    std::int64_t block = address / line;
    int set = int(block % sets), victim = set * ways;
    for (int w = set * ways; w < (set + 1) * ways; ++w) {
      if (tag[w] == block) { used[w] = ++clock; return; }
      if (used[w] < used[victim]) victim = w;
    }
    ++misses;
    tag[victim] = block;
    used[victim] = ++clock;
  };
  const std::int64_t a = 0, b = 4 * n * n, c = 8 * n * n;
  for (int jj = 0; jj < n; jj += bj)
    for (int kk = 0; kk < n; kk += bk)
      for (int i = 0; i < n; ++i)
        for (int k = kk; k < std::min(kk + bk, n); ++k) {
          touch(a + 4 * (i * n + k));  // a[i][k] stays in a register over j
          for (int j = jj; j < std::min(jj + bj, n); ++j) {
            touch(b + 4 * (k * n + j));
            touch(c + 4 * (i * n + j));
          }
        }
  return misses;
}

// Every strategy asks through this table, so a point tried twice is
// simulated once and counted once, as ATLAS keeps its timings in files.
std::map<std::pair<int, int>, std::int64_t> tried;
std::int64_t cost(int bk, int bj) {
  auto [it, fresh] = tried.try_emplace({bk, bj}, 0);
  if (fresh) it->second = simulate(bk, bj);
  return it->second;
}

struct Pick { int bk, bj; std::int64_t misses; };
void keep_best(Pick& best, int bk, int bj) {
  std::int64_t m = cost(bk, bj);
  if (best.misses < 0 || m < best.misses) best = {bk, bj, m};
}
void report(std::string_view name, const Pick& p) {
  std::println("{:<24}{:>3} tried  bk={:<2} bj={:<2} misses={}", name,
               tried.size(), p.bk, p.bj, p.misses);
  tried.clear();
}

int main() {
  std::println("misses; rows bk, columns bj = 2 4 8 16 32 64");
  for (int bk : sizes) {
    std::print("bk={:<2}", bk);
    for (int bj : sizes) std::print("{:>7}", simulate(bk, bj));
    std::println("");
  }

  Pick grid{0, 0, -1};  // try every pair
  for (int bk : sizes) for (int bj : sizes) keep_best(grid, bk, bj);
  report("grid search", grid);

  Pick lines{0, 0, -1};  // bk first with bj at 64, then bj with that bk
  for (int bk : sizes) keep_best(lines, bk, 64);
  for (int bj : sizes) keep_best(lines, lines.bk, bj);
  report("orthogonal line search", lines);

  for (std::uint64_t seed : {1, 2, 3}) {  // 8 draws each, fixed seeds
    Pick sample{0, 0, -1};
    std::uint64_t state = seed;
    for (int draw = 0; draw < 8; ++draw) {
      state = state * 6364136223846793005u + 1442695040888963407u;
      int pick = int((state >> 33) % (sizes.size() * sizes.size()));
      keep_best(sample, sizes[pick / sizes.size()], sizes[pick % sizes.size()]);
    }
    report(std::format("random, seed {}", seed), sample);
  }

  // Model: the largest square tile whose b block, c row segment and one
  // line of a fit in the cache, counted in lines. It runs no simulation.
  constexpr int cache_lines = cache_bytes / line, per_line = line / 4;
  int t = 0;
  for (int s : sizes)
    if (s * ((s + per_line - 1) / per_line) + (s + per_line - 1) / per_line + 1 <= cache_lines) t = s;
  std::println("{:<24}{:>3} tried  bk={:<2} bj={:<2} (predicted, not scored)", "model", 0, t, t);

  Pick local{t, t, cost(t, t)};  // then move to the best neighbour, if better
  auto at = [](int s) { return int(std::ranges::find(sizes, s) - sizes.begin()); };
  for (Pick here = local;; here = local) {
    for (auto [dr, dq] : {std::pair{-1, 0}, {1, 0}, {0, -1}, {0, 1}}) {
      int r = at(here.bk) + dr, q = at(here.bj) + dq;
      if (r >= 0 && q >= 0 && r < int(sizes.size()) && q < int(sizes.size()))
        keep_best(local, sizes[r], sizes[q]);
    }
    if (local.misses == here.misses) break;
  }
  report("model + local search", local);
}

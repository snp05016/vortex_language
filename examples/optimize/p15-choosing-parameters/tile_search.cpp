// Choosing a two-parameter tile size three ways: an analytic model, grid
// search and random search, all scored by the same cost function so the
// three strategies can be compared honestly.
//
// The toy: block a matrix-vector product y = A*x over row-block size bi and
// reduction-block size bk. Reloading x from memory costs (n*n)/bi units
// (larger row blocks reuse a resident chunk of x across more rows before it
// is evicted); switching tiles costs a fixed overhead per tile. Both bi and
// bk must fit a resident-data budget. n, the budget and the overhead weight
// are toy constants, not measurements: see the chapter for the real cache
// facts they stand in for.
//
// Follows: no external source; the cost function is original to this toy.

#include <array>
#include <cstdint>
#include <limits>
#include <print>
#include <random>

constexpr std::int64_t n = 1024;
constexpr std::int64_t budget = 512;          // elements resident at once
constexpr std::int64_t overhead_weight = 4;   // cost units per tile switch
constexpr std::array<std::int64_t, 9> candidates = {4, 8, 16, 32, 64, 128, 256, 512, 1024};
constexpr std::int64_t infeasible = std::numeric_limits<std::int64_t>::max();

struct Choice { std::int64_t bi, bk, cost; };

std::int64_t cost(std::int64_t bi, std::int64_t bk) {
  if (bi + bk > budget) return infeasible;
  std::int64_t traffic = (n * n) / bi;
  std::int64_t switches = overhead_weight * (n / bi) * (n / bk);
  return traffic + switches;
}

// Grid search: every (bi, bk) pair on the candidate grid.
Choice grid_search(std::int64_t& evaluated, std::int64_t& feasible) {
  Choice best{0, 0, infeasible};
  for (std::int64_t bi : candidates) {
    for (std::int64_t bk : candidates) {
      ++evaluated;
      std::int64_t c = cost(bi, bk);
      if (c == infeasible) continue;
      ++feasible;
      if (c < best.cost) best = {bi, bk, c};
    }
  }
  return best;
}

// Random search: a fixed number of pairs, chosen by a seeded generator so
// the run is repeatable.
Choice random_search(std::int64_t attempts, std::int64_t& feasible) {
  std::mt19937 rng(20260924);
  std::uniform_int_distribution<std::size_t> pick(0, candidates.size() - 1);
  Choice best{0, 0, infeasible};
  for (std::int64_t i = 0; i < attempts; ++i) {
    std::int64_t bi = candidates[pick(rng)];
    std::int64_t bk = candidates[pick(rng)];
    std::int64_t c = cost(bi, bk);
    if (c == infeasible) continue;
    ++feasible;
    if (c < best.cost) best = {bi, bk, c};
  }
  return best;
}

// The model: reason about the cost function instead of trying every pair.
// Growing bk only lowers cost here and only the budget caps it, so for each
// bi the best bk is the largest candidate that still fits; that leaves one
// free parameter, bi, which the model scans directly.
Choice model_choice(std::int64_t& evaluated) {
  Choice best{0, 0, infeasible};
  for (std::int64_t bi : candidates) {
    std::int64_t room = budget - bi;
    std::int64_t bk = 0;
    for (std::int64_t candidate : candidates) {
      if (candidate <= room) bk = candidate;
    }
    if (bk == 0) continue;
    ++evaluated;
    std::int64_t c = cost(bi, bk);
    if (c < best.cost) best = {bi, bk, c};
  }
  return best;
}

int main() {
  std::int64_t grid_evaluated = 0, grid_feasible = 0;
  Choice grid = grid_search(grid_evaluated, grid_feasible);
  std::println("grid:   {} evaluated, {} feasible, best bi={} bk={} cost={}",
               grid_evaluated, grid_feasible, grid.bi, grid.bk, grid.cost);

  std::int64_t random_feasible = 0;
  Choice random = random_search(12, random_feasible);
  std::println("random: 12 evaluated, {} feasible, best bi={} bk={} cost={}",
               random_feasible, random.bi, random.bk, random.cost);

  std::int64_t model_evaluated = 0;
  Choice model = model_choice(model_evaluated);
  std::println("model:  {} evaluated, best bi={} bk={} cost={}",
               model_evaluated, model.bi, model.bk, model.cost);

  std::println("model matches grid optimum: {}", model.cost == grid.cost);
  std::println("random matches grid optimum: {}", random.cost == grid.cost);
  return 0;
}

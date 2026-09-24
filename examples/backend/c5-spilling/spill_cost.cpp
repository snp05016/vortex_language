// Spill cost: why a use inside a loop should count for more than one
// outside it. Follows Chaitin's spill-cost heuristic (see the .toml).
//
// Each candidate is a live range described only by where it is touched: a
// list of (is a definition?, loop nesting depth) pairs. The classic
// heuristic weights each touch by 10 raised to its loop depth, so one use
// three loops deep outweighs a thousand uses at depth 0. The allocator
// should spill whichever candidate this cost is *lowest* for: it is the
// one that costs the least to reload every time it is needed again.
#include <cstdio>
#include <string>
#include <vector>

struct Touch {
  bool is_def;
  int loop_depth;
};

struct Candidate {
  std::string name;
  std::vector<Touch> touches;
};

// A touch at depth d is assumed to run 10^d times per call, the standard
// stand-in for "loops usually run many times" when no profile is available.
long long spill_cost(const Candidate &c) {
  long long cost = 0;
  for (const Touch &t : c.touches) {
    long long weight = 1;
    for (int i = 0; i < t.loop_depth; ++i) weight *= 10;
    cost += weight;
  }
  return cost;
}

int main() {
  std::vector<Candidate> candidates = {
      // A loop-invariant temporary: defined once outside any loop, used
      // once outside any loop. Touching it is always cheap.
      {"loop_bound", {{true, 0}, {false, 0}}},
      // A row base pointer: defined once outside the k-loop, then read on
      // every trip around it (depth 1).
      {"row_base", {{true, 0}, {false, 1}, {false, 1}, {false, 1}}},
      // The matmul accumulator: defined before the k-loop, updated and
      // read on every trip around it (depth 1), used once after.
      {"accumulator", {{true, 0}, {false, 1}, {true, 1}, {false, 0}}},
  };

  long long cheapest = -1;
  std::string cheapest_name;
  for (const Candidate &c : candidates) {
    long long cost = spill_cost(c);
    std::printf("%-12s cost %lld\n", c.name.c_str(), cost);
    if (cheapest == -1 || cost < cheapest) {
      cheapest = cost;
      cheapest_name = c.name;
    }
  }
  std::printf("spill: %s\n", cheapest_name.c_str());
  return 0;
}

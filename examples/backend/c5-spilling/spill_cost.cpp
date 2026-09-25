// Spill cost for the general-purpose values of a small matrix kernel, the
// way Chaitin estimates it: every definition and use of a value counts once,
// weighted by how often it is expected to run. Chaitin assumes a loop body
// runs ten times as often as the code around it, so a touch at loop depth d
// weighs 10^d. Follows: Chaitin, "Register allocation & spilling via graph
// coloring", section 5 (see the .toml).
//
// Fixed-shape arrays give a second option: the trip counts are known, so the
// weight can be the exact number of times the touch runs. Both are printed.
#include <cstdio>
#include <string>
#include <vector>

struct Candidate {
  std::string name;
  std::vector<int> depths;  // loop depth of each definition and use
};

int main() {
  // Loops: row (depth 1, 2 trips), column (depth 2, 2 trips), k (depth 3,
  // 3 trips). A body at depth 3 therefore runs 2 * 2 * 3 = 12 times.
  const long exact_runs[] = {1, 2, 4, 12};

  // Touches as listed in the chapter's table for a plain lowering: a loop's
  // compare and increment sit in its own body, at its own depth.
  const std::vector<Candidate> candidates = {
      {"a", {0, 3}},
      {"b", {0, 3}},
      {"c", {0, 2}},
      {"row", {0, 1, 3, 2, 1, 1}},
      {"column", {1, 2, 3, 2, 2, 2}},
      {"k", {2, 3, 3, 3, 3, 3}},
  };

  std::string cheapest;
  long cheapest_cost = -1;
  std::printf("value    10^depth  exact\n");
  for (const Candidate &c : candidates) {
    long guess = 0, exact = 0;
    for (int d : c.depths) {
      long w = 1;
      for (int i = 0; i < d; ++i) w *= 10;
      guess += w;
      exact += exact_runs[d];
    }
    std::printf("%-8s %8ld  %5ld\n", c.name.c_str(), guess, exact);
    // All six are live together in the k loop, so every node has the same
    // degree and Chaitin's cost / degree picks the same value as cost alone.
    if (cheapest_cost < 0 || guess < cheapest_cost) {
      cheapest_cost = guess;
      cheapest = c.name;
    }
  }
  std::printf("spill first: %s\n", cheapest.c_str());
  return 0;
}

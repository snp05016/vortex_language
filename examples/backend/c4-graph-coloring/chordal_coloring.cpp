// Coloring a chordal interference graph in the order maximum cardinality
// search (MCS) finds, versus coloring the same graph in an arbitrary order.
// An SSA-form program's interference graph is always chordal (Hack): every
// cycle of length 4 or more has a chord, an edge joining two
// non-consecutive nodes on the cycle. MCS visits each node next by picking
// whichever unvisited node has the most already-visited neighbors; coloring
// greedily in the reverse of that order never uses more colors than the
// graph's largest clique needs, with no backtracking. Coloring the same
// chordal graph in a bad order can waste a color.
//
// The five nodes below model live ranges from an SSA-form loop after
// renaming: "e" is live across the whole loop (it interferes with
// everything), and "a", "b", "c", "d" is a path where each overlaps only
// its neighbors on the path, so a and c never interfere (nor b and d),
// which is what a chord-free-cycle graph would lack and what SSA guarantees
// here.
//
// Follows: Pereira, Palsberg, "Register Allocation via Coloring of Chordal
// Graphs", APLAS 2005, section 3 (maximum cardinality search and the
// greedy coloring theorem).

#include <algorithm>
#include <print>
#include <set>
#include <string>
#include <vector>

const std::vector<std::string> nodes = {"a", "b", "c", "d", "e"};
const std::vector<std::pair<std::string, std::string>> edges = {
    {"a", "b"}, {"b", "c"}, {"c", "d"},
    {"e", "a"}, {"e", "b"}, {"e", "c"}, {"e", "d"}};

std::set<std::string> neighbors(const std::string& n) {
  std::set<std::string> out;
  for (auto& [x, y] : edges) {
    if (x == n) out.insert(y);
    if (y == n) out.insert(x);
  }
  return out;
}

std::vector<std::string> maximum_cardinality_search() {
  std::vector<std::string> order;
  std::set<std::string> visited;
  while (order.size() < nodes.size()) {
    std::string best;
    int best_count = -1;
    for (auto& n : nodes) {
      if (visited.count(n)) continue;
      int count = 0;
      for (auto& m : neighbors(n))
        if (visited.count(m)) ++count;
      if (count > best_count) { best = n; best_count = count; }
    }
    order.push_back(best);
    visited.insert(best);
  }
  return order;
}

// Colors `order` greedily, each node taking the lowest color its
// already-colored neighbors do not use. Returns the number of colors used.
int greedy_color(const std::vector<std::string>& order, bool report) {
  std::vector<std::pair<std::string, int>> color;
  int max_color = -1;
  for (auto& n : order) {
    std::set<int> used;
    for (auto& m : neighbors(n))
      for (auto& [cn, c] : color)
        if (cn == m) used.insert(c);
    int picked = 0;
    while (used.count(picked)) ++picked;
    color.push_back({n, picked});
    max_color = std::max(max_color, picked);
    if (report) std::println("  {}: color {}", n, picked);
  }
  return max_color + 1;
}

int main() {
  auto mcs = maximum_cardinality_search();
  std::print("MCS order:");
  for (auto& n : mcs) std::print(" {}", n);
  std::println("");

  std::println("Reverse-MCS coloring:");
  std::vector<std::string> reverse_mcs(mcs.rbegin(), mcs.rend());
  int used_reverse = greedy_color(reverse_mcs, true);
  std::println("  colors used: {}", used_reverse);

  std::println("Arbitrary-order coloring (a, e, b, d, c):");
  std::vector<std::string> arbitrary = {"a", "e", "b", "d", "c"};
  int used_arbitrary = greedy_color(arbitrary, true);
  std::println("  colors used: {}", used_arbitrary);
}

// Coloring a chordal graph in the order maximum cardinality search (MCS)
// visits it, against coloring the same graph in an arbitrary order.
//
// MCS repeatedly visits the unvisited node with the most visited
// neighbors. On a chordal graph the visit order is a simplicial
// elimination ordering: when a node is visited, its already-visited
// neighbors are all adjacent to one another. Greedy coloring in that same
// order therefore never needs more colors than the largest clique.
//
// The graph: "e" is adjacent to everything, and a-b-c-d is a path. Every
// cycle of four or more nodes passes through e, and e is joined to every
// other node on it, so every such cycle has a chord: the graph is chordal.
// Its largest cliques are triangles such as {e, a, b}.
//
// Follows: Pereira, Palsberg, "Register Allocation via Coloring of Chordal
// Graphs", APLAS 2005, section 3 (greedy coloring and MCS).

#include <print>
#include <set>
#include <string>
#include <utility>
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

// Ties go to the node listed first, so the output is deterministic.
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

// Each node takes the lowest color its already-colored neighbors do not
// use. Returns the number of colors used.
int greedy_color(const std::vector<std::string>& order) {
  std::vector<std::pair<std::string, int>> color;
  int colors = 0;
  for (auto& n : order) {
    std::set<int> used;
    for (auto& m : neighbors(n))
      for (auto& [cn, c] : color)
        if (cn == m) used.insert(c);
    int picked = 0;
    while (used.count(picked)) ++picked;
    color.push_back({n, picked});
    if (picked + 1 > colors) colors = picked + 1;
    std::print(" {}={}", n, picked);
  }
  std::println("");
  return colors;
}

int main() {
  auto mcs = maximum_cardinality_search();
  std::print("MCS order:");
  for (auto& n : mcs) std::print(" {}", n);
  std::println("");

  std::print("greedy in MCS order:");
  std::println("  colors used: {}", greedy_color(mcs));

  std::print("greedy in order a, d, b, c, e:");
  std::println("  colors used: {}", greedy_color({"a", "d", "b", "c", "e"}));
}

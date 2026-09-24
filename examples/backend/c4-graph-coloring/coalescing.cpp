// Briggs's conservative coalescing test: when is it safe to give two
// copy-related values (values joined by a plain register-to-register move,
// with no interference edge between them) the same color, deleting the move
// for free? Merging always risks raising some other node's degree past k
// and forcing a spill that would not otherwise happen. Briggs's rule is
// conservative rather than exact: merge a and b only if the node that
// results has fewer than k neighbors whose own degree, in the graph before
// merging, is already k or more ("significant degree"). That is always
// safe, though it sometimes refuses a merge that would have been fine.
//
// This checks the rule two ways on two small graphs: first by counting
// significant-degree neighbors directly, then by building the merged graph
// and brute-force testing whether it still has a k-coloring, to confirm the
// rule never approves a merge that breaks colorability.
//
// Follows: Briggs, Cooper, Torczon, "Improvements to graph coloring
// register allocation", TOPLAS 1994, section 4 (conservative coalescing).

#include <algorithm>
#include <functional>
#include <print>
#include <set>
#include <string>
#include <vector>

constexpr int K = 3;

struct Graph {
  std::vector<std::string> nodes;
  std::vector<std::pair<std::string, std::string>> edges;

  std::set<std::string> neighbors(const std::string& n) const {
    std::set<std::string> out;
    for (auto& [a, b] : edges) {
      if (a == n) out.insert(b);
      if (b == n) out.insert(a);
    }
    return out;
  }
  int degree(const std::string& n) const { return static_cast<int>(neighbors(n).size()); }
};

// True if a and b (not adjacent: a copy, not an interference) may be
// coalesced without risking a new spill.
bool briggs_safe(const Graph& g, const std::string& a, const std::string& b) {
  std::set<std::string> merged_neighbors = g.neighbors(a);
  for (auto& n : g.neighbors(b)) merged_neighbors.insert(n);
  merged_neighbors.erase(a);
  merged_neighbors.erase(b);
  int significant = 0;
  for (auto& n : merged_neighbors)
    if (g.degree(n) >= K) ++significant;
  return significant < K;
}

// Merges a and b into one node "ab" and returns the resulting graph.
Graph coalesce(const Graph& g, const std::string& a, const std::string& b) {
  Graph out;
  for (auto& n : g.nodes)
    if (n != a && n != b) out.nodes.push_back(n);
  out.nodes.push_back("ab");
  auto rename = [&](const std::string& n) { return (n == a || n == b) ? "ab" : n; };
  std::set<std::pair<std::string, std::string>> seen;
  for (auto& [x, y] : g.edges) {
    std::string rx = rename(x), ry = rename(y);
    if (rx == ry) continue;  // the a-b copy itself, or a self-loop after renaming
    auto key = rx < ry ? std::pair{rx, ry} : std::pair{ry, rx};
    if (seen.insert(key).second) out.edges.push_back({rx, ry});
  }
  return out;
}

bool k_colorable(const Graph& g, int k) {
  std::vector<int> color(g.nodes.size(), -1);
  std::function<bool(std::size_t)> place = [&](std::size_t i) -> bool {
    if (i == g.nodes.size()) return true;
    auto used = g.neighbors(g.nodes[i]);
    for (int c = 0; c < k; ++c) {
      bool ok = true;
      for (std::size_t j = 0; j < i; ++j)
        if (used.count(g.nodes[j]) && color[j] == c) { ok = false; break; }
      if (ok) {
        color[i] = c;
        if (place(i + 1)) return true;
        color[i] = -1;
      }
    }
    return false;
  };
  return place(0);
}

void check(const std::string& title, Graph g, const std::string& a, const std::string& b) {
  std::println("{}:", title);
  bool safe = briggs_safe(g, a, b);
  std::println("  Briggs's test: {} to coalesce {} and {}", safe ? "safe" : "unsafe", a, b);
  Graph merged = coalesce(g, a, b);
  bool colorable = k_colorable(merged, K);
  std::println("  merged graph is {}-colorable: {}", K, colorable ? "yes" : "no");
}

int main() {
  // p and q are copy-related; their combined neighbors r, s each have
  // degree 2 (< K), so neither counts as significant.
  check("Low-degree neighbors",
        {{"p", "q", "r", "s"}, {{"p", "r"}, {"q", "s"}, {"r", "s"}}}, "p", "q");

  // a and b are copy-related; their combined neighbors c, d, e all have
  // degree 3 or more (they form a triangle plus edges to a and b).
  check("High-degree neighbors",
        {{"a", "b", "c", "d", "e"},
         {{"a", "c"}, {"a", "d"}, {"b", "d"}, {"b", "e"}, {"c", "d"}, {"c", "e"}, {"d", "e"}}},
        "a", "b");
}

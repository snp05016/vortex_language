// Two conservative coalescing tests, checked against brute force.
//
// Coalescing merges two copy-related nodes (joined by a move, not by an
// interference edge) so they get one register and the move disappears.
// A merged node has the union of both nodes' edges, so a careless merge
// can make a colorable graph uncolorable. With k registers, a node of
// "significant degree" has k or more neighbors.
//
// Briggs: merge a and b if the merged node would have fewer than k
// neighbors of significant degree, counted in the merged graph.
// George: merge a into b if every neighbor of a already interferes with b
// or has insignificant degree.
//
// Both tests are safe but conservative. The third graph below is one that
// Briggs's test refuses although merging is harmless.
//
// Follows: George, Appel, "Iterated register coalescing", TOPLAS 1996,
// section 3; Bouchez, Hack, Rastello, "Register Allocation", chapter 22 of
// the SSA-based Compiler Design book draft, section 22.3.3.

#include <cstddef>
#include <functional>
#include <print>
#include <set>
#include <string>
#include <utility>
#include <vector>

constexpr int K = 3;

struct Graph {
  std::vector<std::string> nodes;
  std::vector<std::pair<std::string, std::string>> edges;

  std::set<std::string> neighbors(const std::string& n) const {
    std::set<std::string> out;
    for (auto& [x, y] : edges) {
      if (x == n) out.insert(y);
      if (y == n) out.insert(x);
    }
    return out;
  }
  std::size_t degree(const std::string& n) const { return neighbors(n).size(); }
};

// Merges a and b into one node named a + b.
Graph merge(const Graph& g, const std::string& a, const std::string& b) {
  Graph out;
  std::string ab = a + b;
  for (auto& n : g.nodes)
    if (n != a && n != b) out.nodes.push_back(n);
  out.nodes.push_back(ab);
  auto rename = [&](const std::string& n) { return n == a || n == b ? ab : n; };
  std::set<std::pair<std::string, std::string>> seen;
  for (auto& [x, y] : g.edges) {
    std::string rx = rename(x), ry = rename(y);
    std::pair<std::string, std::string> key = rx < ry ? std::pair{rx, ry} : std::pair{ry, rx};
    if (seen.insert(key).second) out.edges.push_back(key);
  }
  return out;
}

bool briggs(const Graph& g, const std::string& a, const std::string& b) {
  Graph m = merge(g, a, b);
  int significant = 0;
  for (auto& n : m.neighbors(a + b))
    if (m.degree(n) >= K) ++significant;
  return significant < K;
}

// Tries both directions: a into b, then b into a.
bool george(const Graph& g, const std::string& a, const std::string& b) {
  auto one_way = [&](const std::string& from, const std::string& into) {
    auto into_neighbors = g.neighbors(into);
    for (auto& t : g.neighbors(from))
      if (!into_neighbors.count(t) && g.degree(t) >= K) return false;
    return true;
  };
  return one_way(a, b) || one_way(b, a);
}

bool colorable(const Graph& g, int k) {
  std::vector<int> color(g.nodes.size(), -1);
  std::function<bool(std::size_t)> place = [&](std::size_t i) {
    if (i == g.nodes.size()) return true;
    auto adjacent = g.neighbors(g.nodes[i]);
    for (int c = 0; c < k; ++c) {
      bool free = true;
      for (std::size_t j = 0; j < i; ++j)
        if (adjacent.count(g.nodes[j]) && color[j] == c) free = false;
      if (free) {
        color[i] = c;
        if (place(i + 1)) return true;
      }
    }
    color[i] = -1;
    return false;
  };
  return place(0);
}

void check(const char* title, const Graph& g, const std::string& a, const std::string& b) {
  auto verdict = [](bool ok) { return ok ? "merge" : "refuse"; };
  std::println("{}: coalesce {} and {}?", title, a, b);
  std::println("  Briggs: {}   George: {}   merged graph {}-colorable: {}",
               verdict(briggs(g, a, b)), verdict(george(g, a, b)), K,
               colorable(merge(g, a, b), K) ? "yes" : "no");
}

int main() {
  check("graph 1", {{"p", "q", "r", "s"}, {{"p", "r"}, {"q", "s"}, {"r", "s"}}}, "p", "q");

  check("graph 2",
        {{"a", "b", "c", "d", "e"},
         {{"a", "c"}, {"a", "d"}, {"b", "d"}, {"b", "e"}, {"c", "d"}, {"c", "e"}, {"d", "e"}}},
        "a", "b");

  // u meets x, y and z; v meets only x. x, y and z each also meet m and n.
  check("graph 3",
        {{"u", "v", "x", "y", "z", "m", "n"},
         {{"u", "x"}, {"u", "y"}, {"u", "z"}, {"v", "x"},
          {"x", "m"}, {"x", "n"}, {"y", "m"}, {"y", "n"}, {"z", "m"}, {"z", "n"}}},
        "u", "v");
}

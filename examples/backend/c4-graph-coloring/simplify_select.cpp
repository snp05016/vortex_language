// Chaitin's simplify/select against Briggs's optimistic variant, on one
// small interference graph: a hub node "s" that interferes with four others
// that form a cycle among themselves ("p", "q", "r", "t"). With 3 colors,
// every node's degree is already 3 or more, so simplify cannot remove a node
// by the plain rule (degree < k) before doing something about that.
//
// Chaitin: when stuck, pick the highest-degree node and spill it outright.
// Briggs: when stuck, push the highest-degree node onto the stack anyway
// (an "optimistic" spill candidate) and keep going; only mark it a real
// spill if, at select time, none of the k colors remain free.
//
// Follows: Chaitin, "Register allocation & spilling via graph coloring",
// SIGPLAN 1982, section 3; Briggs, Cooper, Torczon, "Improvements to graph
// coloring register allocation", TOPLAS 1994, section 3.

#include <algorithm>
#include <array>
#include <print>
#include <set>
#include <string>
#include <vector>

constexpr int K = 3;
const std::vector<std::string> nodes = {"s", "p", "q", "r", "t"};
// s interferes with all four others; p-q-r-t-p form a 4-cycle.
const std::vector<std::pair<std::string, std::string>> edges = {
    {"s", "p"}, {"s", "q"}, {"s", "r"}, {"s", "t"},
    {"p", "q"}, {"q", "r"}, {"r", "t"}, {"t", "p"}};

std::set<std::string> neighbors(const std::string& n) {
  std::set<std::string> out;
  for (auto& [a, b] : edges) {
    if (a == n) out.insert(b);
    if (b == n) out.insert(a);
  }
  return out;
}

// Runs simplify/select. optimistic=false is Chaitin's rule (spill the stuck
// node immediately, never color it); optimistic=true is Briggs's rule (push
// it anyway, decide at select time).
void run(bool optimistic) {
  std::println("{}:", optimistic ? "Briggs (optimistic)" : "Chaitin (spill on stuck)");
  std::set<std::string> remaining(nodes.begin(), nodes.end());
  std::vector<std::pair<std::string, bool>> stack;  // (node, "must try to color")
  std::vector<std::string> spilled;

  while (!remaining.empty()) {
    auto degree_in_remaining = [&](const std::string& n) {
      int d = 0;
      for (auto& m : neighbors(n))
        if (remaining.count(m)) ++d;
      return d;
    };
    // Prefer a node with degree < K: it is always colorable later.
    std::string chosen;
    for (auto& n : nodes)
      if (remaining.count(n) && degree_in_remaining(n) < K) { chosen = n; break; }

    if (!chosen.empty()) {
      std::println("  simplify {} (degree {} < {})", chosen, degree_in_remaining(chosen), K);
      stack.push_back({chosen, true});
      remaining.erase(chosen);
      continue;
    }

    // Stuck: every remaining node has degree >= K.
    std::string worst;
    int worst_degree = -1;
    for (auto& n : nodes)
      if (remaining.count(n) && degree_in_remaining(n) > worst_degree) {
        worst = n;
        worst_degree = degree_in_remaining(n);
      }

    if (optimistic) {
      std::println("  stuck; push {} anyway (degree {})", worst, worst_degree);
      stack.push_back({worst, true});
    } else {
      std::println("  stuck; spill {} outright (degree {})", worst, worst_degree);
      spilled.push_back(worst);
      stack.push_back({worst, false});
    }
    remaining.erase(worst);
  }

  std::vector<std::pair<std::string, int>> color;  // node -> color, -1 if spilled
  while (!stack.empty()) {
    auto [n, try_color] = stack.back();
    stack.pop_back();
    if (!try_color) {
      color.push_back({n, -1});
      continue;
    }
    std::set<int> used;
    for (auto& m : neighbors(n))
      for (auto& [cn, c] : color)
        if (cn == m && c >= 0) used.insert(c);
    int picked = -1;
    for (int c = 0; c < K; ++c)
      if (!used.count(c)) { picked = c; break; }
    if (picked < 0) {
      std::println("  select {}: no color free, spill after all", n);
      spilled.push_back(n);
    } else {
      std::println("  select {}: color {}", n, picked);
    }
    color.push_back({n, picked});
  }

  if (spilled.empty())
    std::println("  result: every node colored, no spills");
  else
    std::println("  result: {} spill(s)", spilled.size());
}

int main() {
  run(false);
  run(true);
}

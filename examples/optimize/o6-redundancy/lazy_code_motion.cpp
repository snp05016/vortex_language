// Lazy code motion for one term, t = a + b, on three small flow graphs whose
// nodes are single statements. As in the paper, every edge into a node with
// several predecessors has its own empty node ("-"), so code can always be
// inserted at the entry of a node. Node 0 is the start and the last node the end.
//
//   down-safe (backward, greatest): every path from here to the end computes t
//             before a or b changes. Inserting only at down-safe nodes adds no
//             computation to any path, so no new overflow or division by zero.
//   earliest  (forward, least): on some path, no earlier node could compute
//             the same value safely.
//   delay     (forward, greatest): the insertion can still move down.
//   latest    delayed, and it uses t or some successor is not delayed.
//   isolated  (backward, greatest): an insertion here would serve only itself.
// Insert h = a + b at latest nodes that are not isolated, and replace t by h
// at every node that uses t, unless the node is both latest and isolated.
//
// Follows: Knoop, Ruthing and Steffen, "Lazy Code Motion", PLDI 1992,
// equation systems 3.5, 3.7, 4.2 and 4.7, and section 4.1.

#include <format>
#include <print>
#include <string>
#include <vector>

struct Node { std::string text; bool used, transp; std::vector<int> succ; };
using Bits = std::vector<bool>;

// Recompute every node from `start` until nothing changes.
template <class Step> Bits solve(int n, bool start, Step step) {
  Bits x(n, start);
  for (bool changed = true; changed;) {
    changed = false;
    for (int i = 0; i < n; ++i)
      if (const bool v = step(x, i); v != x[i]) x[i] = v, changed = true;
  }
  return x;
}

std::string nodes(const Bits& x) {
  std::string out;
  for (int i = 0; i < int(x.size()); ++i)
    if (x[i]) out += " " + std::to_string(i);
  return out.empty() ? " none" : out;
}

void motion(const char* title, const std::vector<Node>& g, bool table) {
  const int n = int(g.size());
  std::vector<std::vector<int>> pred(n);
  for (int i = 0; i < n; ++i)
    for (int s : g[i].succ) pred[s].push_back(i);
  const Bits safe = solve(n, true, [&](const Bits& x, int i) {
    if (i == n - 1) return false;
    bool all = true;
    for (int s : g[i].succ) all = all && x[s];
    return g[i].used || (g[i].transp && all);
  });
  const Bits early = solve(n, false, [&](const Bits& x, int i) {
    bool any = i == 0;
    for (int p : pred[i]) any = any || !g[p].transp || (!safe[p] && x[p]);
    return any;
  });
  const Bits delay = solve(n, true, [&](const Bits& x, int i) {
    bool all = i != 0;
    for (int p : pred[i]) all = all && !g[p].used && x[p];
    return (safe[i] && early[i]) || all;
  });
  Bits latest(n), busy(n), insert(n), replace(n);
  for (int i = 0; i < n; ++i) {
    bool all = true;
    for (int s : g[i].succ) all = all && delay[s];
    latest[i] = delay[i] && (g[i].used || !all);
  }
  const Bits isolated = solve(n, true, [&](const Bits& x, int i) {
    bool all = true;
    for (int s : g[i].succ) all = all && (latest[s] || (!g[s].used && x[s]));
    return all;
  });
  for (int i = 0; i < n; ++i) {
    busy[i] = safe[i] && early[i];
    insert[i] = latest[i] && !isolated[i];
    replace[i] = g[i].used && !(latest[i] && isolated[i]);
  }
  std::println("{}", title);
  if (table) std::println("  node  statement   down-safe earliest delay latest isolated");
  for (int i = 0; table && i < n; ++i) {
    auto mark = [&](const Bits& b) { return b[i] ? "x" : "."; };
    std::string row = std::format("  {:>4}  {:<11} {:^9} {:^8} {:^5} {:^6} {:^8}", i, g[i].text,
                                  mark(safe), mark(early), mark(delay), mark(latest), mark(isolated));
    std::println("{}", row.erase(row.find_last_not_of(' ') + 1));
  }
  std::println("  earliest placement inserts h = a + b at:{}", nodes(busy));
  std::println("  lazy placement inserts h = a + b at:{}", nodes(insert));
  std::println("  and replaces a + b by h at:{}", nodes(replace));
}

int main() {
  // A partially redundant a + b: computed on the left arm and after the join.
  motion("diamond", {{"start", 0, 1, {1}}, {"a = input", 0, 0, {2}}, {"c ?", 0, 1, {3, 5}},
                     {"x = a + b", 1, 1, {4}}, {"-", 0, 1, {6}}, {"-", 0, 1, {6}},
                     {"y = a + b", 1, 1, {7}}, {"end", 0, 1, {}}}, true);
  // An invariant a + b in a loop tested at the top, which may run zero times.
  motion("while loop", {{"start", 0, 1, {1}}, {"a = input", 0, 0, {2}}, {"-", 0, 1, {3}},
                        {"i < n ?", 0, 1, {4, 7}}, {"x = a + b", 1, 1, {5}},
                        {"i = i + 1", 0, 1, {6}}, {"-", 0, 1, {3}}, {"end", 0, 1, {}}}, false);
  // The same loop rotated: a guard, then a body that runs at least once.
  motion("rotated loop", {{"start", 0, 1, {1}}, {"a = input", 0, 0, {2}}, {"0 < n ?", 0, 1, {3, 9}},
                          {"-", 0, 1, {4}}, {"x = a + b", 1, 1, {5}}, {"i = i + 1", 0, 1, {6}},
                          {"i < n ?", 0, 1, {7, 8}}, {"-", 0, 1, {4}}, {"-", 0, 1, {10}},
                          {"-", 0, 1, {10}}, {"end", 0, 1, {}}}, false);
}

// The iterative dominator algorithm of Cooper, Harvey and Kennedy, run twice on
// one control-flow graph: once visiting the blocks in reverse postorder, once
// in postorder. Both runs find the same immediate dominators. The order only
// decides how many passes over the blocks that takes.
//
// The graph is the while loop from guide stage 7, one letter per block:
//   A: count = 0; total = 0       B: count < 10 ?
//   C: count += 1; count even ?   D: total > 10 ?
//   E: total += count             F: print(total)
//
// doms[b] holds b's immediate dominator, so the whole dominator tree lives in
// one array. intersect() climbs that tree from two blocks until they meet,
// comparing postorder numbers: a block's dominators all have higher ones.
//
// Follows: Cooper, Harvey and Kennedy, "A Simple, Fast Dominance Algorithm",
// figure 3 (the engineered algorithm).

#include <array>
#include <print>
#include <vector>

constexpr int N = 6, entry = 0, none = -1;
constexpr char name[N] = {'A', 'B', 'C', 'D', 'E', 'F'};
const std::array<std::vector<int>, N> succ = {{{1}, {2, 5}, {1, 3}, {5, 4}, {1}, {}}};
std::array<std::vector<int>, N> pred;
std::array<int, N> number;  // postorder number of each block
std::vector<int> postorder;

void depth_first(int b, std::array<bool, N> &seen) {
  seen[b] = true;
  for (int s : succ[b])
    if (!seen[s]) depth_first(s, seen);
  number[b] = static_cast<int>(postorder.size());
  postorder.push_back(b);  // b finishes after everything it reached first
}

int solve(const std::vector<int> &order, std::array<int, N> &doms) {
  doms.fill(none);
  doms[entry] = entry;
  auto intersect = [&](int f1, int f2) {
    while (f1 != f2) {
      while (number[f1] < number[f2]) f1 = doms[f1];
      while (number[f2] < number[f1]) f2 = doms[f2];
    }
    return f1;
  };
  int passes = 0;
  for (bool changed = true; changed; ++passes) {
    changed = false;
    for (int b : order) {
      if (b == entry) continue;
      int idom = none;
      for (int p : pred[b])
        if (doms[p] != none) idom = idom == none ? p : intersect(p, idom);
      if (idom != none && doms[b] != idom) {
        doms[b] = idom;
        changed = true;
      }
    }
  }
  return passes;  // the last pass is the one that changed nothing
}

void print_tree(const std::array<int, N> &doms, int b, int depth) {
  std::println("{:{}}{}", "", 2 * depth, name[b]);
  for (int c = 0; c < N; ++c)
    if (c != entry && doms[c] == b) print_tree(doms, c, depth + 1);
}

int main() {
  for (int b = 0; b < N; ++b)
    for (int s : succ[b]) pred[s].push_back(b);
  std::array<bool, N> seen{};
  depth_first(entry, seen);
  const std::vector<int> rpo(postorder.rbegin(), postorder.rend());
  std::print("reverse postorder is");
  for (int b : rpo) std::print(" {}", name[b]);
  std::println("");

  std::array<int, N> doms;
  for (const auto &[label, order] : {std::pair{"reverse postorder", rpo}, std::pair{"postorder", postorder}}) {
    const int passes = solve(order, doms);
    std::print("{:<18} {} passes, idom:", label, passes);
    for (int b = 1; b < N; ++b) std::print(" {}={}", name[b], name[doms[b]]);
    std::println("");
  }
  std::println("dominator tree:");
  print_tree(doms, entry, 1);
}

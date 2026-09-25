// Dominance frontiers by the join-point walk of Cooper, Harvey and Kennedy,
// then the iterated frontier, which tells SSA construction where phis go.
//
// Same graph as dominators.cpp, whose immediate dominators are copied below.
// Only a join block j, one with two or more predecessors, can be in a
// frontier. From each predecessor of j, walk up the dominator tree and stop at
// j's immediate dominator: j is in the frontier of every block the walk
// passes, because each dominates a predecessor of j but not j itself.
//
// A variable assigned in several blocks needs a phi at each block of the
// iterated frontier of those blocks, counting the entry as one of them. A
// phi is itself an assignment, so its block's frontier is added in turn.
// count is assigned in A and C, total in A and E.
//
// Follows: Cooper, Harvey and Kennedy, "A Simple, Fast Dominance Algorithm",
// figure 5; Cytron et al., TOPLAS 1991, section 4.3, theorem 2.

#include <array>
#include <print>
#include <set>
#include <utility>
#include <vector>

constexpr int N = 6;
constexpr char name[N] = {'A', 'B', 'C', 'D', 'E', 'F'};
const std::array<std::vector<int>, N> succ = {{{1}, {2, 5}, {1, 3}, {5, 4}, {1}, {}}};
constexpr std::array<int, N> idom = {0, 0, 1, 2, 3, 1};  // A is the entry

void print_blocks(const std::set<int> &blocks) {
  for (int b : blocks) std::print(" {}", name[b]);
}

int main() {
  std::array<std::vector<int>, N> pred;
  for (int b = 0; b < N; ++b)
    for (int s : succ[b]) pred[s].push_back(b);

  std::array<std::set<int>, N> df;
  for (int j = 0; j < N; ++j) {
    if (pred[j].size() < 2) continue;
    for (int p : pred[j])
      for (int runner = p; runner != idom[j]; runner = idom[runner]) df[runner].insert(j);
  }
  for (int b = 0; b < N; ++b) {
    std::print("DF({}) = {{", name[b]);
    print_blocks(df[b]);
    std::println(" }}");
  }

  for (const auto &[variable, assigned] :
       {std::pair{"count", std::vector{0, 2}}, std::pair{"total", std::vector{0, 4}}}) {
    std::set<int> phis;
    std::vector<int> work = assigned;
    while (!work.empty()) {
      const int x = work.back();
      work.pop_back();
      for (int y : df[x])
        if (phis.insert(y).second) work.push_back(y);
    }
    std::print("{}: phis at", variable);
    print_blocks(phis);
    std::println("");
  }
}

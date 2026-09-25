// Building an interference graph from liveness, one instruction at a time,
// and comparing it with the graph that live intervals give.
//
// The rule: walk the block backwards keeping the set of values live after
// each instruction. At an instruction that defines d, add an edge from d to
// every other value live after it. An instruction reads its operands before
// it writes its result, so a value whose last use is this instruction is
// not live after it and gets no edge to d. (A plain copy d = s would also
// skip s; this block has no copies.)
//
// Intervals [definition, last use] that share an endpoint count as
// overlapping, so the interval graph joins d to a and b at position 4, and
// e to c and d at position 5. Those four extra edges cost a register.
//
// Follows: Pfenning, Platzer, CMU 15-411 lecture 3 notes, section 2
// (building the interference graph).

#include <algorithm>
#include <cstddef>
#include <print>
#include <set>
#include <string>
#include <utility>
#include <vector>

struct Instruction {
  std::string def;  // empty for the return
  std::vector<std::string> uses;
};

// 1 a = 2; 2 b = 3; 3 c = a + b; 4 d = a * b; 5 e = c + d; 6 return e
const std::vector<Instruction> block = {
    {"a", {}}, {"b", {}}, {"c", {"a", "b"}},
    {"d", {"a", "b"}}, {"e", {"c", "d"}}, {"", {"e"}}};

using Edges = std::set<std::pair<std::string, std::string>>;

void add_edge(Edges& edges, const std::string& x, const std::string& y) {
  if (x != y) edges.insert(x < y ? std::pair{x, y} : std::pair{y, x});
}

void print_edges(const char* title, const Edges& edges) {
  std::print("{} ({}):", title, edges.size());
  for (auto& [x, y] : edges) std::print(" {}-{}", x, y);
  std::println("");
}

int main() {
  // Backward pass: live_after[i] is the set live just after instruction i.
  std::vector<std::set<std::string>> live_after(block.size());
  std::set<std::string> live;  // nothing is live after the return
  for (std::size_t i = block.size(); i-- > 0;) {
    live_after[i] = live;
    live.erase(block[i].def);
    for (auto& u : block[i].uses) live.insert(u);
  }

  Edges precise;
  std::size_t max_live = 0;
  for (std::size_t i = 0; i < block.size(); ++i) {
    std::print("{}  live after:", i + 1);
    for (auto& v : live_after[i]) std::print(" {}", v);
    std::println("");
    max_live = std::max(max_live, live_after[i].size());
    if (!block[i].def.empty())
      for (auto& v : live_after[i]) add_edge(precise, block[i].def, v);
  }
  std::println("most values live at once: {}", max_live);
  print_edges("edges from definitions", precise);

  // Interval view: [definition position, last use position], closed.
  std::vector<std::pair<std::string, std::pair<std::size_t, std::size_t>>> intervals;
  for (std::size_t i = 0; i < block.size(); ++i) {
    if (block[i].def.empty()) continue;
    std::size_t end = i + 1;
    for (std::size_t j = i + 1; j < block.size(); ++j)
      for (auto& u : block[j].uses)
        if (u == block[i].def) end = j + 1;
    intervals.push_back({block[i].def, {i + 1, end}});
  }
  Edges overlap;
  for (auto& [x, rx] : intervals)
    for (auto& [y, ry] : intervals)
      if (rx.first <= ry.second && ry.first <= rx.second) add_edge(overlap, x, y);
  print_edges("edges from interval overlap", overlap);
}

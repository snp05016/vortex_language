// Renaming, the second half of Cytron's SSA construction: walk the dominator
// tree and keep one stack of versions per variable.
//
// The function is the stage 7 loop from O2, whose phis are already placed
// (O2's frontiers.cpp found them: count at B and F, total at B). A definition
// pushes a new version; a use reads the top of its variable's stack; leaving a
// block fills in, for each CFG successor, the phi operands that belong to this
// edge; and the versions a block pushed are popped when the walk leaves the
// block's subtree. So the top of a stack is always the nearest definition that
// dominates the current point.
//
// Follows: Cytron et al., TOPLAS 13(4), 1991, section 5.2 and figure 12.

#include <algorithm>
#include <array>
#include <map>
#include <print>
#include <sstream>
#include <string>
#include <vector>

constexpr int N = 6;
constexpr char name[N] = {'A', 'B', 'C', 'D', 'E', 'F'};
const std::array<std::vector<int>, N> succ = {{{1}, {2, 5}, {1, 3}, {5, 4}, {1}, {}}};
const std::array<std::vector<int>, N> children = {{{1}, {2, 5}, {3}, {4}, {}, {}}};
std::array<std::vector<std::string>, N> code = {{{"count = 0", "total = 0"},
                                                 {"if count < 10"},
                                                 {"count = count + 1", "if count % 2 == 0"},
                                                 {"if total > 10"},
                                                 {"total = total + count"},
                                                 {"print total"}}};

struct Phi {
  std::string var, target;
  std::vector<std::string> args;  // one per predecessor, in pred order
};
std::array<std::vector<Phi>, N> phis;
std::array<std::vector<int>, N> pred;
std::map<std::string, std::vector<int>> stacks = {{"count", {}}, {"total", {}}};
std::map<std::string, int> counter;

std::string top(const std::string &v) { return v + std::to_string(stacks[v].back()); }

std::string define(const std::string &v, std::vector<std::string> &pushed) {
  stacks[v].push_back(counter[v]++);
  pushed.push_back(v);
  return top(v);
}

// Uses are renamed before the target, so "count = count + 1" reads the old version.
std::string rename(const std::string &statement, std::vector<std::string> &pushed) {
  std::istringstream in(statement);
  std::vector<std::string> words;
  for (std::string w; in >> w;) words.push_back(w);
  const bool assigns = words.size() > 2 && words[1] == "=";
  for (std::size_t i = assigns ? 2 : 0; i < words.size(); ++i)
    if (stacks.contains(words[i])) words[i] = top(words[i]);
  if (assigns) words[0] = define(words[0], pushed);
  std::string out;
  for (const std::string &w : words) out += (out.empty() ? "" : " ") + w;
  return out;
}

void search(int b) {
  std::vector<std::string> pushed;
  for (Phi &p : phis[b]) p.target = define(p.var, pushed);
  for (std::string &s : code[b]) s = rename(s, pushed);
  std::print("visit {}:", name[b]);
  for (const auto &[v, versions] : stacks) std::print("  {} {}", v, versions);
  std::println("");
  for (int s : succ[b]) {
    const auto j = std::ranges::find(pred[s], b) - pred[s].begin();
    for (Phi &p : phis[s]) p.args[j] = top(p.var);
  }
  for (int c : children[b]) search(c);
  for (const std::string &v : pushed) stacks[v].pop_back();
}

int main() {
  for (int b = 0; b < N; ++b)
    for (int s : succ[b]) pred[s].push_back(b);
  phis[1] = {{"count", "", {}}, {"total", "", {}}};
  phis[5] = {{"count", "", {}}};
  for (int b = 0; b < N; ++b)
    for (Phi &p : phis[b]) p.args.resize(pred[b].size());

  search(0);
  for (int b = 0; b < N; ++b) {
    std::println("{}:", name[b]);
    for (const Phi &p : phis[b]) {
      std::print("    {} = phi(", p.target);
      for (std::size_t j = 0; j < p.args.size(); ++j)
        std::print("{}{}: {}", j ? ", " : "", name[pred[b][j]], p.args[j]);
      std::println(")");
    }
    for (const std::string &s : code[b]) std::println("    {}", s);
  }
}

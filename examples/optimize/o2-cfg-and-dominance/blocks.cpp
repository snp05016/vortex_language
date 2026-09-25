// Splitting a list of instructions into basic blocks, then finding the edges.
//
// A block may be entered only at its top and left only at its bottom, so a new
// block must start at every "leader": the first instruction, every instruction
// a jump can land on, and every instruction just after a jump, a branch or a
// return. Each block runs from one leader up to the next.
//
// The toy function adds up the numbers below n that are not multiples of 3.
// Its last two instructions follow a return and carry no label, so nothing can
// reach them: the program reports that block as unreachable.
//
// Follows: LLVM Language Reference, "Functions" (a basic block ends with a
// terminator), and Cytron et al., TOPLAS 1991, section 2 (control flow graphs).

#include <cstddef>
#include <map>
#include <print>
#include <set>
#include <string_view>
#include <vector>

enum class Op { Plain, Jump, Branch, Return };  // Branch: jump if true, else fall through
struct Instr { std::string_view label, text; Op op = Op::Plain; std::string_view target = ""; };

const std::vector<Instr> code = {
    {"", "i = 0"},
    {"", "s = 0"},
    {"top", "if i >= n goto done", Op::Branch, "done"},
    {"", "t = i % 3"},
    {"", "if t == 0 goto next", Op::Branch, "next"},
    {"", "s = s + i"},
    {"next", "i = i + 1"},
    {"", "goto top", Op::Jump, "top"},
    {"done", "return s", Op::Return},
    {"", "s = s * 2"},
    {"", "return s", Op::Return},
};

int main() {
  std::map<std::string_view, std::size_t> at;  // label -> instruction index
  for (std::size_t i = 0; i < code.size(); ++i)
    if (!code[i].label.empty()) at[code[i].label] = i;

  std::set<std::size_t> leaders = {0};
  for (std::size_t i = 0; i < code.size(); ++i) {
    if (code[i].op == Op::Plain) continue;
    if (!code[i].target.empty()) leaders.insert(at[code[i].target]);
    if (i + 1 < code.size()) leaders.insert(i + 1);
  }

  std::vector<std::size_t> first(leaders.begin(), leaders.end()), block_of(code.size());
  for (std::size_t b = 0; b < first.size(); ++b)
    for (std::size_t i = first[b]; i < code.size() && (b + 1 == first.size() || i < first[b + 1]); ++i)
      block_of[i] = b;

  // A block's successors depend only on its last instruction.
  std::vector<std::set<std::size_t>> succ(first.size());
  for (std::size_t b = 0; b < first.size(); ++b) {
    const std::size_t last = b + 1 < first.size() ? first[b + 1] - 1 : code.size() - 1;
    const Instr &end = code[last];
    if (end.op == Op::Jump || end.op == Op::Branch) succ[b].insert(block_of[at[end.target]]);
    if ((end.op == Op::Plain || end.op == Op::Branch) && last + 1 < code.size())
      succ[b].insert(block_of[last + 1]);  // falls through to the next block
    std::print("B{}  instructions {}-{}  successors:", b, first[b], last);
    for (std::size_t s : succ[b]) std::print(" B{}", s);
    std::println("");
  }

  // Everything reachable from the entry block; the rest can never run.
  std::vector<bool> seen(first.size());
  std::vector<std::size_t> stack = {0};
  while (!stack.empty()) {
    const std::size_t b = stack.back();
    stack.pop_back();
    if (seen[b]) continue;
    seen[b] = true;
    for (std::size_t s : succ[b]) stack.push_back(s);
  }
  std::print("unreachable:");
  for (std::size_t b = 0; b < first.size(); ++b)
    if (!seen[b]) std::print(" B{}", b);
  std::println("");
}

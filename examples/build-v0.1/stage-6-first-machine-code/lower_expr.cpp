// Lowering: turning a tree into a list of steps whose order is explicit.
//
// A syntax tree implies the order of work only through its shape. Lowering
// writes that order out as a flat list of small instructions, one per
// operator. This program builds the tree for a calculator expression,
// -(3 + 4) * 2 - 1, by hand (there is no parser here) and lowers it by
// visiting children before their parent, left child before right.
//
// Follows: cppreference on std::vector, which holds the tree as a flat array
// of nodes addressed by index, and on std::format, which names temporaries.
// https://en.cppreference.com/cpp/container/vector
// https://en.cppreference.com/cpp/utility/format/format

#include <cstddef>
#include <format>
#include <print>
#include <string>
#include <vector>

namespace {

// A node is a literal (op 0), a negation ('~', only lhs used) or a binary
// operator ('+', '-', '*'). Children are indices into the node array.
struct Node { char op; int value; int lhs; int rhs; };

const char* mnemonic(char op) {
  switch (op) {
    case '+': return "add";
    case '-': return "sub";
    case '*': return "mul";
    default: return "neg";
  }
}

// Appends the instructions that compute node `index` to `out` and returns
// where its value lives: the literal itself, or the temporary that holds it.
// The temporary is numbered only after the children are lowered, so every
// instruction uses results that earlier instructions already produced.
std::string lower(const std::vector<Node>& nodes, int index, std::vector<std::string>& out) {
  const Node& n = nodes[static_cast<std::size_t>(index)];
  if (n.op == 0) return std::to_string(n.value);
  const std::string a = lower(nodes, n.lhs, out);
  const std::string b = n.op == '~' ? "" : ", " + lower(nodes, n.rhs, out);
  const std::string dst = std::format("t{}", out.size());
  out.push_back(std::format("{} = {} {}{}", dst, mnemonic(n.op), a, b));
  return dst;
}

}  // namespace

int main() {
  const std::vector<Node> nodes = {
      {0, 3, 0, 0},    // 0: 3
      {0, 4, 0, 0},    // 1: 4
      {'+', 0, 0, 1},  // 2: 3 + 4
      {'~', 0, 2, 0},  // 3: -(3 + 4)
      {0, 2, 0, 0},    // 4: 2
      {'*', 0, 3, 4},  // 5: -(3 + 4) * 2
      {0, 1, 0, 0},    // 6: 1
      {'-', 0, 5, 6},  // 7: -(3 + 4) * 2 - 1, the root
  };

  std::vector<std::string> program;
  const std::string result = lower(nodes, 7, program);

  std::println("source: -(3 + 4) * 2 - 1");
  for (const std::string& line : program) std::println("  {}", line);
  std::println("result: {}", result);
}

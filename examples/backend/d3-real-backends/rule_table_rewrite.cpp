// A table of rewrite rules applied to a tree until none of them match, in
// the style of Go's compiler: several of its SSA passes, "lower" among them,
// are generated from rule files, and its driver repeats a pass's rewrites
// until one sweep changes nothing. Nothing here builds Go's rule compiler;
// it is a small rule table and a driver for a toy expression tree.
//
// Different problem from the Vortex exercise: `add` and `mul` over a toy
// expression tree, not Vortex's own constant folding.
#include <functional>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace {

struct Node {
  std::string op;         // "const", "var", "add", "mul"
  long imm = 0;             // for "const"
  std::string name;       // for "var"
  std::vector<Node> kids; // exactly two, for "add" and "mul"
};

Node make_const(long v) { return Node{"const", v, "", {}}; }
Node make_var(std::string n) { return Node{"var", 0, std::move(n), {}}; }
Node make_op(std::string op, Node a, Node b) {
  return Node{std::move(op), 0, "", {std::move(a), std::move(b)}};
}

std::string print(const Node &n) {
  if (n.op == "const") return std::to_string(n.imm);
  if (n.op == "var") return n.name;
  return "(" + n.op + " " + print(n.kids[0]) + " " + print(n.kids[1]) + ")";
}

bool is_const(const Node &n, long v) { return n.op == "const" && n.imm == v; }

// The rule table. Each rule looks only at a node's immediate shape (its op
// and its already-rewritten children) and either rewrites it or declines.
// Order matters only in that the first matching rule wins.
using Rule = std::function<std::optional<Node>(const Node &)>;
const std::vector<Rule> kRules = {
    // add(x, 0) -> x
    [](const Node &n) -> std::optional<Node> {
      if (n.op == "add" && is_const(n.kids[1], 0)) return n.kids[0];
      return std::nullopt;
    },
    // mul(x, 1) -> x
    [](const Node &n) -> std::optional<Node> {
      if (n.op == "mul" && is_const(n.kids[1], 1)) return n.kids[0];
      return std::nullopt;
    },
    // mul(x, 0) -> 0
    [](const Node &n) -> std::optional<Node> {
      if (n.op == "mul" && is_const(n.kids[1], 0)) return make_const(0);
      return std::nullopt;
    },
    // add(const, const) -> const
    [](const Node &n) -> std::optional<Node> {
      if (n.op == "add" && n.kids[0].op == "const" && n.kids[1].op == "const")
        return make_const(n.kids[0].imm + n.kids[1].imm);
      return std::nullopt;
    },
    // mul(const, const) -> const
    [](const Node &n) -> std::optional<Node> {
      if (n.op == "mul" && n.kids[0].op == "const" && n.kids[1].op == "const")
        return make_const(n.kids[0].imm * n.kids[1].imm);
      return std::nullopt;
    },
    // op(const, x) -> op(x, const): puts a lone constant on the right, where
    // the identity rules above look for it. It builds a new node, and the
    // sweep does not look at a node it has just built.
    [](const Node &n) -> std::optional<Node> {
      if (n.kids.size() == 2 && n.kids[0].op == "const" &&
          n.kids[1].op != "const")
        return make_op(n.op, n.kids[1], n.kids[0]);
      return std::nullopt;
    },
};

// One bottom-up sweep: rewrite both children first, then try the table
// once against this node. A rewritten node is returned as is, not
// re-examined in the same sweep; if it can reduce further, the next sweep
// will catch it. Leaves (`const`, `var`) never match any rule.
Node sweep(Node n, bool &changed) {
  if (n.op == "add" || n.op == "mul") {
    n.kids[0] = sweep(std::move(n.kids[0]), changed);
    n.kids[1] = sweep(std::move(n.kids[1]), changed);
    for (const auto &rule : kRules) {
      if (auto replacement = rule(n)) {
        changed = true;
        return *replacement;
      }
    }
  }
  return n;
}

void run(const std::string &title, Node expr) {
  std::cout << title << ":\n";
  std::cout << "  pass 0: " << print(expr) << "\n";
  int pass = 0;
  while (true) {
    bool changed = false;
    expr = sweep(std::move(expr), changed);
    pass++;
    std::cout << "  pass " << pass << ": " << print(expr) << "\n";
    if (!changed) break;
  }
  std::cout << "  fixpoint after " << pass << " passes\n";
}

} // namespace

int main() {
  // (x * 1) + (2 * 3): one sweep turns "x * 1" into "x" and folds "2 * 3"
  // into 6; the confirming sweep finds nothing left to do.
  run("mul-by-one beside a constant product",
      make_op("add", make_op("mul", make_var("x"), make_const(1)),
              make_op("mul", make_const(2), make_const(3))));

  // 0 + (1 * y): the first sweep only moves the constants to the right; the
  // identities they now expose fire in the second sweep, and the third
  // confirms there is nothing left.
  run("constants on the left",
      make_op("add", make_const(0),
              make_op("mul", make_const(1), make_var("y"))));
  return 0;
}

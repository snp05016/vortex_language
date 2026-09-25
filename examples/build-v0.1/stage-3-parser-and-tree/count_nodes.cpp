// Parses "2 + 3 * 4" two ways with the same recursive-descent grammar: once
// keeping a node for every rule used and every token read (a parse tree),
// once keeping a node only for each number and each operator applied (an
// abstract syntax tree). Counts how many nodes each one needs.
//
// Follows: Robert Nystrom, Crafting Interpreters, "Representing Code" (parse
// trees keep every rule, syntax trees elide the ones later phases do not
// need) and "Parsing Expressions" (one rule per precedence level).

#include <memory>
#include <print>
#include <string>
#include <vector>

struct Token {
  enum Kind { Num, Op, End } kind;
  char op = 0;
  int value = 0;
};

struct Node {
  std::string label;  // a rule name, an operator, or a number's spelling
  std::vector<std::unique_ptr<Node>> children;
};

std::unique_ptr<Node> leaf(std::string label) {
  auto node = std::make_unique<Node>();
  node->label = std::move(label);
  return node;
}

// keep_every_rule selects a parse tree (true) or an abstract syntax tree
// (false). The grammar and the order of calls are identical; only the nodes
// each rule leaves behind differ.
class Parser {
 public:
  Parser(std::vector<Token> tokens, bool keep_every_rule)
      : tokens_(std::move(tokens)), keep_every_rule_(keep_every_rule) {}

  std::unique_ptr<Node> parse_additive() {
    return parse_level(
        "additive", [this] { return parse_multiplicative(); }, '+', '-');
  }

 private:
  std::unique_ptr<Node> parse_multiplicative() {
    return parse_level(
        "multiplicative", [this] { return parse_primary(); }, '*', '/');
  }

  // One precedence level: operand { op operand }.
  template <class Next>
  std::unique_ptr<Node> parse_level(const char* rule_name, Next next,
                                     char op_a, char op_b) {
    auto rule = leaf(rule_name);  // used only for the parse tree
    auto left = next();
    if (keep_every_rule_) rule->children.push_back(std::move(left));
    while (tokens_[pos_].kind == Token::Op &&
           (tokens_[pos_].op == op_a || tokens_[pos_].op == op_b)) {
      auto op = leaf(std::string(1, tokens_[pos_].op));
      ++pos_;
      auto right = next();
      if (keep_every_rule_) {
        // The parse tree keeps the operator token as a leaf beside its
        // operands, in the order they were read.
        rule->children.push_back(std::move(op));
        rule->children.push_back(std::move(right));
      } else {
        // The syntax tree turns the operator into the node that joins them.
        op->children.push_back(std::move(left));
        op->children.push_back(std::move(right));
        left = std::move(op);
      }
    }
    return keep_every_rule_ ? std::move(rule) : std::move(left);
  }

  std::unique_ptr<Node> parse_primary() {
    auto number = leaf(std::to_string(tokens_[pos_].value));
    ++pos_;
    if (!keep_every_rule_) return number;
    auto wrapper = leaf("primary");
    wrapper->children.push_back(std::move(number));
    return wrapper;
  }

  std::vector<Token> tokens_;
  std::size_t pos_ = 0;
  bool keep_every_rule_;
};

int count_nodes(const Node& node) {
  int total = 1;
  for (const auto& child : node.children) total += count_nodes(*child);
  return total;
}

std::string to_sexpr(const Node& node) {
  if (node.children.empty()) return node.label;
  std::string text = "(" + node.label;
  for (const auto& child : node.children) text += " " + to_sexpr(*child);
  return text + ")";
}

int main() {
  const std::vector<Token> tokens = {
      {Token::Num, 0, 2}, {Token::Op, '+', 0}, {Token::Num, 0, 3},
      {Token::Op, '*', 0}, {Token::Num, 0, 4}, {Token::End, 0, 0}};

  Parser parse_tree_parser(tokens, /*keep_every_rule=*/true);
  const auto parse_tree = parse_tree_parser.parse_additive();
  std::println("parse tree ({} nodes): {}", count_nodes(*parse_tree),
               to_sexpr(*parse_tree));

  Parser ast_parser(tokens, /*keep_every_rule=*/false);
  const auto ast = ast_parser.parse_additive();
  std::println("syntax tree ({} nodes): {}", count_nodes(*ast),
               to_sexpr(*ast));
}

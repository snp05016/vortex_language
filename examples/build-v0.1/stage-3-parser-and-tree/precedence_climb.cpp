// A precedence-climbing parser for a tiny four-operator calculator language.
// One loop, guided by each operator's binding power, replaces having one
// grammar rule per precedence level.
//
// Follows: LLVM Project, "My First Language Frontend with LLVM Tutorial",
// chapter 2 (operator-precedence parsing), and Aleksey Kladov, "Simple but
// Powerful Pratt Parsing" (binding power and associativity).

#include <memory>
#include <print>
#include <string>
#include <vector>

struct Token {
  enum Kind { Num, Op, End } kind;
  char op = 0;    // '+', '-', '*' or '/' when kind == Op
  int value = 0;  // when kind == Num
};

struct Node {
  char op = 0;  // 0 marks a leaf
  int value = 0;
  std::unique_ptr<Node> lhs, rhs;
};

// Higher means "grouped first". '+' and '-' share a power, as do '*' and
// '/', so each pair ties; the loop in parse_expression breaks ties.
int binding_power(char op) {
  switch (op) {
    case '+':
    case '-':
      return 1;
    case '*':
    case '/':
      return 2;
    default:
      return -1;
  }
}

class Parser {
 public:
  explicit Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

  std::unique_ptr<Node> parse_expression(int min_power) {
    auto left = parse_leaf();
    while (tokens_[pos_].kind == Token::Op) {
      const char op = tokens_[pos_].op;
      const int power = binding_power(op);
      if (power < min_power) break;
      ++pos_;
      // Requiring "power + 1", not "power", on the recursive call is what
      // makes the operator left-associative: a second copy of the same
      // operator does not meet that higher minimum, so this loop picks it
      // up instead of another level of recursion.
      auto right = parse_expression(power + 1);
      auto node = std::make_unique<Node>();
      node->op = op;
      node->lhs = std::move(left);
      node->rhs = std::move(right);
      left = std::move(node);
    }
    return left;
  }

 private:
  std::unique_ptr<Node> parse_leaf() {
    auto node = std::make_unique<Node>();
    node->value = tokens_[pos_].value;
    ++pos_;
    return node;
  }

  std::vector<Token> tokens_;
  std::size_t pos_ = 0;
};

std::string to_sexpr(const Node& node) {
  if (node.op == 0) return std::to_string(node.value);
  return std::string("(") + node.op + " " + to_sexpr(*node.lhs) + " " +
         to_sexpr(*node.rhs) + ")";
}

int evaluate(const Node& node) {
  if (node.op == 0) return node.value;
  const int l = evaluate(*node.lhs);
  const int r = evaluate(*node.rhs);
  switch (node.op) {
    case '+':
      return l + r;
    case '-':
      return l - r;
    case '*':
      return l * r;
    default:
      return l / r;
  }
}

int main() {
  const std::vector<Token> sum_then_product = {
      {Token::Num, 0, 2}, {Token::Op, '+', 0}, {Token::Num, 0, 3},
      {Token::Op, '*', 0}, {Token::Num, 0, 4}, {Token::End, 0, 0}};
  const std::vector<Token> two_subtractions = {
      {Token::Num, 0, 10}, {Token::Op, '-', 0}, {Token::Num, 0, 4},
      {Token::Op, '-', 0}, {Token::Num, 0, 3}, {Token::End, 0, 0}};

  for (const auto& tokens : {sum_then_product, two_subtractions}) {
    Parser parser(tokens);
    const auto tree = parser.parse_expression(0);
    std::println("{} = {}", to_sexpr(*tree), evaluate(*tree));
  }
}

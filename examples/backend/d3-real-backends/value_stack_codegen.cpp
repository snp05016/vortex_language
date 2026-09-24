// One-pass code generation with a value stack, in the style TCC's developer
// guide describes: no AST is built. A recursive-descent parser emits
// instructions as it goes, and a small stack of "where is this value"
// descriptors (a constant, or a named temporary) tracks results well enough
// to fold constant subexpressions on the fly.
//
// Different problem from the Vortex exercise: a four-function integer
// calculator, not a compiler back end for a fixed-shape array language.
#include <cctype>
#include <iostream>
#include <sstream>
#include <string>
#include <variant>
#include <vector>

namespace {

// A value is either a compile-time constant or the name of a temporary that
// holds a run-time result. This is the "value stack": at every point in
// parsing, it says where each live value already lives.
struct Value {
  bool is_const;
  long value;       // meaningful when is_const
  std::string name; // meaningful when !is_const
};

class Generator {
public:
  explicit Generator(std::string text) : text_(std::move(text)) {}

  // Parses and emits in one pass; returns the final value descriptor.
  Value run() {
    Value v = parse_sum();
    if (pos_ != text_.size()) throw std::runtime_error("trailing input");
    return v;
  }

  const std::vector<std::string> &code() const { return code_; }

private:
  Value parse_sum() {
    Value left = parse_product();
    while (true) {
      skip_space();
      if (peek() == '+' || peek() == '-') {
        char op = text_[pos_++];
        Value right = parse_product();
        left = emit_binary(op, left, right);
      } else {
        break;
      }
    }
    return left;
  }

  Value parse_product() {
    Value left = parse_atom();
    while (true) {
      skip_space();
      if (peek() == '*' || peek() == '/') {
        char op = text_[pos_++];
        Value right = parse_atom();
        left = emit_binary(op, left, right);
      } else {
        break;
      }
    }
    return left;
  }

  Value parse_atom() {
    skip_space();
    if (peek() == '(') {
      pos_++;
      Value v = parse_sum();
      skip_space();
      if (peek() != ')') throw std::runtime_error("expected )");
      pos_++;
      return v;
    }
    if (std::isdigit(static_cast<unsigned char>(peek()))) {
      size_t start = pos_;
      while (pos_ < text_.size() &&
             std::isdigit(static_cast<unsigned char>(text_[pos_])))
        pos_++;
      return Value{true, std::stol(text_.substr(start, pos_ - start)), ""};
    }
    if (std::isalpha(static_cast<unsigned char>(peek()))) {
      // A single-letter name stands for a run-time input: its value is not
      // known while generating code, so it can never be folded away.
      std::string name(1, text_[pos_++]);
      return Value{false, 0, name};
    }
    throw std::runtime_error("expected a digit, a letter, or (");
  }

  // Emits an instruction for one binary operator, or none at all when both
  // operands are constants: the value stack folds the constant instead of
  // generating code for it, exactly as TCC's single-pass generator does.
  Value emit_binary(char op, const Value &a, const Value &b) {
    if (a.is_const && b.is_const) {
      long r = fold(op, a.value, b.value);
      return Value{true, r, ""};
    }
    std::string dst = "t" + std::to_string(next_temp_++);
    code_.push_back(dst + " = " + operand(a) + " " + std::string(1, op) +
                     " " + operand(b));
    return Value{false, 0, dst};
  }

  static long fold(char op, long a, long b) {
    switch (op) {
      case '+': return a + b;
      case '-': return a - b;
      case '*': return a * b;
      case '/': return a / b;
    }
    throw std::runtime_error("bad operator");
  }

  static std::string operand(const Value &v) {
    return v.is_const ? std::to_string(v.value) : v.name;
  }

  char peek() const { return pos_ < text_.size() ? text_[pos_] : '\0'; }
  void skip_space() {
    while (pos_ < text_.size() && text_[pos_] == ' ') pos_++;
  }

  std::string text_;
  size_t pos_ = 0;
  int next_temp_ = 0;
  std::vector<std::string> code_;
};

void run_example(const std::string &expr) {
  Generator gen(expr);
  Value result = gen.run();
  std::cout << "expr: " << expr << "\n";
  for (const auto &line : gen.code()) std::cout << "  " << line << "\n";
  std::cout << "  result: " << (result.is_const ? std::to_string(result.value)
                                                 : result.name)
             << "\n";
}

} // namespace

int main() {
  // Wholly constant: folded away, no instructions at all.
  run_example("2 + 3 * 4");
  // Mixed: "4 - 1" folds to 3 on the spot; "x * 3" cannot, so it is the
  // only instruction emitted, and the final "+" combines it with the
  // already-folded constant.
  run_example("x * 3 + (4 - 1)");
  return 0;
}

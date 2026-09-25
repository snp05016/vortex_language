// A tiny calculator tokenizer that applies the longest-match rule (also
// called maximal munch): when two operator spellings could both start at the
// same character, the lexer keeps the longer one. `<=` must be tried before
// `<`, or the lexer would stop one character too early and leave a stray `=`.
//
// Follows the shape of the token loop in the LLVM Kaleidoscope tutorial
// (skip spaces, then decide from the current character what token starts
// there). Kaleidoscope returns every operator as a single character; the
// two-character check is what this example adds. The character tests follow
// cppreference.

#include <cctype>
#include <cstddef>
#include <print>
#include <string_view>
#include <vector>

enum class Kind { Number, Op, Paren };

struct Token {
  Kind kind;
  std::string_view spelling;
};

int main() {
  constexpr std::string_view text = "(3 <= 4) != (5 < 2)";
  constexpr std::string_view two_char_ops[] = {"<=", ">=", "==", "!="};

  std::vector<Token> tokens;
  std::size_t i = 0;
  while (i < text.size()) {
    const auto byte = static_cast<unsigned char>(text[i]);
    if (std::isspace(byte)) {
      ++i;
      continue;
    }
    if (std::isdigit(byte)) {
      const std::size_t start = i;
      while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) {
        ++i;
      }
      tokens.push_back({Kind::Number, text.substr(start, i - start)});
      continue;
    }
    if (text[i] == '(' || text[i] == ')') {
      tokens.push_back({Kind::Paren, text.substr(i, 1)});
      ++i;
      continue;
    }
    bool matched_two = false;
    for (const std::string_view op : two_char_ops) {
      if (text.substr(i, 2) == op) {
        tokens.push_back({Kind::Op, op});
        i += 2;
        matched_two = true;
        break;
      }
    }
    if (!matched_two) {
      tokens.push_back({Kind::Op, text.substr(i, 1)});
      ++i;
    }
  }

  for (const Token& tok : tokens) {
    const char* kind = tok.kind == Kind::Number  ? "number"
                        : tok.kind == Kind::Op ? "op"
                                                : "paren";
    std::println("{:<7} '{}'", kind, tok.spelling);
  }
}

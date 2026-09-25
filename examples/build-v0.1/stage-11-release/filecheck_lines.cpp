// A tiny relative of LLVM's FileCheck: confirm that a set of substrings
// appear in a text, in order, one per line. Other lines may sit between the
// matches. Real FileCheck also matches within a line and has CHECK-NEXT,
// which requires the match on the very next line; this keeps only plain,
// in-order matching.
//
// Follows: LLVM FileCheck documentation.

#include <print>
#include <string>
#include <string_view>
#include <vector>

// Returns the number of patterns matched, in order, before the first one
// that never appears. Equal to patterns.size() when every pattern is found.
std::size_t match_in_order(const std::vector<std::string>& lines,
                            const std::vector<std::string>& patterns) {
  std::size_t pattern = 0;
  for (const auto& line : lines) {
    if (pattern == patterns.size()) {
      break;
    }
    if (line.find(patterns[pattern]) != std::string::npos) {
      ++pattern;
    }
  }
  return pattern;
}

void report(std::string_view name, const std::vector<std::string>& lines,
            const std::vector<std::string>& patterns) {
  const auto matched = match_in_order(lines, patterns);
  if (matched == patterns.size()) {
    std::println("{}: all {} patterns matched, in order", name,
                  patterns.size());
  } else {
    std::println("{}: pattern {} (\"{}\") never appeared in order", name,
                  matched, patterns[matched]);
  }
}

int main() {
  // Two diagnostics from a toy calculator. The patterns name only the
  // position and the category, never the wording of the message.
  const std::vector<std::string> good{
      "calc.txt:2:7: syntax error: expected ')'",
      "calc.txt:4:3: divide error: divisor is zero",
  };
  const std::vector<std::string> wrong_order{
      "calc.txt:4:3: divide error: divisor is zero",
      "calc.txt:2:7: syntax error: expected ')'",
  };
  const std::vector<std::string> patterns{"2:7: syntax error",
                                           "4:3: divide error"};

  report("good", good, patterns);
  report("wrong_order", wrong_order, patterns);
}

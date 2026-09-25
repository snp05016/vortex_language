// A test runner in miniature. Each case is an input and the answer the code
// under test should give; the runner compares them, prints a verdict per case
// naming what was expected and what happened, then a summary and the exit
// status the whole run should end with. The code under test is a sign
// classifier, not a compiler: only the reporting shape is the point.
//
// The last case is wrong on purpose. Seeing it reported as FAIL, and seeing
// the status become 1, is what shows the runner checks anything at all.
//
// A real runner returns that status from main. This one prints it and returns
// 0, because the examples harness treats any other status as a broken example.

#include <print>
#include <string_view>
#include <vector>

std::string_view classify(int n) {
  if (n < 0) return "negative";
  if (n == 0) return "zero";
  return "positive";
}

struct Case {
  std::string_view name;
  int input;
  std::string_view expected;
};

int main() {
  const std::vector<Case> cases = {
      {"below_zero", -3, "negative"},
      {"at_zero", 0, "zero"},
      {"above_zero", 7, "positive"},
      {"deliberate_failure", 2, "negative"},  // wrong on purpose
  };

  int passed = 0;
  int failed = 0;
  for (const Case& c : cases) {
    const std::string_view actual = classify(c.input);
    const bool ok = actual == c.expected;
    std::println("{}: {} (expected {}, got {})", c.name, ok ? "PASS" : "FAIL",
                 c.expected, actual);
    if (ok) {
      ++passed;
    } else {
      ++failed;
    }
  }
  std::println("{} passed, {} failed", passed, failed);
  std::println("exit status: {}", failed == 0 ? 0 : 1);
}

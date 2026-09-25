// A miniature test result classifier, after LLVM lit's outcomes: an
// ordinary pass or fail, an expected failure (xfail), and an unexpected pass
// (xpass): a test marked xfail that now succeeds, which usually means a known
// limitation has quietly gone away. Like lit, it counts xpass as a failure.
//
// Follows: LLVM lit documentation.

#include <print>
#include <string>
#include <string_view>
#include <vector>

enum class Outcome { pass, fail, xfail, xpass };

struct Test {
  std::string name;
  bool actually_passed;
  bool marked_xfail;
};

Outcome classify(const Test& test) {
  if (test.marked_xfail) {
    return test.actually_passed ? Outcome::xpass : Outcome::xfail;
  }
  return test.actually_passed ? Outcome::pass : Outcome::fail;
}

std::string_view name(Outcome outcome) {
  switch (outcome) {
    case Outcome::pass:
      return "PASS";
    case Outcome::fail:
      return "FAIL";
    case Outcome::xfail:
      return "XFAIL";
    case Outcome::xpass:
      return "XPASS";
  }
  return "?";
}

int main() {
  const std::vector<Test> tests{
      {"bounds_check", true, false},
      {"eight_dimensions", false, true},  // known limitation, still fails
      {"tail_calls", true, true},         // was xfail; now quietly fixed
  };

  int surprises = 0;
  for (const auto& test : tests) {
    const auto outcome = classify(test);
    std::println("{}: {}", test.name, name(outcome));
    surprises += outcome == Outcome::fail || outcome == Outcome::xpass;
  }
  std::println("{} result(s) need attention", surprises);
}

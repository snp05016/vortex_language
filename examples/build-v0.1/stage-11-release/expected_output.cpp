// A minimal test runner: run a table of small cases, compare each one's
// output against an expected string, and report a pass/fail summary. Real
// test runners add categories and source spans; this shows the comparison
// that sits at the core of all of them.
//
// Follows: cppreference std::function and std::ostringstream.

#include <functional>
#include <print>
#include <sstream>
#include <string>
#include <vector>

struct Case {
  std::string name;
  std::function<void(std::ostream&)> run;
  std::string expected;
};

int main() {
  const std::vector<Case> cases{
      {"sum", [](std::ostream& out) { out << 2 + 3; }, "5"},
      {"product", [](std::ostream& out) { out << 2 * 3; }, "6"},
      {"wrong", [](std::ostream& out) { out << 2 + 2; }, "5"},
  };

  int passed = 0;
  for (const auto& test : cases) {
    std::ostringstream actual;
    test.run(actual);
    const bool ok = actual.str() == test.expected;
    std::println("{}: {} (expected {}, got {})", test.name,
                  ok ? "pass" : "fail", test.expected, actual.str());
    passed += ok ? 1 : 0;
  }
  std::println("{}/{} passed", passed, cases.size());
}

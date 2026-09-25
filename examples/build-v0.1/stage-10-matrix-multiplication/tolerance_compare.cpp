// An "almost equal" check that combines an absolute and a relative
// tolerance, compared with `==` on the same pairs. The absolute part handles
// values near zero, where any relative distance shrinks to nothing; the
// relative part scales the allowed distance with the size of the values.
//
// Follows: cppreference std::abs (floating-point) and std::max.

#include <algorithm>
#include <cmath>
#include <print>

bool almost_equal(double a, double b, double relative_tol,
                  double absolute_tol) {
  const double allowed =
      std::max(absolute_tol, relative_tol * std::max(std::abs(a), std::abs(b)));
  return std::abs(a - b) <= allowed;
}

int main() {
  struct Pair {
    const char* what;
    double computed;
    double expected;
  };
  const Pair pairs[] = {
      // Whole numbers: every step is exact, so `==` is safe.
      {"exact inputs", 2.0 * 3.0 + 4.0, 10.0},
      // The same sum grouped two ways: each grouping rounds differently.
      {"regrouped sum", (0.1 + 0.2) + 0.3, 0.1 + (0.2 + 0.3)},
      // Near zero only the absolute tolerance can accept the pair.
      {"near zero", 0.0, 1e-9},
      // A real disagreement must still be rejected.
      {"wrong answer", 1.0, 1.0001},
  };

  const double relative_tol = 1e-6;
  const double absolute_tol = 1e-8;

  for (const auto& pair : pairs) {
    std::println("{}: {} vs {}, == {}, almost_equal {}", pair.what,
                 pair.computed, pair.expected,
                 pair.computed == pair.expected,
                 almost_equal(pair.computed, pair.expected, relative_tol,
                              absolute_tol));
  }
}

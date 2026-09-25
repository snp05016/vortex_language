// Follows: Goff, Kennedy and Tseng, "Practical Dependence Testing", PLDI 1991,
// section 4.2.1 (the strong SIV test), and Bacon, Graham and Sharp, ACM
// Computing Surveys 26(4), 1994, section 5.4 (the GCD test).
#include <cstdio>
#include <cstdlib>
#include <numeric>

// One loop, 0 <= i < n. A write to x[a1*i + c1] and a read of x[a2*i + c2]
// touch the same element when a1*i1 + c1 == a2*i2 + c2 for some i1, i2 in
// range. Three ways to decide it, from cheapest to most expensive.
struct Pair { const char* name; int a1, c1, a2, c2, n; };

// Necessary condition: an integer solution exists at all, bounds ignored.
const char* gcd_test(const Pair& p) {
  int g = std::gcd(p.a1, p.a2);
  int rhs = p.c2 - p.c1;
  bool maybe = g == 0 ? rhs == 0 : rhs % g == 0;
  return maybe ? "maybe" : "none";
}

// Exact when both coefficients are the same constant a: the distance is
// d = (c1 - c2) / a, and a dependence exists iff d is an integer that fits
// inside the loop, |d| <= n - 1. d is the read's iteration minus the write's:
// positive, the read comes later (flow); negative, earlier (anti).
const char* strong_siv(const Pair& p, char* out) {
  if (p.a1 != p.a2 || p.a1 == 0) return "n/a";
  int diff = p.c1 - p.c2;
  if (diff % p.a1 != 0) return "none";
  int d = diff / p.a1;
  if (std::abs(d) > p.n - 1) return "none";
  std::snprintf(out, 16, "d = %d", d);
  return out;
}

// Ground truth: try every pair of iterations.
const char* brute_force(const Pair& p) {
  for (int i1 = 0; i1 < p.n; ++i1)
    for (int i2 = 0; i2 < p.n; ++i2)
      if (p.a1 * i1 + p.c1 == p.a2 * i2 + p.c2) return "yes";
  return "no";
}

int main() {
  Pair pairs[] = {
      {"x[i]      x[i - 1]", 1, 0, 1, -1, 10},
      {"x[3i + 6] x[3i]", 3, 6, 3, 0, 10},
      {"x[2i]     x[2i + 1]", 2, 0, 2, 1, 10},
      {"x[i + 20] x[i]", 1, 20, 1, 0, 10},
      {"x[2i]     x[4i + 1]", 2, 0, 4, 1, 10},
      {"x[2i]     x[i + 20]", 2, 0, 1, 20, 10},
  };
  std::printf("%-21s %-6s %-10s %s\n", "write     read", "GCD", "strong SIV",
              "brute force");
  for (auto& p : pairs) {
    char buffer[16];
    std::printf("%-21s %-6s %-10s %s\n", p.name, gcd_test(p),
                strong_siv(p, buffer), brute_force(p));
  }
}

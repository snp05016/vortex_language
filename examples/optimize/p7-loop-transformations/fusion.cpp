// Loop fusion merges two loops into one; loop fission (also called
// distribution) splits one loop into two. Each is legal exactly when every
// dependence keeps its direction; in the four cases below, that means every
// value is still written before it is read. The program applies each
// transformation once legally and once illegally, and compares the result
// with the loops as written.
//
// Follows: the fusion conditions listed in LLVM 18's LoopFuse.cpp (condition
// 4: no negative-distance dependence between the loops), the loop
// distribution section of Clang's Language Extensions, and Carr and Kennedy,
// TOPLAS 1994, section 3.2.1 (distribution must not break a recurrence).

#include <array>
#include <print>

constexpr int n = 8;
using Row = std::array<int, n + 1>;

const char *verdict(bool same) { return same ? "same results" : "different results"; }

int main() {
  Row a{};
  for (int i = 0; i <= n; ++i) a[i] = 3 * i + 1;

  // Fusion, legal: iteration i of the second loop reads b[i], which iteration
  // i of the first loop writes. In the fused loop the write still comes first.
  Row b1{}, c1{}, b2{}, c2{};
  for (int i = 0; i < n; ++i) b1[i] = 2 * a[i];
  for (int i = 0; i < n; ++i) c1[i] = b1[i] + 1;
  for (int i = 0; i < n; ++i) {
    b2[i] = 2 * a[i];
    c2[i] = b2[i] + 1;
  }
  std::println("fuse, reads b[i]:     {}", verdict(c1 == c2));

  // Fusion, illegal: the second loop reads b[i + 1]. Fused, iteration i reads
  // it one iteration before iteration i + 1 writes it (distance -1).
  Row b3{}, c3{}, b4{}, c4{};
  b3[n] = b4[n] = 100;
  for (int i = 0; i < n; ++i) b3[i] = 2 * a[i];
  for (int i = 0; i < n; ++i) c3[i] = b3[i + 1] + 1;
  for (int i = 0; i < n; ++i) {
    b4[i] = 2 * a[i];
    c4[i] = b4[i + 1] + 1;
  }
  std::println("fuse, reads b[i + 1]: {}", verdict(c3 == c4));

  // Fission, legal: x is a recurrence, y does not touch x, so all of x can
  // run before any of y.
  Row x1{}, y1{}, x2{}, y2{};
  for (int i = 1; i <= n; ++i) {
    x1[i] = x1[i - 1] + a[i];
    y1[i] = 5 * a[i];
  }
  for (int i = 1; i <= n; ++i) x2[i] = x2[i - 1] + a[i];
  for (int i = 1; i <= n; ++i) y2[i] = 5 * a[i];
  std::println("split, independent:   {}", verdict(x1 == x2 && y1 == y2));

  // Fission, illegal: x[i] needs y[i - 1] and y[i] needs x[i], a cycle.
  // Split, the first loop reads y before the second loop has written it.
  Row x3{}, y3{}, x4{}, y4{};
  for (int i = 1; i <= n; ++i) {
    x3[i] = y3[i - 1] + 1;
    y3[i] = 2 * x3[i];
  }
  for (int i = 1; i <= n; ++i) x4[i] = y4[i - 1] + 1;
  for (int i = 1; i <= n; ++i) y4[i] = 2 * x4[i];
  std::println("split, cycle:         {}", verdict(x3 == x4 && y3 == y4));
}

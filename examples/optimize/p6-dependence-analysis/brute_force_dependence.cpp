// Follows: Goff, Kennedy and Tseng, "Practical Dependence Testing", PLDI 1991,
// section 1 (dependence, distance and direction vectors, the carrying loop),
// and Bacon, Graham and Sharp, ACM Computing Surveys 26(4), 1994, section 6.2
// (a reordered distance vector must stay lexicographically positive).
#include <algorithm>
#include <array>
#include <cstdio>
#include <map>
#include <string>
#include <tuple>
#include <vector>

// Runs every iteration of a small loop nest, records which array element
// each access touches, and compares every earlier iteration with every later
// one. No subscript algebra: this is the ground truth a real test must match.

using Vec = std::array<int, 3>;  // one index (or one distance) per loop level
struct Access { bool write; char array; Vec element; };
using Body = std::vector<Access> (*)(Vec);
using Names = std::array<const char*, 3>;

// c[row, column] += a[row, k] * b[k, column], loops (row, column, k)
std::vector<Access> matmul(Vec i) {
  auto [row, column, k] = i;
  return {{false, 'a', {row, k, 0}}, {false, 'b', {k, column, 0}},
          {false, 'c', {row, column, 0}}, {true, 'c', {row, column, 0}}};
}
// g[i, j] = g[i - 1, j + 1] + 1, loops (i, j); the third level is unused.
// Reads outside the grid touch elements no iteration writes: no dependence.
std::vector<Access> stencil(Vec i) {
  return {{false, 'g', {i[0] - 1, i[1] + 1, 0}}, {true, 'g', {i[0], i[1], 0}}};
}

bool positive(const Vec& d, int depth) {  // first nonzero entry is > 0
  for (int l = 0; l < depth; ++l) if (d[l] != 0) return d[l] > 0;
  return true;  // all zero: same iteration, loop-independent
}

void analyze(const char* name, Body body, int depth, int n, Names loops) {
  std::vector<Vec> order;  // the iterations, in lexicographic (source) order
  for (int x = 0; x < n; ++x)
    for (int y = 0; y < n; ++y)
      for (int z = 0; z < (depth == 3 ? n : 1); ++z) order.push_back({x, y, z});
  std::map<std::tuple<char, std::string, Vec>, int> found;  // how many pairs
  for (size_t e = 0; e < order.size(); ++e)
    for (size_t l = e + 1; l < order.size(); ++l)
      for (auto& s : body(order[e]))
        for (auto& t : body(order[l])) {
          if (s.array != t.array || s.element != t.element) continue;
          if (!s.write && !t.write) continue;  // two reads: no constraint
          const char* kind = s.write ? (t.write ? "output" : "flow") : "anti";
          Vec d{};
          for (int v = 0; v < depth; ++v) d[v] = order[l][v] - order[e][v];
          ++found[{s.array, kind, d}];
        }
  std::printf("%s, %d iterations per loop\n", name, n);
  std::vector<Vec> distances;
  for (auto& [key, count] : found) {
    auto& [array, kind, d] = key;
    int carrier = 0;
    while (d[carrier] == 0) ++carrier;
    std::printf("  %c %-6s distance (%d", array, kind.c_str(), d[0]);
    for (int v = 1; v < depth; ++v) std::printf(", %d", d[v]);
    std::printf("), carried by %s, %d pairs\n", loops[carrier], count);
    distances.push_back(d);
  }
  std::array<int, 3> perm{0, 1, 2};  // try every order of the loop levels
  do {
    if (depth == 2 && perm[2] != 2) continue;
    bool legal = std::all_of(distances.begin(), distances.end(), [&](const Vec& d) {
      return positive({d[perm[0]], d[perm[1]], d[perm[2]]}, depth);
    });
    std::printf("  order");
    for (int v = 0; v < depth; ++v) std::printf(" %s", loops[perm[v]]);
    std::printf(": %s\n", legal ? "legal" : "illegal");
  } while (std::next_permutation(perm.begin(), perm.end()));
}

int main() {
  analyze("matmul", matmul, 3, 3, {"row", "column", "k"});
  analyze("stencil", stencil, 2, 4, {"i", "j", ""});
}

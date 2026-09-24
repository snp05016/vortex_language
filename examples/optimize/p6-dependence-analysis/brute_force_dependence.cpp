// Follows: Goff, Kennedy & Tseng, "Practical Dependence Testing", PLDI 1991
// (dependence classification and distance vectors), doi:10.1145/113445.113448
#include <cstdio>
#include <utility>
#include <vector>

// Enumerates the iterations of two small, 2-deep affine loop nests by brute
// force and reports every dependence it finds between their memory
// accesses: a matrix-vector reduction (one loop-carried dependence, legal
// under any loop order) and a skewed stencil (legal as written, illegal
// once its loops are interchanged).

struct Access {
  bool is_write;
  int array_id;        // which array this access touches
  int idx0, idx1;       // the element it touches
};

using Body = std::vector<Access> (*)(int, int);

// c[row] += a[row][k] * b[k];  one row of a matrix-vector product: the same
// accumulation shape as one (row, column) pair of a matmul, with the column
// loop dropped because it carries no dependence at all.
std::vector<Access> matvec_body(int row, int k) {
  return {
      {false, /*a*/ 0, row, k},
      {false, /*b*/ 1, k, 0},
      {false, /*c*/ 2, row, 0},  // read half of "+="
      {true, /*c*/ 2, row, 0},   // write half of "+="
  };
}

// a[i][j] = a[i - 1][j + 1] + 1;
std::vector<Access> stencil_body(int i, int j) {
  return {
      {false, /*a*/ 0, i - 1, j + 1},
      {true, /*a*/ 0, i, j},
  };
}

// Finds every dependence between an iteration (i1, j1) that runs first and
// an iteration (i2, j2) that runs later in the nest's original (row-major)
// order, and prints its kind and distance vector.
void report(const char* name, Body body, int n0, int n1) {
  std::printf("%s\n", name);
  bool found = false;
  for (int i1 = 0; i1 < n0; ++i1) {
    for (int j1 = 0; j1 < n1; ++j1) {
      auto earlier = body(i1, j1);
      for (int i2 = i1; i2 < n0; ++i2) {
        for (int j2 = (i2 == i1 ? j1 + 1 : 0); j2 < n1; ++j2) {
          auto later = body(i2, j2);
          for (auto& a : earlier) {
            for (auto& b : later) {
              if (a.array_id != b.array_id) continue;
              if (a.idx0 != b.idx0 || a.idx1 != b.idx1) continue;
              if (!a.is_write && !b.is_write) continue;  // read/read: none
              const char* kind = a.is_write && b.is_write   ? "output"
                                  : a.is_write               ? "flow"
                                                              : "anti";
              std::printf("  %-6s (%d,%d) -> (%d,%d), distance (%d,%d)\n",
                          kind, i1, j1, i2, j2, i2 - i1, j2 - j1);
              found = true;
            }
          }
        }
      }
    }
  }
  if (!found) std::printf("  no dependence\n");
}

// A permutation that swaps the nest's two loop levels is legal exactly when
// every dependence's distance vector, read in the new order, is
// lexicographically positive: its first nonzero component is positive.
bool legal_after_swap(std::pair<int, int> distance) {
  auto [d0, d1] = distance;
  int swapped0 = d1, swapped1 = d0;
  if (swapped0 > 0) return true;
  if (swapped0 < 0) return false;
  return swapped1 >= 0;
}

int main() {
  report("matvec (row, k), 2x2", matvec_body, 2, 2);
  std::printf("  interchange (k, row) legal: %s\n",
              legal_after_swap({0, 1}) ? "yes" : "no");

  report("stencil (i, j), 3x3", stencil_body, 3, 3);
  std::printf("  interchange (j, i) legal: %s\n",
              legal_after_swap({1, -1}) ? "yes" : "no");
}

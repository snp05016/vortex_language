// A cycle-by-cycle model of an output-stationary systolic array.
//
// Idea, from Kung 1982: skew the two operand matrices in time so that
// A[i][k] reaches cell (i, j) exactly when B[k][j] does. Each cell then
// does one multiply-add per cycle and never needs to re-fetch either
// operand: the value that arrived this cycle was pushed to it by its
// neighbour, not read from memory. Cell (i, j) accumulates C[i][j] in
// place ("output-stationary") and is done after K terms have arrived.
//
// For a K x K array computing a K x K times K x K product, cell (i, j)'s
// k-th term arrives at cycle i + j + k, so the whole array finishes at
// cycle 3K - 3 (the last cell is (K-1, K-1) and its last term is
// k = K-1). This program simulates exactly that schedule and checks the
// result against a direct triple loop.

#include <cassert>
#include <cstddef>
#include <print>

constexpr int K = 3;

int main() {
  const int a[K][K] = {{1, 2, 3}, {4, 5, 6}, {7, 8, 9}};
  const int b[K][K] = {{9, 8, 7}, {6, 5, 4}, {3, 2, 1}};

  int c[K][K] = {};
  const int total_cycles = 3 * K - 2; // cycles 0 .. 3K-3, inclusive

  for (int t = 0; t < total_cycles; ++t) {
    std::print("cycle {}: ", t);
    bool any_active = false;
    for (int i = 0; i < K; ++i) {
      for (int j = 0; j < K; ++j) {
        const int k = t - i - j; // which term of the dot product arrives now
        if (k < 0 || k >= K) continue;
        c[i][j] += a[i][k] * b[k][j];
        std::print("({},{},k={}) ", i, j, k);
        any_active = true;
      }
    }
    if (!any_active) std::print("(no cell active: pipeline still filling)");
    std::println("");
  }

  // A direct triple loop must give exactly the same matrix: the skewed
  // schedule only changes when each product is formed, never the order
  // the K products for one output element are summed in.
  int reference[K][K] = {};
  for (int i = 0; i < K; ++i)
    for (int j = 0; j < K; ++j)
      for (int k = 0; k < K; ++k) reference[i][j] += a[i][k] * b[k][j];
  for (int i = 0; i < K; ++i)
    for (int j = 0; j < K; ++j) assert(c[i][j] == reference[i][j]);

  std::println("");
  std::println("C:");
  for (int i = 0; i < K; ++i) {
    for (int j = 0; j < K; ++j) std::print("{:>4}", c[i][j]);
    std::println("");
  }
}

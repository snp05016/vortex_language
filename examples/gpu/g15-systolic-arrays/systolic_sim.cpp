// A register-level model of a 3 x 3 output-stationary systolic array.
//
// Follows the principle in H. T. Kung, "Why Systolic Architectures?" (1982):
// data enter only at the boundary cells and are then pumped from cell to cell,
// so each value read from memory is used at every cell it passes.
//
// Every cycle, each cell takes an `a` value from its left neighbour's register
// (or from memory, if it sits on the left edge) and a `b` value from the
// register above (or from memory, on the top edge), multiplies them into its
// own accumulator, and latches both values for its neighbours. Row i of A is
// fed one cycle later than row i - 1, and column j of B one cycle later than
// column j - 1, so A[i][k] and B[k][j] meet at cell (i, j) on cycle i + j + k.

#include <cassert>
#include <print>

constexpr int K = 3;

struct Reg {
  int value = 0;
  int k = -1; // which term of the dot product this is; -1 is an empty slot
};

int main() {
  const int A[K][K] = {{1, 2, 3}, {4, 5, 6}, {7, 8, 9}};
  const int B[K][K] = {{9, 8, 7}, {6, 5, 4}, {3, 2, 1}};

  Reg a_reg[K][K], b_reg[K][K]; // what each cell passes on next cycle
  int c[K][K] = {};
  int memory_reads = 0, multiply_adds = 0;

  for (int t = 0; t < 3 * K - 2; ++t) {
    Reg a_in[K][K], b_in[K][K];
    for (int i = 0; i < K; ++i)
      for (int j = 0; j < K; ++j) {
        if (j > 0) {
          a_in[i][j] = a_reg[i][j - 1];
        } else if (const int k = t - i; k >= 0 && k < K) {
          a_in[i][j] = {A[i][k], k};
          ++memory_reads;
        }
        if (i > 0) {
          b_in[i][j] = b_reg[i - 1][j];
        } else if (const int k = t - j; k >= 0 && k < K) {
          b_in[i][j] = {B[k][j], k};
          ++memory_reads;
        }
      }

    // Print the k each cell works on this cycle, one grid row per group.
    std::print("cycle {}:", t);
    for (int i = 0; i < K; ++i) {
      std::print("{}", i == 0 ? "  " : " | ");
      for (int j = 0; j < K; ++j) {
        const Reg &a = a_in[i][j], &b = b_in[i][j];
        if (a.k < 0) {
          std::print(" .");
          continue;
        }
        assert(a.k == b.k); // the skew makes matching terms meet
        c[i][j] += a.value * b.value;
        ++multiply_adds;
        std::print(" {}", a.k);
      }
    }
    std::println("");

    for (int i = 0; i < K; ++i)
      for (int j = 0; j < K; ++j) {
        a_reg[i][j] = a_in[i][j];
        b_reg[i][j] = b_in[i][j];
      }
  }

  for (int i = 0; i < K; ++i)
    for (int j = 0; j < K; ++j) {
      int expected = 0;
      for (int k = 0; k < K; ++k) expected += A[i][k] * B[k][j];
      assert(c[i][j] == expected);
    }

  std::println("reads from memory: {}, multiply-adds: {}", memory_reads, multiply_adds);
  for (int i = 0; i < K; ++i)
    std::println("C[{}] = {:>4}{:>4}{:>4}", i, c[i][0], c[i][1], c[i][2]);
}

// Counting the loads of a 64 x 64 x 64 matrix product at three rungs of the
// GPU ladder, on the CPU.
//
// Each rung runs the same arithmetic in a different schedule and counts
// every element it reads, by the memory it reads it from. Rung 1 reads both
// operands from global memory for every multiply-add. Rung 3 copies a
// 32 x 8 slice of `a` and an 8 x 32 slice of `b` into a block's shared
// memory, then reads from there. Rung 5 also gives each thread a 4 x 4 tile
// of outputs, so the values it reads from shared memory serve four outputs
// each. The program then checks that all three produce the same bits: each
// output still adds its products in increasing k, starting from 0.0f.
// Nothing here runs on a GPU; the loops stand for the threads and blocks.

#include <array>
#include <cstddef>
#include <print>

constexpr std::size_t n = 64;                      // M = N = K = 64
constexpr std::size_t bm = 32, bn = 32, bk = 8;    // block tile
constexpr std::size_t tm = 4, tn = 4;              // thread tile

using Matrix = std::array<std::array<float, n>, n>;

struct Counts {
  std::size_t global = 0, shared = 0;
};

// Rung 1: one thread per output, every operand from global memory.
Counts naive(const Matrix& a, const Matrix& b, Matrix& c) {
  Counts count;
  for (std::size_t row = 0; row < n; ++row)
    for (std::size_t col = 0; col < n; ++col) {
      float sum = 0.0f;
      for (std::size_t k = 0; k < n; ++k) {
        sum += a[row][k] * b[k][col];
        count.global += 2;
      }
      c[row][col] = sum;
    }
  return count;
}

// Rungs 3 and 5: block tiles staged through shared memory. With thread
// tile t x t, each thread reads t values of `a` and t of `b` per k and
// updates t * t accumulators (t = 1 is rung 3).
Counts tiled(const Matrix& a, const Matrix& b, Matrix& c, std::size_t t_m,
             std::size_t t_n) {
  Counts count;
  for (std::size_t r0 = 0; r0 < n; r0 += bm)
    for (std::size_t c0 = 0; c0 < n; c0 += bn) {
      float acc[bm][bn] = {};  // every thread's accumulators, kept across k0
      for (std::size_t k0 = 0; k0 < n; k0 += bk) {
        float as[bm][bk], bs[bk][bn];  // the block's shared memory
        for (std::size_t i = 0; i < bm; ++i)
          for (std::size_t k = 0; k < bk; ++k, ++count.global)
            as[i][k] = a[r0 + i][k0 + k];
        for (std::size_t k = 0; k < bk; ++k)
          for (std::size_t j = 0; j < bn; ++j, ++count.global)
            bs[k][j] = b[k0 + k][c0 + j];
        // (a barrier goes here on a GPU)
        for (std::size_t tr = 0; tr < bm; tr += t_m)    // one thread per
          for (std::size_t tc = 0; tc < bn; tc += t_n)  // t_m x t_n tile
            for (std::size_t k = 0; k < bk; ++k) {
              float a_reg[tm], b_reg[tn];  // the thread's registers
              for (std::size_t i = 0; i < t_m; ++i, ++count.shared)
                a_reg[i] = as[tr + i][k];
              for (std::size_t j = 0; j < t_n; ++j, ++count.shared)
                b_reg[j] = bs[k][tc + j];
              for (std::size_t i = 0; i < t_m; ++i)  // outer product
                for (std::size_t j = 0; j < t_n; ++j)
                  acc[tr + i][tc + j] += a_reg[i] * b_reg[j];
            }
      }
      for (std::size_t i = 0; i < bm; ++i)
        for (std::size_t j = 0; j < bn; ++j) c[r0 + i][c0 + j] = acc[i][j];
    }
  return count;
}

int main() {
  static Matrix a, b, c1, c3, c5;
  for (std::size_t i = 0; i < n; ++i)
    for (std::size_t j = 0; j < n; ++j) {
      a[i][j] = 0.1f * static_cast<float>((i * 7 + j * 3) % 17) - 0.8f;
      b[i][j] = 0.3f * static_cast<float>((i * 5 + j * 11) % 13) - 1.7f;
    }

  Counts r1 = naive(a, b, c1);
  Counts r3 = tiled(a, b, c3, 1, 1);
  Counts r5 = tiled(a, b, c5, tm, tn);

  std::println("{:<34} {:>10} {:>10}", "rung", "global", "shared");
  std::println("{:<34} {:>10} {:>10}", "1  naive", r1.global, r1.shared);
  std::println("{:<34} {:>10} {:>10}", "3  block tile 32 x 32 x 8",
               r3.global, r3.shared);
  std::println("{:<34} {:>10} {:>10}", "5  same, thread tile 4 x 4",
               r5.global, r5.shared);
  std::println("rung 3 bits equal rung 1: {}", c3 == c1 ? "yes" : "no");
  std::println("rung 5 bits equal rung 1: {}", c5 == c1 ? "yes" : "no");
}

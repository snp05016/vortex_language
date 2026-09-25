// A matrix unit computes D = A * B + C for one small tile of fixed shape in a
// single instruction. This program models that instruction on a 2 x 2 x 2
// tile of integers, then builds a 4 x 4 x 4 product out of it by chaining
// tiles along the reduction dimension k, each result fed in as the next C,
// and checks the answer against the plain triple loop. Integers keep every
// sum exact, so the check is about which products are added, not rounding.
// Last, it counts tile instructions for larger products and tile shapes.
//
// Follows: https://docs.nvidia.com/cuda/parallel-thread-execution/index.html#warp-level-matrix-instructions
//          https://developer.nvidia.com/blog/programming-tensor-cores-cuda-9/

#include <array>
#include <cstddef>
#include <print>

constexpr std::size_t t = 2;  // the one tile shape this "unit" accepts
using Tile = std::array<std::array<int, t>, t>;

// The "instruction": fixed shape, no loop visible to the program.
Tile mma(const Tile& a, const Tile& b, const Tile& c) {
  Tile d = c;
  for (std::size_t i = 0; i < t; ++i)
    for (std::size_t j = 0; j < t; ++j)
      for (std::size_t k = 0; k < t; ++k) d[i][j] += a[i][k] * b[k][j];
  return d;
}

constexpr std::size_t n = 4;
using Matrix = std::array<std::array<int, n>, n>;

Tile tile_of(const Matrix& m, std::size_t r, std::size_t c) {
  Tile x{};
  for (std::size_t i = 0; i < t; ++i)
    for (std::size_t j = 0; j < t; ++j) x[i][j] = m[r * t + i][c * t + j];
  return x;
}

int main() {
  Tile d = mma({{{2, 0}, {1, 1}}}, {{{1, 3}, {2, 0}}}, {{{1, 1}, {0, 1}}});
  std::println("one instruction: D = [[{}, {}], [{}, {}]]", d[0][0], d[0][1], d[1][0], d[1][1]);

  Matrix a{}, b{}, scalar{}, tiled{};
  for (std::size_t i = 0; i < n; ++i)
    for (std::size_t j = 0; j < n; ++j) {
      a[i][j] = static_cast<int>((i + 2 * j) % 5) - 1;
      b[i][j] = static_cast<int>((3 * i + j) % 4) - 1;
    }
  for (std::size_t i = 0; i < n; ++i)  // the reference: n^3 multiply-adds
    for (std::size_t j = 0; j < n; ++j)
      for (std::size_t k = 0; k < n; ++k) scalar[i][j] += a[i][k] * b[k][j];

  std::size_t instructions = 0;
  for (std::size_t ti = 0; ti < n / t; ++ti)
    for (std::size_t tj = 0; tj < n / t; ++tj) {
      Tile acc{};                                  // C starts at zero
      for (std::size_t tk = 0; tk < n / t; ++tk) {  // chain along k
        acc = mma(tile_of(a, ti, tk), tile_of(b, tk, tj), acc);
        ++instructions;
      }
      for (std::size_t i = 0; i < t; ++i)
        for (std::size_t j = 0; j < t; ++j) tiled[ti * t + i][tj * t + j] = acc[i][j];
    }
  std::println("4x4x4 from 2x2x2 tiles: {} instructions of {} multiply-adds, matches loop: {}",
               instructions, t * t * t, tiled == scalar);

  // Counting only: an M x N x K product on an m x n x k tile shape that
  // divides it exactly takes (M/m)(N/n)(K/k) instructions.
  struct Case { std::size_t M, N, K, m, n, k; };
  for (Case c : {Case{64, 64, 64, 16, 16, 16}, Case{64, 64, 64, 16, 8, 16}, Case{64, 64, 64, 8, 8, 8}}) {
    std::size_t count = (c.M / c.m) * (c.N / c.n) * (c.K / c.k);
    std::println("{}x{}x{} on {}x{}x{}: {} instructions x {} = {} multiply-adds", c.M, c.N, c.K,
                 c.m, c.n, c.k, count, c.m * c.n * c.k, count * c.m * c.n * c.k);
  }
}

// One matrix-unit instruction multiplies and accumulates a whole small tile:
// D = A * B + C for fixed-size A, B, C, D (the shape WMMA and the SIMD-group
// matrix functions both expose). A scalar multiply-add instruction does the
// same job for one number. This example tiles an N x N x N matrix multiply
// into T x T x T pieces along every axis and counts how many tile
// instructions that takes, and how many scalar multiply-adds each tile
// instruction stands in for. The total work does not change; the number of
// instructions the hardware has to issue for it does.
//
// Follows: https://developer.nvidia.com/blog/programming-tensor-cores-cuda-9/
//          https://developer.apple.com/metal/Metal-Shading-Language-Specification.pdf (6.8, SIMD-group matrix functions)

#include <cstddef>
#include <print>
#include <stdexcept>

struct Counts {
  std::size_t tile_ops = 0;       // one D = A*B + C per call
  std::size_t scalar_macs = 0;    // scalar multiply-adds those tile ops cover
};

// Tiling an N x N x N matmul into T x T x T tiles along every axis: each of
// the (N/T)^2 output tiles accumulates over N/T tiles of the reduction
// dimension, one tile instruction per step, the way a real matrix unit
// chains D = A*B + C across the reduction.
Counts tile_matmul(std::size_t n, std::size_t t) {
  if (n % t != 0) throw std::invalid_argument("n must be a multiple of t");
  std::size_t tiles_per_side = n / t;
  Counts c;
  c.tile_ops = tiles_per_side * tiles_per_side * tiles_per_side;
  c.scalar_macs = c.tile_ops * t * t * t;
  return c;
}

void row(std::size_t n, std::size_t t) {
  Counts c = tile_matmul(n, t);
  std::size_t naive_scalar_instructions = n * n * n;
  std::println("n={:<4} t={:<3} tile_ops={:<8} scalar_macs={:<10} naive_scalar_instructions={:<10} macs_match={}",
               n, t, c.tile_ops, c.scalar_macs, naive_scalar_instructions,
               c.scalar_macs == naive_scalar_instructions);
}

int main() {
  row(64, 8);
  row(64, 16);
  row(256, 16);
}

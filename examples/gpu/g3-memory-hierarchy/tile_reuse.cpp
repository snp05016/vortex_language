// Why a thread block stages tiles in shared memory: count the reads.
//
// An N x N matrix product runs one thread per output element, in blocks of
// T x T threads. The naive schedule reads a[row, k] and b[k, column] from
// device memory for every k. The tiled schedule walks k in phases of T: in
// each phase every thread copies one element of a and one of b into the
// block's two T x T shared tiles, and then all T x T threads read the tiles
// T times each. Both schedules add the products in increasing k from 0.0,
// so their results must match bit for bit; the program checks that too.
// Follows: NVIDIA, CUDA Programming Guide, "GPU Device Memory Spaces".

#include <cstdint>
#include <cstring>
#include <print>
#include <vector>

struct Counts {
  std::uint64_t device_reads = 0;  // loads from device (global) memory
  std::uint64_t shared_reads = 0;  // loads from a block's shared tiles
};

using Matrix = std::vector<float>;

Counts naive(int n, const Matrix &a, const Matrix &b, Matrix &c) {
  Counts counts;
  for (int row = 0; row < n; ++row)
    for (int col = 0; col < n; ++col) {  // one thread's work
      float sum = 0.0f;                  // lives in a register
      for (int k = 0; k < n; ++k) {
        sum += a[row * n + k] * b[k * n + col];
        counts.device_reads += 2;
      }
      c[row * n + col] = sum;
    }
  return counts;
}

Counts tiled(int n, int t, const Matrix &a, const Matrix &b, Matrix &c) {
  Counts counts;
  std::vector<float> tile_a(t * t), tile_b(t * t);  // one block's shared memory
  for (int r0 = 0; r0 < n; r0 += t)
    for (int c0 = 0; c0 < n; c0 += t) {        // one thread block
      std::vector<float> sum(t * t, 0.0f);     // one register per thread
      for (int k0 = 0; k0 < n; k0 += t) {      // one phase
        for (int i = 0; i < t; ++i)            // each thread copies two values
          for (int j = 0; j < t; ++j) {
            tile_a[i * t + j] = a[(r0 + i) * n + (k0 + j)];
            tile_b[i * t + j] = b[(k0 + i) * n + (c0 + j)];
            counts.device_reads += 2;
          }
        // (a barrier goes here on a real GPU: G6)
        for (int i = 0; i < t; ++i)
          for (int j = 0; j < t; ++j)
            for (int k = 0; k < t; ++k) {
              sum[i * t + j] += tile_a[i * t + k] * tile_b[k * t + j];
              counts.shared_reads += 2;
            }
      }
      for (int i = 0; i < t; ++i)
        for (int j = 0; j < t; ++j) c[(r0 + i) * n + (c0 + j)] = sum[i * t + j];
    }
  return counts;
}

int main() {
  for (int n : {4, 64}) {
    Matrix a(n * n), b(n * n), c1(n * n), c2(n * n);
    for (int i = 0; i < n * n; ++i) {  // small values with inexact products
      a[i] = static_cast<float>(i % 7) * 0.1f;
      b[i] = static_cast<float>(i % 5) * 0.3f;
    }
    Counts base = naive(n, a, b, c1);
    std::println("N = {}: naive reads {} values from device memory", n, base.device_reads);
    for (int t : {2, 4, 8, 16, 32}) {
      if (t > n) break;
      Counts tc = tiled(n, t, a, b, c2);
      bool same = std::memcmp(c1.data(), c2.data(), c1.size() * sizeof(float)) == 0;
      std::println("  T = {:>2}: device {:>6} (1/{} of naive), shared {:>6}, "
                   "tiles {:>5} bytes, bits {}",
                   t, tc.device_reads, base.device_reads / tc.device_reads,
                   tc.shared_reads, 2 * t * t * sizeof(float), same ? "equal" : "DIFFER");
    }
  }
}

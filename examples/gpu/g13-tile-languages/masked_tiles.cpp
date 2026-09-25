// A tile program sees whole tiles, so an edge that a tile does not divide
// is handled with a mask: lanes outside the array load a padding value and
// store nothing. This program runs a Triton-style "one program per output
// tile" matmul on the CPU, 10 x 10 x 10 with 4 x 4 x 4 tiles (tile edges
// are powers of two in Triton and CUDA Tile), and checks two claims:
//   1. masked edges plus zero padding along K give the same bits as the
//      plain triple loop, because each sum still runs k = 0, 1, 2, ... and
//      adding +0.0 to a sum that started at +0.0 changes nothing;
//   2. splitting K into chunks with their own partial sums (split-K) is a
//      different order of additions, so some outputs change.

#include <bit>
#include <cstddef>
#include <cstdint>
#include <print>

constexpr std::size_t M = 10, N = 10, K = 10, T = 4;   // T: tile edge
constexpr std::size_t tiles(std::size_t n) { return (n + T - 1) / T; }

float a[M][K], b[K][N], ref[M][N], tiled[M][N], split[M][N];

// Masked load: positions outside the array read `other`, as tl.load does.
float load(const float *base, std::size_t row, std::size_t col,
           std::size_t rows, std::size_t cols) {
  return (row < rows && col < cols) ? base[row * cols + col] : 0.0f;
}

void run_program(std::size_t pm, std::size_t pn) {
  float acc[T][T] = {};
  for (std::size_t kb = 0; kb < tiles(K); ++kb)          // K tiles in order
    for (std::size_t kk = 0; kk < T; ++kk)
      for (std::size_t i = 0; i < T; ++i)
        for (std::size_t j = 0; j < T; ++j) {
          const std::size_t r = pm * T + i, c = pn * T + j, k = kb * T + kk;
          const float p = load(&a[0][0], r, k, M, K) * load(&b[0][0], k, c, K, N);
          acc[i][j] += p;             // separate statement: no fused multiply-add
        }
  for (std::size_t i = 0; i < T; ++i)                   // masked store
    for (std::size_t j = 0; j < T; ++j)
      if (pm * T + i < M && pn * T + j < N) tiled[pm * T + i][pn * T + j] = acc[i][j];
}

int main() {
  for (std::size_t i = 0; i < M; ++i)
    for (std::size_t k = 0; k < K; ++k) a[i][k] = 1.0f / float(i + 2 * k + 3);
  for (std::size_t k = 0; k < K; ++k)
    for (std::size_t j = 0; j < N; ++j) b[k][j] = float(int(j) - int(k)) / 7.0f;

  for (std::size_t i = 0; i < M; ++i)
    for (std::size_t j = 0; j < N; ++j) {
      float sum = 0.0f, chunk = 0.0f, total = 0.0f;
      for (std::size_t k = 0; k < K; ++k) {
        const float p = a[i][k] * b[k][j];
        sum += p;
        chunk += p;
        if (k % T == T - 1 || k == K - 1) { total += chunk; chunk = 0.0f; }
      }
      ref[i][j] = sum;
      split[i][j] = total;
    }

  for (std::size_t pm = 0; pm < tiles(M); ++pm)
    for (std::size_t pn = 0; pn < tiles(N); ++pn) run_program(pm, pn);

  std::size_t same = 0, split_same = 0;
  for (std::size_t i = 0; i < M; ++i)
    for (std::size_t j = 0; j < N; ++j) {
      same += std::bit_cast<std::uint32_t>(ref[i][j]) == std::bit_cast<std::uint32_t>(tiled[i][j]);
      split_same += std::bit_cast<std::uint32_t>(ref[i][j]) == std::bit_cast<std::uint32_t>(split[i][j]);
    }

  const std::size_t programs = tiles(M) * tiles(N), slots = programs * T * T;
  std::println("{} x {} outputs, {} x {} tiles: {} programs", M, N, T, T, programs);
  std::println("output slots computed {}, real {}, masked off {}", slots, M * N, slots - M * N);
  std::println("K steps per sum: {} real, {} with padding", K, tiles(K) * T);
  std::println("masked tiles vs triple loop: {} of {} bit-identical", same, M * N);
  std::println("split-K vs triple loop:      {} of {} bit-identical", split_same, M * N);
}

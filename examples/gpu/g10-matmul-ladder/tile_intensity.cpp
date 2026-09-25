// Arithmetic intensity of an output tile: FLOPs done per byte read.
//
// A tile of m x n outputs shares its inputs. One step of the k loop reads
// m values of `a` and n values of `b` (4 bytes each, f32) and does one
// multiply and one add per output: 2 * m * n FLOPs. They are counted
// separately because decision 56 forbids fusing them into one rounding.
// The formula does not say which memory the bytes come from: for a block
// tile they come from global memory into shared memory, for a thread tile
// from shared memory into registers. Nothing here runs on a GPU.

#include <cstddef>
#include <format>
#include <print>
#include <utility>

constexpr double f32_bytes = 4.0;

double intensity(std::size_t m, std::size_t n) {
  double flops = 2.0 * static_cast<double>(m * n);
  double bytes = f32_bytes * static_cast<double>(m + n);
  return flops / bytes;
}

int main() {
  constexpr std::pair<std::size_t, std::size_t> tiles[] = {
      {1, 1},    {2, 2},   {4, 4},    {8, 8},     {16, 16},
      {32, 32},  {64, 64}, {128, 128}, {8, 1},    {8, 4},
      {128, 64},
  };
  std::println("{:<12} {:>12}", "tile", "FLOPs/byte");
  for (auto [m, n] : tiles) {
    std::println("{:<12} {:>12.2f}", std::format("{} x {}", m, n),
                 intensity(m, n));
  }
}

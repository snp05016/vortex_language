// Arithmetic intensity of a square output tile, at two memory levels.
//
// A tile of BM x BN outputs shares its inputs: one step of the reduction
// loop needs BM elements of one operand and BN of the other, and does
// 2 * BM * BN floating-point operations with them (one multiply and one add
// per output, counted separately because Vortex's strict rule forbids
// fusing them into one rounding, decision 56). Intensity is FLOPs moved per
// byte fetched. The same formula applies whether the tile is read from
// global memory into shared memory, or from shared memory into registers;
// only the memory levels change. Nothing here runs on a GPU: it is the
// arithmetic behind the ladder's rungs, not a measurement of any of them.

#include <cstddef>
#include <print>
#include <string_view>

constexpr std::size_t f32_bytes = 4;

// flops_per_byte for a BM x BN tile, reading each operand once per step.
double intensity(std::size_t bm, std::size_t bn) {
  double flops = 2.0 * static_cast<double>(bm) * static_cast<double>(bn);
  double bytes = static_cast<double>(f32_bytes) *
                 static_cast<double>(bm + bn);
  return flops / bytes;
}

void ladder(std::string_view level) {
  std::println("{}", level);
  std::println("{:<12} {:>14}", "edge (t)", "FLOPs/byte");
  for (std::size_t t : {1uz, 2uz, 4uz, 8uz, 16uz, 32uz, 64uz, 128uz}) {
    std::println("{:<12} {:>14.2f}", t, intensity(t, t));
  }
}

int main() {
  ladder("Block tile: t x t outputs, read from global memory");
  std::println("");
  ladder("Thread tile: t x t outputs, read from shared memory");
}

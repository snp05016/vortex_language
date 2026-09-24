// The same array of f32 values, summed two ways: strictly left to right, and
// in blocks of 16 combined after. Floating-point addition is not
// associative, so a tuner that reorders a reduction to search a wider space
// can change the answer, not just the time. This is why a legal tuning move
// must be checked against the language's floating-point rule, not only
// against the loop's dependences.
//
// Follows: no external source.

#include <bit>
#include <cstdint>
#include <print>
#include <vector>

std::vector<float> make_values() {
  std::vector<float> values;
  values.reserve(4096);
  values.push_back(1.0e7f);
  for (int i = 0; i < 4095; ++i) values.push_back(0.3f);
  return values;
}

float sequential_sum(const std::vector<float>& values) {
  float total = 0.0f;
  for (float v : values) total += v;
  return total;
}

// Same values, same left-to-right order within each block of 16, but the
// 16 block totals are then combined in reverse block order.
float block_reordered_sum(const std::vector<float>& values) {
  constexpr std::size_t block = 16;
  std::vector<float> partials;
  for (std::size_t start = 0; start < values.size(); start += block) {
    float partial = 0.0f;
    for (std::size_t i = start; i < start + block && i < values.size(); ++i) {
      partial += values[i];
    }
    partials.push_back(partial);
  }
  float total = 0.0f;
  for (auto it = partials.rbegin(); it != partials.rend(); ++it) total += *it;
  return total;
}

int main() {
  std::vector<float> values = make_values();
  float a = sequential_sum(values);
  float b = block_reordered_sum(values);
  std::println("sequential:      {} (bits {:#010x})", a, std::bit_cast<std::uint32_t>(a));
  std::println("block, reversed: {} (bits {:#010x})", b, std::bit_cast<std::uint32_t>(b));
  std::println("identical bits: {}", a == b);
  return 0;
}

// Detect signed 32-bit overflow before trusting a sum, the way generated
// code must check every addition whose result might not fit its type.
//
// Follows: Clang Language Extensions, "Checked Arithmetic Builtins", and
// the GCC manual, "Integer Overflow Builtins".

#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <print>
#include <utility>

// Mirrors the check a compiler inserts before every integer addition:
// compute the sum and ask, in the same step, whether it overflowed.
std::optional<std::int32_t> checked_add(std::int32_t left,
                                         std::int32_t right) {
  std::int32_t result{};
  if (__builtin_add_overflow(left, right, &result)) {
    return std::nullopt;
  }
  return result;
}

int main() {
  constexpr auto kMax = std::numeric_limits<std::int32_t>::max();
  const std::array<std::pair<std::int32_t, std::int32_t>, 3> tries{{
      {2, 3},
      {kMax - 1, 1},
      {kMax, 1},
  }};

  for (const auto& [left, right] : tries) {
    const auto sum = checked_add(left, right);
    if (sum) {
      std::println("{} + {} = {}", left, right, *sum);
    } else {
      std::println("{} + {} overflows i32", left, right);
    }
  }
}

// Validate a shift count against the width of the type before trusting the
// shift: the check that guards Vortex's `<<` and `>>`.
//
// Follows: cppreference, "Arithmetic operators" (shift operators).

#include <array>
#include <cstdint>
#include <optional>
#include <print>
#include <utility>

constexpr int kBits = 32;

// A count is valid only when it is at least 0 and below the type's width.
// C++20 fixed right shift of a negative value to be an arithmetic shift, so
// the sign bit is copied in, matching Vortex's rule for signed `>>`.
std::optional<std::int32_t> checked_shift_right(std::int32_t value,
                                                 int count) {
  if (count < 0 || count >= kBits) {
    return std::nullopt;
  }
  return value >> count;
}

int main() {
  const std::array<std::pair<std::int32_t, int>, 4> tries{{
      {-8, 1},
      {1, 31},
      {1, 32},
      {1, -1},
  }};

  for (const auto& [value, count] : tries) {
    const auto shifted = checked_shift_right(value, count);
    if (shifted) {
      std::println("{} >> {} = {}", value, count, *shifted);
    } else {
      std::println("{} >> {}: invalid shift count", value, count);
    }
  }
}

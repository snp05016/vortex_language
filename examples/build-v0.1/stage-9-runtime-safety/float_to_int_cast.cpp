// Check whether truncating a floating-point value toward zero fits the
// destination integer type: the check a float-to-integer cast needs before
// it runs.
//
// Follows: cppreference std::isnan, std::isinf, std::trunc, std::numeric_limits.

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <print>

struct Case {
  const char* label;
  double value;
};

// A float-to-int cast fails on NaN, an infinity, or a value that truncates
// to something outside the destination's range.
std::optional<std::int32_t> checked_cast_to_i32(double value) {
  if (std::isnan(value) || std::isinf(value)) {
    return std::nullopt;
  }
  const double truncated = std::trunc(value);
  constexpr auto kMin =
      static_cast<double>(std::numeric_limits<std::int32_t>::min());
  constexpr auto kMax =
      static_cast<double>(std::numeric_limits<std::int32_t>::max());
  if (truncated < kMin || truncated > kMax) {
    return std::nullopt;
  }
  return static_cast<std::int32_t>(truncated);
}

int main() {
  // The last two sit on either side of the edge: 2147483647.5 truncates to
  // the largest i32, and 2147483648.0 is one past it.
  const std::array<Case, 6> tries{{
      {"21.8", 21.8},
      {"-21.8", -21.8},
      {"NaN", std::numeric_limits<double>::quiet_NaN()},
      {"infinity", std::numeric_limits<double>::infinity()},
      {"2147483647.5", 2147483647.5},
      {"2147483648.0", 2147483648.0},
  }};

  for (const auto& [label, value] : tries) {
    const auto cast = checked_cast_to_i32(value);
    if (cast) {
      std::println("i32({}) = {}", label, *cast);
    } else {
      std::println("i32({}): invalid cast", label);
    }
  }
}

// A calculator that folds constants ahead of time must check each result
// before it trusts it: does a sum still fit its 32-bit type, and does a
// value survive being narrowed to an unsigned type? The C++ cast alone
// cannot answer the second question, because it wraps instead of failing.
//
// Follows cppreference's std::in_range, which asks whether a value of one
// integer type is representable in another.

#include <cstdint>
#include <print>
#include <utility>

void report_add(std::int32_t a, std::int32_t b) {
  // Add in a wider type, where the sum cannot overflow, then ask whether
  // the result fits the narrower one.
  const std::int64_t sum = std::int64_t{a} + b;
  if (std::in_range<std::int32_t>(sum)) {
    std::println("{} + {} -> {}", a, b, sum);
  } else {
    std::println("{} + {} -> overflow", a, b);
  }
}

void report_cast(std::int32_t value) {
  std::println("u32({}): the C++ cast gives {}, the range check says {}", value,
               static_cast<std::uint32_t>(value),
               std::in_range<std::uint32_t>(value) ? "fits" : "overflow");
}

int main() {
  report_add(2147483646, 1);
  report_add(2147483647, 1);
  report_cast(1);
  report_cast(-1);
}

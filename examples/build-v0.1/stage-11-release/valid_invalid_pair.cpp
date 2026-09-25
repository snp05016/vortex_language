// A valid/invalid pair, generalized. This checker accepts a digit string
// with no sign and no leading zero (unless the whole value is "0"), and
// reports which rule failed when it does not. A real test suite would also
// record where in the string the failure starts, not only why.
//
// Follows: cppreference std::expected.

#include <expected>
#include <print>
#include <string_view>

enum class Rejected { not_digits, leading_zero };

std::expected<int, Rejected> parse_key(std::string_view text) {
  if (text.empty()) {
    return std::unexpected(Rejected::not_digits);
  }
  for (char c : text) {
    if (c < '0' || c > '9') {
      return std::unexpected(Rejected::not_digits);
    }
  }
  if (text.size() > 1 && text.front() == '0') {
    return std::unexpected(Rejected::leading_zero);
  }
  int value = 0;
  for (char c : text) {
    value = value * 10 + (c - '0');
  }
  return value;
}

const char* name(Rejected reason) {
  return reason == Rejected::not_digits ? "not-digits" : "leading-zero";
}

void check(std::string_view text) {
  const auto result = parse_key(text);
  if (result) {
    std::println("\"{}\": accepted, value {}", text, *result);
  } else {
    std::println("\"{}\": rejected, {}", text, name(result.error()));
  }
}

int main() {
  // Each pair below differs by exactly the property its rule is about.
  check("42");
  check("042");  // pair for "42": only the leading zero changes
  check("0");
  check("7a");
  check("");
}

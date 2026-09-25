// A miniature calculator that gives each number a kind, whole or
// fractional, from its spelling alone, then requires both operands of `+`
// to share a kind. Nothing converts one kind into the other, so mixing
// them is an error, not a silent conversion.
//
// Follows cppreference's std::from_chars, which reports how far into the
// text it read, and std::variant, used here to hold either kind of value.

#include <charconv>
#include <optional>
#include <print>
#include <string_view>
#include <variant>

using Value = std::variant<long long, double>;

// A literal's kind comes only from how it is written: a '.' makes it
// fractional, otherwise it is whole. Nothing else about the program can
// change that once the digits are read.
Value parse_literal(std::string_view text) {
  if (text.find('.') != std::string_view::npos) {
    double value{};
    std::from_chars(text.data(), text.data() + text.size(), value);
    return value;
  }
  long long value{};
  std::from_chars(text.data(), text.data() + text.size(), value);
  return value;
}

// Addition needs both operands to already be the same kind. There is no
// rule here that turns one kind into the other.
std::optional<Value> add(const Value& left, const Value& right) {
  if (left.index() != right.index()) {
    return std::nullopt;
  }
  if (std::holds_alternative<long long>(left)) {
    return std::get<long long>(left) + std::get<long long>(right);
  }
  return std::get<double>(left) + std::get<double>(right);
}

void report(std::string_view a, std::string_view b) {
  const auto result = add(parse_literal(a), parse_literal(b));
  if (!result) {
    std::println("{} + {} -> rejected: kinds differ", a, b);
  } else if (std::holds_alternative<long long>(*result)) {
    std::println("{} + {} -> {}", a, b, std::get<long long>(*result));
  } else {
    std::println("{} + {} -> {}", a, b, std::get<double>(*result));
  }
}

int main() {
  report("10", "20");
  report("0.5", "1.5");
  report("10", "0.5");
}

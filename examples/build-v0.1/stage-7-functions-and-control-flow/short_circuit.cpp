// Whether && and || run their right side, on a problem that has nothing to
// do with a compiler: deciding if a small inventory count is "low but not
// empty". Each predicate prints when it runs, so the trace shows exactly
// which side of each operator was skipped.
//
// Follows: cppreference, "Logical operators", which guarantees that the
// right operand of && and || is evaluated only when the left operand does
// not already decide the result, and that this evaluation is sequenced
// after the left operand's.

#include <print>

bool has_stock(int count) {
  std::println("  has_stock({}) checked", count);
  return count > 0;
}

bool needs_reorder(int count) {
  std::println("  needs_reorder({}) checked", count);
  return count < 3;
}

int main() {
  for (int count : {0, 1, 5}) {
    std::println("count = {}", count);
    bool low_but_present = has_stock(count) && needs_reorder(count);
    std::println("  low_but_present = {}", low_but_present);
    bool out_or_low = !has_stock(count) || needs_reorder(count);
    std::println("  out_or_low = {}", out_or_low);
  }
}

// A print routine's layout is a small, fixed contract: it decides how values
// sit on the line, not what they mean. This example applies one such rule to
// ordinary C++ values: one space between arguments, one line feed after the
// last, and nothing else.
//
// Follows: cppreference on parameter packs and fold expressions, which do the
// joining below, and on std::print.
// https://en.cppreference.com/cpp/language/pack
// https://en.cppreference.com/cpp/language/fold
// https://en.cppreference.com/cpp/io/print

#include <print>

namespace {

// Prints every argument separated by one space, then one line feed. A fold
// expression over the comma operator visits the arguments left to right,
// printing a leading space before every argument after the first.
template <typename... Values>
void print_spaced(const Values&... values) {
  bool first = true;
  ((std::print("{}{}", first ? "" : " ", values), first = false), ...);
  std::println("");
}

}  // namespace

int main() {
  print_spaced(14);                // one argument: no separator to place
  print_spaced("rows:", 3, 4);     // three arguments of different types
  print_spaced(true, 'x', 2.5);    // a bool, a char and a floating-point value
}

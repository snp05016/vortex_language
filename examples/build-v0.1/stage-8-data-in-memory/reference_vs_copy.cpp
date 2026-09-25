// Two ways to hand an array to a function: a reference, which reaches the
// caller's own storage, and a plain value, which receives a copy. Only one
// of them lets the callee's changes reach back to the caller.
//
// Follows: cppreference's page on references, which describes a reference
// as "an alias to an already-existing object", and its page on std::array,
// which has the value semantics of a struct holding a plain array.

#include <array>
#include <print>

using Row = std::array<int, 4>;

void double_in_place(Row &row) {
  for (int &value : row) {
    value *= 2;
  }
}

void double_a_copy(Row row) { // no &, so `row` is a new array
  for (int &value : row) {
    value *= 2;
  }
  // Changes to this local copy never reach the caller.
}

int main() {
  Row values{1, 2, 3, 4};

  double_a_copy(values);
  std::println("after passing by value:     {} {} {} {}", values[0],
                values[1], values[2], values[3]);

  double_in_place(values);
  std::println("after passing by reference: {} {} {} {}", values[0],
                values[1], values[2], values[3]);
}

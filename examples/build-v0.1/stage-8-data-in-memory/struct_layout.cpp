// How much room does a struct take, and where does each field start?
// offsetof, alignof and sizeof answer both questions without guessing.
//
// Follows: cppreference's pages on offsetof (a field's byte offset in a
// standard-layout struct), alignof (the alignment a type requires) and
// sizeof (a class's size includes the padding an array of it needs).

#include <cstddef>
#include <print>

struct Reading {
  bool valid;
  float value;
  bool calibrated;
};

int main() {
  std::println("sizeof(Reading)      = {}", sizeof(Reading));
  std::println("alignof(Reading)     = {}", alignof(Reading));
  std::println("offsetof(valid)      = {}", offsetof(Reading, valid));
  std::println("offsetof(value)      = {}", offsetof(Reading, value));
  std::println("offsetof(calibrated) = {}", offsetof(Reading, calibrated));

  // Fields stay in declaration order, so `value` must skip ahead to the next
  // multiple of its alignment. Putting the two bools together would save
  // space, but C++ does not reorder fields to do it.
  const std::size_t gap = offsetof(Reading, value) - sizeof(Reading::valid);
  std::println("padding after valid  = {}", gap);

  // The size is rounded up to a multiple of the alignment, so the next
  // Reading in an array also starts correctly aligned.
  const std::size_t used =
      offsetof(Reading, calibrated) + sizeof(Reading::calibrated);
  std::println("trailing padding     = {}", sizeof(Reading) - used);
}

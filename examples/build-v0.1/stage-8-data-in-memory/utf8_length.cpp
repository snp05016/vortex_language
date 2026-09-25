// Two different counts for the same piece of text: how many bytes it takes
// in UTF-8, and how many characters (Unicode code points) it holds. They
// only agree when every character is plain ASCII.
//
// Follows: RFC 3629, section 3, where every byte after the first byte of a
// character has its top two bits set to 10.

#include <cstddef>
#include <print>
#include <string_view>

std::size_t count_code_points(std::string_view text) {
  std::size_t count = 0;
  for (unsigned char byte : text) {
    // A UTF-8 continuation byte always has the top two bits 10. Every other
    // byte starts a new character, whether that character is one byte
    // (ASCII) or several.
    const bool is_continuation = (byte & 0b1100'0000) == 0b1000'0000;
    if (!is_continuation) {
      ++count;
    }
  }
  return count;
}

int main() {
  constexpr std::string_view greeting = "naïve café";

  std::println("text:        \"{}\"", greeting);
  std::println("bytes:       {}", greeting.size());
  std::println("code points: {}", count_code_points(greeting));
}

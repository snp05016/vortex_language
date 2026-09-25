// Counts Unicode characters and UTF-8 bytes for a few short strings: the
// distinction a source manager has to make when it turns a byte offset into a
// column.
//
// In valid UTF-8, a byte starts a new character unless its top two bits are
// 10, which marks it as a continuation byte of the character before it.
// Counting the other bytes counts characters without decoding each one.
// Follows: RFC 3629, section 3, "UTF-8 definition".

#include <cstddef>
#include <print>
#include <string_view>

namespace {

std::size_t utf8_character_count(std::string_view text) {
  std::size_t characters = 0;
  for (const unsigned char byte : text) {
    if ((byte & 0xC0) != 0x80) {
      ++characters;
    }
  }
  return characters;
}

void report(std::string_view label, std::string_view text) {
  std::println("{:<16} bytes: {:<3} characters: {}", label, text.size(),
               utf8_character_count(text));
}

}  // namespace

int main() {
  report("\"let\"", "let");
  report("\"café\"", "café");
  report("'λ'", "'λ'");
  report("let s = 'λ';", "let s = 'λ';");
}

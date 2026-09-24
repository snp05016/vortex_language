// Classify each character of a fixed line of text and count the classes:
// the first job a lexer does with its input.
//
// Two rules from the <cctype> documentation matter here:
//   * The argument must fit in an unsigned char (or be EOF). A plain char can
//     be negative for non-ASCII bytes, and passing that is undefined
//     behaviour, so every character is converted to unsigned char first.
//   * The answers depend on the C locale. This program never calls
//     std::setlocale, so it runs in the "C" locale and prints the same counts
//     on every machine.

#include <cctype>
#include <print>
#include <string_view>

int main() {
  constexpr std::string_view text = "area = width * 12 + pad_2;  // cm";

  int letters = 0, digits = 0, spaces = 0, punctuation = 0, other = 0;
  for (const char c : text) {
    const auto byte = static_cast<unsigned char>(c);
    if (std::isalpha(byte)) {
      ++letters;
    } else if (std::isdigit(byte)) {
      ++digits;
    } else if (std::isspace(byte)) {
      ++spaces;
    } else if (std::ispunct(byte)) {
      ++punctuation; // '_' lands here, though a lexer keeps it in names
    } else {
      ++other;
    }
  }

  std::println("text:        \"{}\"", text);
  std::println("letters:     {}", letters);
  std::println("digits:      {}", digits);
  std::println("spaces:      {}", spaces);
  std::println("punctuation: {}", punctuation);
  std::println("other:       {}", other);
}

// The difference between a token's spelling, what the programmer typed, and
// its decoded value, what the running program uses. A lexer keeps the
// spelling and also works out the decoded value for string and character
// literals, so it (not some later stage) is what checks that every escape is
// one of the ones the language supports.
//
// Follows the escape-sequence table on cppreference's page on string and
// character literals, and std::string's growable buffer, also cppreference.

#include <cstddef>
#include <print>
#include <stdexcept>
#include <string>
#include <string_view>

std::string decode(std::string_view raw) {
  std::string out;
  out.reserve(raw.size());
  for (std::size_t i = 0; i < raw.size(); ++i) {
    if (raw[i] != '\\') {
      out.push_back(raw[i]);
      continue;
    }
    ++i;
    if (i == raw.size()) {
      throw std::invalid_argument("trailing backslash");
    }
    switch (raw[i]) {
      case 'n':
        out.push_back('\n');
        break;
      case 't':
        out.push_back('\t');
        break;
      case '\\':
        out.push_back('\\');
        break;
      case '"':
        out.push_back('"');
        break;
      case '\'':
        out.push_back('\'');
        break;
      default:
        throw std::invalid_argument("unsupported escape");
    }
  }
  return out;
}

int main() {
  // Raw string literals here stand in for source spellings: the backslashes
  // below are literal backslash-then-letter pairs, exactly as a lexer would
  // read them from a file, not C++ escapes.
  constexpr std::string_view spellings[] = {
      R"(first line\nsecond line)",
      R"(left\\right)",
      R"(a tab\there)",
  };

  for (const std::string_view spelling : spellings) {
    const std::string decoded = decode(spelling);
    std::println("spelling: {}", spelling);
    std::println("decoded:  {}", decoded);
    std::println("bytes: spelling={} decoded={}", spelling.size(), decoded.size());
    std::println("");
  }

  // The same check that decodes also rejects: `\q` is not a supported escape.
  try {
    decode(R"(bad\q)");
  } catch (const std::invalid_argument& error) {
    std::println("spelling: {}", R"(bad\q)");
    std::println("error:    {}", error.what());
  }
}

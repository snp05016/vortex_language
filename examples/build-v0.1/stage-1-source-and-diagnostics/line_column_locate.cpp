// A settings-file checker stores each problem as a byte offset and turns it
// into a line and column only when it prints the problem. It records where
// every line starts once, then finds the line holding an offset with a binary
// search over that list.
//
// Only a line feed starts a new line, so a carriage return before it stays the
// last byte of the line it ends: a "\r\n" pair is one line break, and both of
// its bytes still count in offsets. The text is ASCII, so a column here is one
// byte; text with multi-byte characters would count characters instead.
// Follows: cppreference, std::upper_bound.

#include <algorithm>
#include <cstddef>
#include <print>
#include <string_view>
#include <vector>

namespace {

std::vector<std::size_t> line_starts(std::string_view text) {
  std::vector<std::size_t> starts{0};
  for (std::size_t i = 0; i < text.size(); ++i) {
    if (text[i] == '\n') {
      starts.push_back(i + 1);
    }
  }
  return starts;
}

void report(const std::vector<std::size_t>& starts, std::size_t offset) {
  // The first start greater than the offset begins the next line, so the
  // offset's own line starts one entry earlier.
  const auto next = std::upper_bound(starts.begin(), starts.end(), offset);
  const auto line = static_cast<std::size_t>(next - starts.begin());
  const std::size_t column = offset - starts[line - 1] + 1;
  std::println("offset {:<2} -> line {}, column {}", offset, line, column);
}

}  // namespace

int main() {
  // Line 1 ends with "\r\n" (2 bytes); line 2 ends with "\n" (1 byte).
  constexpr std::string_view text = "port=80\r\nhost=example\n";
  const std::vector<std::size_t> starts = line_starts(text);
  report(starts, 0);            // first byte of the file
  report(starts, 7);            // the '\r' at the end of line 1
  report(starts, 9);            // first byte of line 2, right after the pair
  report(starts, 14);           // the 'e' of "example"
  report(starts, text.size());  // the end of the file, after the last break
}

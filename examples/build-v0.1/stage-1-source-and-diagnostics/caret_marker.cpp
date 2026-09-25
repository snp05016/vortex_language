// A settings-file checker quotes a bad line and prints a marker line under
// it: a row of spaces (or copied tabs) up to the problem, then at least one
// caret, so even an empty span, such as a missing value, stays visible.
//
// A terminal expands a tab to the next tab stop. A tab copied into the marker
// line sits at the same place as the one in the quoted line, so both expand
// to the same width and the carets stay under the right characters. The
// lines here are ASCII, so a column is one byte.
// Follows: cppreference, std::string::append.

#include <cstddef>
#include <print>
#include <string>
#include <string_view>

namespace {

std::string marker_line(std::string_view source_line, std::size_t start_column,
                         std::size_t length) {
  std::string marker;
  for (std::size_t column = 1; column < start_column; ++column) {
    const char c = source_line[column - 1];
    marker.push_back(c == '\t' ? '\t' : ' ');
  }
  marker.append(length == 0 ? 1 : length, '^');
  return marker;
}

void report(std::string_view line, std::size_t start_column, std::size_t length) {
  std::println("{}", line);
  std::println("{}", marker_line(line, start_column, length));
}

}  // namespace

int main() {
  report("\tport = 8o80", 9, 4);  // "8o80" starts after a tab and 7 bytes
  report("host = ", 8, 0);        // an empty span where the value belongs
}

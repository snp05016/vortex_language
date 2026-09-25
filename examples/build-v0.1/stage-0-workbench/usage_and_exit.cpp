// Separate exit statuses for separate outcomes, in a tool that is not a
// compiler: a tiny "does this text appear in that text" search. Its statuses
// follow the POSIX grep convention: 0 when something was found, 1 when
// nothing was, and above 1 when the tool could not do its job at all (here,
// because it was called with the wrong number of arguments).
//
// A caller that reads only the status can still tell the three apart. With a
// single "failed" status, "not found" and "you called me wrongly" would look
// the same.
//
// The command lines come from a table instead of argv, so the output does not
// depend on how this example was started. main itself returns 0, because the
// examples harness treats any other status as a broken example.

#include <print>
#include <string>
#include <string_view>
#include <vector>

enum Status { found = 0, not_found = 1, usage_error = 2 };

Status search(const std::vector<std::string_view>& args) {
  if (args.size() != 2) return usage_error;  // needs a pattern and a text
  return args[1].find(args[0]) != std::string_view::npos ? found : not_found;
}

int main() {
  const std::vector<std::vector<std::string_view>> command_lines = {
      {"cat", "the cat sat"},
      {"dog", "the cat sat"},
      {"cat"},
      {},
  };
  for (const auto& args : command_lines) {
    std::string shown = "search";
    for (std::string_view a : args) {
      shown += " \"";
      shown += a;
      shown += '"';
    }
    std::println("{} -> status {}", shown, static_cast<int>(search(args)));
  }
}

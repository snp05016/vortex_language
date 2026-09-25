// Look up a name by searching from the innermost scope outward, stopping at
// the first scope that has it. This is the technique any nested-scope
// language uses; here it resolves settings in nested configuration sections,
// not names in a program.
//
// Follows the <unordered_map> and <optional> documentation on cppreference:
// unordered_map::find returns end() on a miss, and std::optional is the
// standard way to report "found, or not" without a sentinel value.

#include <optional>
#include <print>
#include <string>
#include <unordered_map>
#include <vector>

using Scope = std::unordered_map<std::string, std::string>;

std::optional<std::string> lookup(const std::vector<Scope> &scopes,
                                   const std::string &name) {
  for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
    if (auto found = it->find(name); found != it->end()) {
      return found->second;
    }
  }
  return std::nullopt;
}

int main() {
  std::vector<Scope> scopes;
  scopes.push_back({{"theme", "dark"}, {"font_size", "12"}}); // outer section
  scopes.push_back({{"font_size", "14"}});                    // inner section

  for (const char *key : {"font_size", "theme", "margin"}) {
    if (auto value = lookup(scopes, key)) {
      std::println("{}: {} (from the nearest section that sets it)", key,
                    *value);
    } else {
      std::println("{}: not set in any enclosing section", key);
    }
  }
}

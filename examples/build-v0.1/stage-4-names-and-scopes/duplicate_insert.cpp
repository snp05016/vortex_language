// Detect a duplicate key in one scope using the return value of
// unordered_map::insert, instead of a separate lookup before the insert.
//
// Follows cppreference's documentation of unordered_map::insert: it returns
// a pair whose second member is false when the key was already present, and
// the map is left unchanged.

#include <print>
#include <string>
#include <unordered_map>
#include <vector>

int main() {
  std::unordered_map<std::string, int> seats; // holder name -> seat number
  const std::vector<std::pair<std::string, int>> requests = {
      {"amara", 1}, {"beno", 2}, {"amara", 3}, {"chike", 4}};

  for (const auto &[name, seat] : requests) {
    auto [entry, inserted] = seats.insert({name, seat});
    if (inserted) {
      std::println("seat {}: {} (new)", entry->second, entry->first);
    } else {
      std::println("seat {} rejected: {} already holds seat {}", seat, name,
                    entry->second);
    }
  }
}

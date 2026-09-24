// How many passes does a warp need when its lanes disagree?
//
// SIMT hardware issues one instruction to a whole warp at once. NVIDIA's
// guide says each thread keeps its own control flow, but also that a warp's
// performance suffers when its lanes take different paths, because the
// paths are not free: the warp serializes them and masks off the lanes not
// on the current one. This program models the cost as a count of passes,
// the number of distinct choices among a warp's 32 lanes; it is a count of
// serialized work, not a cycle-accurate timing, and independent thread
// scheduling changes how the hardware interleaves the passes, not how many
// there are.

#include <cstddef>
#include <print>
#include <set>

constexpr std::size_t lanes = 32;

// passes(choice) counts the distinct values choice(0), choice(1), ...,
// choice(31) takes: one pass per group of lanes that agrees.
template <class Choice> std::size_t passes(Choice choice) {
  std::set<int> seen;
  for (std::size_t lane = 0; lane < lanes; ++lane) seen.insert(choice(lane));
  return seen.size();
}

void row(const char *what, std::size_t p) {
  std::println("{:<46} {:>3} pass(es)", what, p);
}

int main() {
  row("uniform: every lane takes branch 0",
      passes([](std::size_t) { return 0; }));
  row("parity: lane_id % 2",
      passes([](std::size_t l) { return static_cast<int>(l % 2); }));
  row("4-way switch: lane_id % 4",
      passes([](std::size_t l) { return static_cast<int>(l % 4); }));
  row("every lane its own branch: lane_id",
      passes([](std::size_t l) { return static_cast<int>(l); }));
  row("boundary guard: lane_id < 6 (6 of 32 in range)",
      passes([](std::size_t l) { return l < 6 ? 1 : 0; }));
}

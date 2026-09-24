// A live interval is the cheap summary a linear scan allocator (C3) works
// with: for each name, just [first definition, last use], on a single
// numbering of the instructions. It is an approximation of the exact
// per-instruction liveness that instruction_liveness.cpp computes: exact
// liveness can say a name is dead in the middle and reborn later (the name
// is reused for something unrelated), but an interval cannot represent a
// gap, so it claims the name is live all the way through. That gap is a
// "hole": an instruction inside the interval where the name is neither
// live-in nor live-out.
//   0: x = 1
//   1: y = x + 1
//   2: z = 5
//   3: x = z * 2        (a second, unrelated definition of the name x)
//   4: w = x + z
//   5: store out1, y
//   6: store out2, w
// x's two live ranges are [0, 1] and [3, 4]; its interval is [0, 4], with
// instruction 2 as a hole.
//
// Follows: Wimmer and Mössenböck, "Optimized Interval Splitting in a Linear
// Scan Register Allocator", VEE 2005. https://doi.org/10.1145/1064979.1064998

#include <algorithm>
#include <print>
#include <set>
#include <string>
#include <vector>

struct Instr {
  std::string text;
  std::vector<std::string> uses, defs;
};

int main() {
  const std::vector<Instr> code = {
      {"x = 1", {}, {"x"}},        {"y = x + 1", {"x"}, {"y"}}, {"z = 5", {}, {"z"}},
      {"x = z * 2", {"z"}, {"x"}}, {"w = x + z", {"x", "z"}, {"w"}},
      {"store out1, y", {"y"}, {}}, {"store out2, w", {"w"}, {}},
  };
  const int n = static_cast<int>(code.size());

  // Exact per-instruction liveness, one backward pass (as in instruction_liveness.cpp).
  std::vector<std::set<std::string>> live_in(n), live_out(n);
  for (int i = n - 1; i >= 0; --i) {
    live_out[i] = (i + 1 < n) ? live_in[i + 1] : std::set<std::string>{};
    live_in[i] = live_out[i];
    for (const auto &d : code[i].defs) live_in[i].erase(d);
    for (const auto &u : code[i].uses) live_in[i].insert(u);
  }
  // A name occupies instruction i if it is live entering it or leaving it:
  // that covers the instruction that defines it (live-out, not live-in) and
  // the instruction that last uses it (live-in, not live-out).
  auto is_live = [&](const std::string &name, int i) {
    return live_in[i].count(name) != 0 || live_out[i].count(name) != 0;
  };

  auto span = [&](const std::string &name) {
    int first = -1, last = -1;
    for (int i = 0; i < n; ++i) {
      const bool touches = std::ranges::count(code[i].defs, name) || std::ranges::count(code[i].uses, name);
      if (touches && first < 0) first = i;
      if (touches) last = i;
    }
    return std::pair{first, last};
  };

  // The conservative interval: first definition to last use, over every name
  // that appears (names may repeat, as x does; a real allocator would rename
  // apart, which is exactly what building SSA (O3) does before this point).
  std::set<std::string> names;
  for (const auto &ins : code) {
    for (const auto &d : ins.defs) names.insert(d);
    for (const auto &u : ins.uses) names.insert(u);
  }

  std::println("name  interval  holes");
  for (const auto &name : names) {
    const auto [first, last] = span(name);
    std::string holes;
    for (int i = first; i <= last; ++i)
      if (!is_live(name, i)) holes += (holes.empty() ? "" : ", ") + std::to_string(i);
    std::println("{:4}  [{}, {}]     {}", name, first, last, holes.empty() ? "none" : holes);
  }

  // Register pressure the two views disagree on, at the hole: how many names
  // are truly live there, versus how many names' intervals merely cover it.
  const int probe = 2;
  std::size_t exact_pressure = 0, interval_pressure = 0;
  for (const auto &name : names) {
    if (is_live(name, probe)) ++exact_pressure;
    const auto [first, last] = span(name);
    if (first <= probe && probe <= last) ++interval_pressure;
  }
  std::println("at instruction {}: exactly live = {}, interval-based count = {}", probe, exact_pressure, interval_pressure);
}

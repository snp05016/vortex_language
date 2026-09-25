// What a warp pays when its lanes disagree.
//
// A warp issues one instruction at a time for all of its lanes. When the
// lanes disagree at a branch, the warp runs each path that some lane needs,
// with the other lanes switched off by the active mask. This model counts
// two things per case: instructions the warp issues (time) and
// lane-instructions that do real work (useful work). It is a count, not a
// timing, and it ignores everything but control flow.

#include <bitset>
#include <cstddef>
#include <print>

constexpr std::size_t lanes = 32;
using Mask = std::bitset<lanes>;

struct Cost {
  std::size_t issued = 0;  // instructions the warp issues
  std::size_t useful = 0;  // of the issued lane slots, those with work
};

// A block of `length` instructions, run for the lanes in `mask`. A block
// that no lane needs is skipped: the warp branches around it.
void run(Cost &c, Mask mask, std::size_t length) {
  if (mask.none()) return;
  c.issued += length;
  c.useful += mask.count() * length;
}

// A switch on arm(lane) with `ways` arms, each `length` instructions long
// (arm 0 is empty when `first_empty` is set, as in `if (c) { ... }`).
template <class Arm>
Cost branch(Arm arm, std::size_t ways, std::size_t length,
            bool first_empty = false) {
  Cost c;
  for (std::size_t a = 0; a < ways; ++a) {
    Mask m;
    for (std::size_t l = 0; l < lanes; ++l) m[l] = arm(l) == a;
    run(c, m, first_empty && a == 0 ? 0 : length);
  }
  return c;
}

// A loop whose trip count differs per lane: the warp keeps iterating
// while any lane still has an iteration left.
template <class Trips> Cost loop(Trips trips, std::size_t body) {
  Cost c;
  for (std::size_t it = 0;; ++it) {
    Mask m;
    for (std::size_t l = 0; l < lanes; ++l) m[l] = it < trips(l);
    if (m.none()) return c;
    run(c, m, body);
  }
}

void row(const char *what, Cost c) {
  std::println("{:<34} {:>6} {:>6} {:>7}", what, c.issued, c.useful,
               c.issued * lanes);
}

int main() {
  std::println("{:<34} {:>6} {:>6} {:>7}", "10-instruction arms", "issued",
               "useful", "slots");
  row("uniform: every lane takes arm 0", branch([](auto) { return 0u; }, 2, 10));
  row("parity: lane % 2", branch([](auto l) { return l % 2; }, 2, 10));
  row("halves: lane < 16", branch([](auto l) { return l < 16 ? 0u : 1u; }, 2, 10));
  row("4-way switch: lane % 4", branch([](auto l) { return l % 4; }, 4, 10));
  row("32-way switch: lane", branch([](auto l) { return l; }, 32, 10));
  row("guard: if (lane < 6), no else",
      branch([](auto l) { return l < 6 ? 1u : 0u; }, 2, 10, true));
  std::println("{:<34} {:>6} {:>6} {:>7}", "4-instruction loop body", "issued",
               "useful", "slots");
  row("16 trips in every lane", loop([](auto) { return 16u; }, 4));
  row("lane l makes l trips", loop([](auto l) { return l; }, 4));
}

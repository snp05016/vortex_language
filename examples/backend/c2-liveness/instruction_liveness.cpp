// Per-instruction liveness on a straight-line block of three-address code
// for a tiny expression language: out = (a + b) * (a - b) + a * a.
//   0: t0 = a + b
//   1: t1 = a - b
//   2: t2 = t0 * t1
//   3: t3 = a * a
//   4: t4 = t2 + t3
//   5: store out, t4
// A block-level analysis (O4) would only say "a, b live-in; nothing live-out".
// Register allocation needs more: which names are live between instruction 2
// and instruction 3, say. One backward pass over the instructions answers
// that exactly, because there are no branches to iterate over: live-out(n) is
// live-in(n+1), and live-in(n) drops what n defines and adds what n reads.
//
// Follows: Pfenning and Platzer, CMU 15-411 lecture 4, "Liveness Analysis".
// https://www.cs.cmu.edu/~fp/courses/15411-f13/lectures/04-liveness.pdf

#include <algorithm>
#include <array>
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
      {"t0 = a + b", {"a", "b"}, {"t0"}},
      {"t1 = a - b", {"a", "b"}, {"t1"}},
      {"t2 = t0 * t1", {"t0", "t1"}, {"t2"}},
      {"t3 = a * a", {"a"}, {"t3"}},
      {"t4 = t2 + t3", {"t2", "t3"}, {"t4"}},
      {"store out, t4", {"t4"}, {}},
  };
  const int n = static_cast<int>(code.size());

  std::vector<std::set<std::string>> live_in(n), live_out(n);
  for (int i = n - 1; i >= 0; --i) {
    live_out[i] = (i + 1 < n) ? live_in[i + 1] : std::set<std::string>{};
    live_in[i] = live_out[i];
    for (const auto &d : code[i].defs) live_in[i].erase(d);
    for (const auto &u : code[i].uses) live_in[i].insert(u);
  }

  auto show = [](const std::set<std::string> &s) {
    if (s.empty()) return std::string("{}");
    std::string out = "{";
    bool first = true;
    for (const auto &v : s) out += (first ? first = false, "" : ", ") + v;
    return out + "}";
  };

  std::println("instr  code             live-in          live-out");
  for (int i = 0; i < n; ++i)
    std::println("{:5}  {:<15}  {:<15}  {}", i, code[i].text, show(live_in[i]), show(live_out[i]));

  // Peak register pressure: the largest live-in set at any instruction.
  std::size_t peak = 0;
  for (const auto &s : live_in) peak = std::max(peak, s.size());
  std::println("peak simultaneous live names: {}", peak);
}

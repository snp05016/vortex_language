// Per-instruction liveness on a loop: the backward equations from O4, applied
// to single instructions and repeated until a whole pass changes nothing.
// The program sums b elements of an array whose address arrives in `a`:
//   0: s = 0
//   1: i = 0
//   2: t = load a[i]
//   3: s = s + t
//   4: i = i + 1
//   5: if i < b goto 2
//   6: return s
// Visiting 6, 5, ..., 0 is postorder for this graph, so each pass sees an
// instruction's successors first, except across the back edge 5 -> 2.
// The table prints live-in after each pass, then the live interval each name
// really has, next to the naive "first mention to last mention" guess.
//
// Follows: Pfenning and Platzer, CMU 15-411 lecture 4, "Liveness Analysis",
// sections 4 and 5, and Poletto and Sarkar, "Linear Scan Register
// Allocation", TOPLAS 1999, section 3 (the definition of a live interval).

#include <algorithm>
#include <format>
#include <print>
#include <string>
#include <vector>

struct Instr {
  std::string text, uses, defs;  // one letter per name
  std::vector<int> succ;
};

std::string sorted(std::string s) {
  std::ranges::sort(s);
  s.erase(std::ranges::unique(s).begin(), s.end());
  return s;
}

int main() {
  const std::vector<Instr> code = {
      {"s = 0", "", "s", {1}},          {"i = 0", "", "i", {2}},
      {"t = load a[i]", "ai", "t", {3}}, {"s = s + t", "st", "s", {4}},
      {"i = i + 1", "i", "i", {5}},      {"if i < b goto 2", "ib", "", {2, 6}},
      {"return s", "s", "", {}},
  };
  const int n = static_cast<int>(code.size());
  std::vector<std::string> in(n);
  std::vector<std::vector<std::string>> history;  // live-in after each pass

  for (bool changed = true; changed;) {
    changed = false;
    for (int k = n - 1; k >= 0; --k) {
      std::string out;
      for (int s : code[k].succ) out += in[s];
      std::string now = code[k].uses;  // use(k) plus (out(k) minus def(k))
      for (char v : out)
        if (code[k].defs.find(v) == std::string::npos) now += v;
      now = sorted(now);
      if (now != in[k]) in[k] = now, changed = true;
    }
    history.push_back(in);
  }

  std::string header = "instr  code            ";
  for (std::size_t p = 1; p <= history.size(); ++p) header += std::format("  pass {}", p);
  std::println("{}", header);
  for (int k = 0; k < n; ++k) {
    std::string row = std::format("{:5}  {:<16}", k, code[k].text);
    for (const auto &pass : history) row += std::format("  {:<6}", pass[k]);
    std::println("{}", row.substr(0, row.find_last_not_of(' ') + 1));
  }

  // A name occupies instruction k if it is live entering k, or if k defines
  // it (the result needs a register the moment it is written). The
  // parameters a and b are live entering instruction 0.
  std::println("name  interval from liveness  first to last mention");
  for (char v : std::string("aibts")) {
    int lo = n, hi = -1, mlo = n, mhi = -1;
    for (int k = 0; k < n; ++k) {
      const bool mentioned = code[k].uses.find(v) != std::string::npos ||
                             code[k].defs.find(v) != std::string::npos;
      const bool occupied = in[k].find(v) != std::string::npos ||
                            code[k].defs.find(v) != std::string::npos;
      if (occupied) lo = std::min(lo, k), hi = std::max(hi, k);
      if (mentioned) mlo = std::min(mlo, k), mhi = std::max(mhi, k);
    }
    std::println("{:4}  [{}, {}]{:18}[{}, {}]", v, lo, hi, "", mlo, mhi);
  }
}

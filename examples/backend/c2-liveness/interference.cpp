// Building an interference graph from per-instruction liveness. The rule:
// at each instruction, whatever it defines interferes with every other name
// live just after it (its live-out). One exception: a copy "t = s" does not
// make t interfere with s, because right after the copy both hold the same
// value and may share one register.
//   0: a = load p
//   1: c = a          (a copy)
//   2: b = a + 1
//   3: d = b * c      (d is never read: a dead definition)
//   4: e = c + b
//   5: return e
// The program compares the rule with a tempting shortcut, "two names
// interfere when both are live-in at the same instruction", which gets
// both special cases wrong.
//
// Follows: Pfenning and Platzer, CMU 15-411 lecture 3, "Register
// Allocation", section 2, and lecture 4, "Liveness Analysis", section 4.

#include <print>
#include <set>
#include <string>
#include <utility>
#include <vector>

struct Instr {
  std::string text, uses, def;  // one letter per name; def may be empty
  bool is_copy;
};

int main() {
  const std::vector<Instr> code = {
      {"a = load p", "p", "a", false}, {"c = a", "a", "c", true},
      {"b = a + 1", "a", "b", false},  {"d = b * c", "bc", "d", false},
      {"e = c + b", "cb", "e", false}, {"return e", "e", "", false},
  };
  const int n = static_cast<int>(code.size());

  std::vector<std::set<char>> in(n), out(n);
  for (int k = n - 1; k >= 0; --k) {  // straight line: one backward pass
    if (k + 1 < n) out[k] = in[k + 1];
    in[k] = out[k];
    for (char d : code[k].def) in[k].erase(d);
    for (char u : code[k].uses) in[k].insert(u);
  }

  std::set<std::pair<char, char>> rule, shortcut;
  auto edge = [](std::set<std::pair<char, char>> &g, char x, char y) {
    if (x != y) g.insert(x < y ? std::pair{x, y} : std::pair{y, x});
  };
  for (int k = 0; k < n; ++k) {
    for (char d : code[k].def)
      for (char v : out[k])
        if (!(code[k].is_copy && v == code[k].uses[0])) edge(rule, d, v);
    for (char x : in[k])
      for (char y : in[k]) edge(shortcut, x, y);
  }

  auto show = [](const std::set<std::pair<char, char>> &g) {
    std::string s;
    for (auto [x, y] : g) s += std::string(s.empty() ? "" : " ") + x + '-' + y;
    return s;
  };
  auto names = [](const std::set<char> &s) { return s.empty() ? std::string("-") : std::string(s.begin(), s.end()); };

  std::println("instr  code        live-in  live-out");
  for (int k = 0; k < n; ++k)
    std::println("{:5}  {:<10}  {:<7}  {}", k, code[k].text, names(in[k]), names(out[k]));
  std::println("edges by the definition rule: {}", show(rule));
  std::println("edges by the live-in shortcut: {}", show(shortcut));
}

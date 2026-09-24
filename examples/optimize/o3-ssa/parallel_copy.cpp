// The copies that replace the phis on one edge form a parallel copy: every
// source is read before any destination is written, as the phis read their
// operands. Emitted one after another in the listed order, they can go wrong;
// a swap is the smallest case.
//
// To sequentialize a parallel copy, emit a copy whose destination no pending
// copy still reads. When no such copy is left, every pending copy lies on a
// cycle: copy one source into the spare register t, and let the copy that read
// it read t instead. One spare register is enough.
//
// Follows: SSA book draft (Rastello and Bouchez Tichadou, eds., 8 June 2018),
// section 3.2, algorithm 3.6; Rideau, Serpette and Leroy, "Tilting at
// windmills with Coq", Journal of Automated Reasoning, 2008, sections 1 to 3.

#include <algorithm>
#include <map>
#include <print>
#include <string>
#include <utility>
#include <vector>

using Copy = std::pair<char, char>;  // (destination, source)
using Registers = std::map<char, int>;

std::vector<Copy> sequentialize(std::vector<Copy> pending) {
  std::erase_if(pending, [](const Copy &c) { return c.first == c.second; });
  std::vector<Copy> out;
  while (!pending.empty()) {
    auto free = std::ranges::find_if(pending, [&](const Copy &c) {
      return std::ranges::none_of(pending, [&](const Copy &d) { return d.second == c.first; });
    });
    if (free != pending.end()) {
      out.push_back(*free);
      pending.erase(free);
    } else {  // only cycles are left: break one of them
      out.push_back({'t', pending.front().second});
      pending.front().second = 't';
    }
  }
  return out;
}

Registers one_by_one(Registers r, const std::vector<Copy> &copies) {
  for (const auto &[dst, src] : copies) r[dst] = r[src];
  return r;
}

Registers all_at_once(Registers r, const std::vector<Copy> &copies) {
  const Registers before = r;
  for (const auto &[dst, src] : copies) r[dst] = before.at(src);
  return r;
}

std::string show(const std::vector<Copy> &copies) {  // as a sequence
  std::string s;
  for (const auto &[dst, src] : copies) s += std::string(s.empty() ? "" : "; ") + dst + " = " + src;
  return s;
}

std::string show_parallel(const std::vector<Copy> &copies) {  // (a, b) := (b, a)
  std::string dsts, srcs;
  for (const auto &[dst, src] : copies) {
    dsts += std::string(dsts.empty() ? "" : ", ") + dst;
    srcs += std::string(srcs.empty() ? "" : ", ") + src;
  }
  return "(" + dsts + ") := (" + srcs + ")";
}

int main() {
  const Registers start = {{'a', 1}, {'b', 2}, {'c', 3}, {'d', 4}};
  const std::vector<std::vector<Copy>> tests = {
      {{'a', 'b'}, {'b', 'a'}},                          // a swap
      {{'a', 'b'}, {'b', 'c'}, {'c', 'a'}, {'d', 'a'}},  // a cycle, and d hanging off a
  };
  for (const std::vector<Copy> &copies : tests) {
    const auto values = [&](const Registers &r) {
      std::string s;
      for (const auto &[dst, src] : copies)
        s += std::string(s.empty() ? "" : " ") + dst + "=" + std::to_string(r.at(dst));
      return s;
    };
    const std::vector<Copy> sequence = sequentialize(copies);
    std::println("{}", show_parallel(copies));
    std::println("  meaning         {:<34}  {}", "", values(all_at_once(start, copies)));
    std::println("  in listed order {:<34}  {}", show(copies), values(one_by_one(start, copies)));
    std::println("  sequentialized  {:<34}  {}", show(sequence), values(one_by_one(start, sequence)));
  }
}

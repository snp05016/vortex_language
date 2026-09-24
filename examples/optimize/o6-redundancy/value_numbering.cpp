// Local value numbering over one basic block of a toy three-address language.
// Every value gets a number. An operation is keyed by its operator and the
// numbers of its operands, so two operations with the same key compute the
// same value. + and * are commutative, so their two operand numbers are put in
// order first, and b + a gets the key of a + b. Nothing else is assumed:
// (a + b) + c and a + (b + c) keep different numbers, because for f32, and for
// checked integers whose overflow checks can fail differently, they are not
// the same computation.
//
// A name that is assigned again stops holding its old value. A repeated
// operation can reuse an earlier result only while some name still holds it.
// In SSA form every name is assigned once, and that question disappears.
//
// Follows: Alpern, Wegman and Zadeck, POPL 1988, section 1 (value numbering in
// basic blocks); LLVM's GVN.cpp, release/18.x (sorting commutative operands).

#include <format>
#include <map>
#include <print>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

struct Stmt { std::string dest, lhs; char op; std::string rhs; };  // op 0: dest = lhs

int main() {
  const std::vector<Stmt> block = {
      {"t1", "a", '+', "b"},  {"t2", "b", '+', "a"}, {"t3", "t1", '*', "c"},
      {"a", "4", 0, ""},      {"t4", "a", '+', "b"}, {"t5", "t2", '*', "c"},
      {"x", "b", '*', "c"},   {"x", "1", 0, ""},     {"y", "c", '*', "b"},
      {"t6", "t4", '+', "c"}, {"t7", "b", '+', "c"}, {"t8", "a", '+', "t7"},
  };
  std::map<std::string, int> number;                // name or literal -> value number
  std::map<std::tuple<char, int, int>, int> table;  // (operator, left, right) -> number
  std::map<int, std::set<std::string>> holders;     // value number -> names holding it
  int next = 0, operations = 0, removed = 0;

  // A name read before the block writes it, or a literal, gets a fresh number.
  auto value_of = [&](const std::string& operand) {
    auto [it, fresh] = number.try_emplace(operand, next);
    if (fresh) ++next, holders[it->second].insert(operand);
    return it->second;
  };
  for (const Stmt& s : block) {
    std::string text = s.dest + " = " + s.lhs, note;
    int v = 0;
    if (s.op == 0) {
      v = value_of(s.lhs);
    } else {
      ++operations;
      text += std::string(" ") + s.op + " " + s.rhs;
      int l = value_of(s.lhs), r = value_of(s.rhs);
      if (l > r) std::swap(l, r);  // both operators here are commutative
      const auto key = std::tuple(s.op, l, r);
      if (auto hit = table.find(key); hit == table.end()) {
        v = table[key] = next++;
      } else if (v = hit->second; holders[v].empty()) {
        note = "no name holds v" + std::to_string(v) + " now: computed again";
      } else {
        note = "redundant: " + s.dest + " = " + *holders[v].begin();
        ++removed;
      }
    }
    if (auto old = number.find(s.dest); old != number.end()) holders[old->second].erase(s.dest);
    number[s.dest] = v;
    holders[v].insert(s.dest);
    const std::string line = std::format("{:<13} v{}", text, v);
    std::println("{}", note.empty() ? line : std::format("{:<18} {}", line, note));
  }
  std::println("removed {} of {} operations", removed, operations);
}

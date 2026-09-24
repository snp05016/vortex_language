// Constant propagation on a toy SSA function, run twice with one solver: once
// letting every edge carry values (Wegman and Zadeck's sparse simple constant
// algorithm, SSC), once letting only edges proven executable carry them (their
// sparse conditional constant algorithm, SCC). The function is a lookup helper
// after its caller was inlined with the argument from_end = false:
//   A:  index0 = 0;               if from_end goto B else C
//   B:  index1 = 63;              goto C
//   C:  index2 = phi(A: index0, B: index1)
//       ok = index2 < 64;         if ok goto D else R      (a bounds check)
//   D:  x = load values[index2];  return x
//   R:  report the bounds error
// Facts come from the flat lattice: no value yet, a constant (1 is true, 0 is
// false), or "top", not a constant. A load is top: memory is not tracked.
//
// Follows: Wegman and Zadeck, "Constant Propagation with Conditional
// Branches" (1991), sections 3.2 and 3.4.

#include <deque>
#include <print>
#include <set>
#include <string>
#include <utility>
#include <vector>

enum class Op { constant, phi, less, load, branch, jump, stop };  // stop: return or report

struct Inst {
  Op op;
  int block;
  std::string name;         // empty when the instruction defines no value
  std::vector<int> args{};  // operands, as instruction numbers
  long k = 0;               // the constant, or the right side of a comparison
  std::vector<int> from{};  // for a phi: the predecessor block of each operand
};

struct Fact {
  enum Kind { none, constant, top } kind = none;
  long k = 0;
  bool operator==(const Fact&) const = default;
};

Fact join(Fact a, Fact b) {
  if (a.kind == Fact::none) return b;
  if (b.kind == Fact::none || a == b) return a;
  return {Fact::top};
}

const std::string block_name = "ABCDR";
const std::vector<std::vector<int>> succ{{1, 2}, {2}, {3, 4}, {}, {}};  // a branch: {if true, if false}
const std::vector<Inst> insts{
    {Op::constant, 0, "from_end", {}, 0}, {Op::constant, 0, "index0", {}, 0}, {Op::branch, 0, "", {0}},
    {Op::constant, 1, "index1", {}, 63}, {Op::jump, 1, ""},
    {Op::phi, 2, "index2", {1, 3}, 0, {0, 1}}, {Op::less, 2, "ok", {5}, 64}, {Op::branch, 2, "", {6}},
    {Op::load, 3, "x", {5}}, {Op::stop, 3, "", {8}}, {Op::stop, 4, ""}};

struct Solver {
  bool conditional;  // false: every edge carries values, as in SSC
  std::vector<Fact> fact = std::vector<Fact>(insts.size());
  std::set<std::pair<int, int>> executable{};     // edges; block -1 is the start
  std::vector<bool> reached = std::vector<bool>(succ.size());
  std::deque<std::pair<int, int>> flow{{-1, 0}};  // edges newly found executable
  std::deque<int> ssa{};                          // uses whose operand changed

  void visit(int i) {
    const Inst& in = insts[i];
    const Fact a = in.args.empty() ? Fact{} : fact[in.args[0]];
    Fact f;
    if (in.op == Op::constant) f = {Fact::constant, in.k};
    if (in.op == Op::load) f = {Fact::top};
    if (in.op == Op::less) f = a.kind == Fact::constant ? Fact{Fact::constant, a.k < in.k} : a;
    if (in.op == Op::phi)  // only operands that arrive along an executable edge count
      for (size_t j = 0; j < in.args.size(); ++j)
        if (executable.contains({in.from[j], in.block})) f = join(f, fact[in.args[j]]);
    if (in.op == Op::jump) flow.push_back({in.block, succ[in.block][0]});
    if (in.op == Op::branch) {  // a constant condition selects one edge; top selects both
      const bool any = !conditional || a.kind == Fact::top, known = a.kind == Fact::constant;
      if (any || (known && a.k)) flow.push_back({in.block, succ[in.block][0]});
      if (any || (known && !a.k)) flow.push_back({in.block, succ[in.block][1]});
    }
    if (in.name.empty() || f == fact[i]) return;
    fact[i] = f;
    for (int u = 0; u < int(insts.size()); ++u)
      for (int arg : insts[u].args) if (arg == i) ssa.push_back(u);
  }

  void run() {
    while (!flow.empty() || !ssa.empty()) {
      if (!flow.empty()) {
        const auto [from, to] = flow.front();
        flow.pop_front();
        if (!executable.insert({from, to}).second) continue;
        const bool first = !reached[to];
        reached[to] = true;
        for (int i = 0; i < int(insts.size()); ++i)  // phis on every new edge, the rest once
          if (insts[i].block == to && (first || insts[i].op == Op::phi)) visit(i);
      } else {
        const int i = ssa.front();
        ssa.pop_front();
        if (reached[insts[i].block]) visit(i);
      }
    }
  }
};

std::string show(Fact f) {
  if (f.kind == Fact::none) return "no value";
  return f.kind == Fact::top ? "top" : std::to_string(f.k);
}

int main() {
  Solver every{false}, only{true};
  every.run();
  only.run();
  std::println("{:<10}{:<14}{}", "value", "every edge", "executable edges");
  for (size_t i = 0; i < insts.size(); ++i)
    if (!insts[i].name.empty())
      std::println("{:<10}{:<14}{}", insts[i].name, show(every.fact[i]), show(only.fact[i]));
  for (const Solver* s : {&every, &only}) {
    std::string edges, unreached;
    for (auto [from, to] : s->executable)
      edges += (from < 0 ? " start" : " " + block_name.substr(from, 1)) + "->" + block_name[to];
    for (size_t b = 0; b < succ.size(); ++b)
      if (!s->reached[b]) unreached += " " + block_name.substr(b, 1);
    std::println("\n{}:\n  edges used:{}\n  never reached:{}", s->conditional ? "executable edges" : "every edge",
                 edges, unreached.empty() ? " none" : unreached);
  }
}

// Two dead-code eliminations on one toy SSA function with unchecked integers:
//
//   entry: s0 = 0; t0 = 0; i0 = 0;                goto head
//   head:  i1 = phi(i0, i2); s1 = phi(s0, s2); t1 = phi(t0, t2)
//          c = i1 < 10;                           if c goto body else exit
//   body:  s2 = s1 + i1; t2 = t1 + 1; i2 = i1 + 1; goto head
//   exit:  big = s1 > 100;                        if big goto note else done
//   note:  msg = s1 * 2;                          goto done
//   done:  print(s1); return
//
// Use counts delete an instruction that defines a value nobody uses, then look
// again at its operands. Marking starts from the instructions with an effect
// (print, return), marks the values they use, and marks each branch that
// decides whether a live instruction runs (its control dependences, given here
// as data). Whatever is left unmarked is deleted; a dead branch becomes a jump
// to its block's immediate post-dominator. Jumps are kept as they are.
//
// Follows: Cytron et al. (1991), section 7.1 and figure 17; LLVM's pass list
// (dce, adce).

#include <print>
#include <string>
#include <vector>

enum class Kind { value, branch, jump, effect };

struct Inst {
  std::string text;
  int block;
  std::vector<int> args{};  // the instructions whose values this one reads
  Kind kind = Kind::value;
  std::string post_dominator{};  // for a branch: where a jump would go instead
};

const std::vector<Inst> insts{
    {"s0 = 0", 0}, {"t0 = 0", 0}, {"i0 = 0", 0}, {"goto head", 0, {}, Kind::jump},
    {"i1 = phi(i0, i2)", 1, {2, 11}}, {"s1 = phi(s0, s2)", 1, {0, 9}}, {"t1 = phi(t0, t2)", 1, {1, 10}},
    {"c = i1 < 10", 1, {4}}, {"if c goto body else exit", 1, {7}, Kind::branch, "exit"},
    {"s2 = s1 + i1", 2, {5, 4}}, {"t2 = t1 + 1", 2, {6}}, {"i2 = i1 + 1", 2, {4}},
    {"goto head", 2, {}, Kind::jump},
    {"big = s1 > 100", 3, {5}}, {"if big goto note else done", 3, {13}, Kind::branch, "done"},
    {"msg = s1 * 2", 4, {5}}, {"goto done", 4, {}, Kind::jump},
    {"print(s1)", 5, {5}, Kind::effect}, {"return", 5, {}, Kind::effect}};

// For each block (entry, head, body, exit, note, done): the blocks whose
// branch decides whether it runs, from the post-dominance frontier.
const std::vector<std::vector<int>> control_deps{{}, {1}, {1}, {}, {3}, {}};

std::vector<bool> by_use_counts() {
  std::vector<int> uses(insts.size());
  for (const Inst& in : insts)
    for (int a : in.args) ++uses[a];
  std::vector<bool> deleted(insts.size());
  std::vector<int> work;
  for (size_t i = 0; i < insts.size(); ++i)
    if (insts[i].kind == Kind::value && uses[i] == 0) work.push_back(int(i));
  while (!work.empty()) {
    const int i = work.back();
    work.pop_back();
    deleted[i] = true;
    for (int a : insts[i].args)  // an operand may have lost its last use
      if (--uses[a] == 0 && insts[a].kind == Kind::value) work.push_back(a);
  }
  return deleted;
}

std::vector<bool> by_marking() {
  std::vector<bool> live(insts.size());
  std::vector<int> work;
  auto mark = [&](int i) {
    if (live[i]) return;
    live[i] = true;
    work.push_back(i);
  };
  for (size_t i = 0; i < insts.size(); ++i)
    if (insts[i].kind == Kind::effect) mark(int(i));
  while (!work.empty()) {
    const int i = work.back();
    work.pop_back();
    for (int a : insts[i].args) mark(a);
    for (int b : control_deps[insts[i].block])  // the branch that ends block b
      for (size_t j = 0; j < insts.size(); ++j)
        if (insts[j].block == b && insts[j].kind == Kind::branch) mark(int(j));
  }
  std::vector<bool> deleted(insts.size());
  for (size_t i = 0; i < insts.size(); ++i) deleted[i] = !live[i] && insts[i].kind != Kind::jump;
  return deleted;
}

int main() {
  const std::vector<bool> counted = by_use_counts(), marked = by_marking();
  std::println("{:<28}{:<13}{}", "instruction", "use counts", "marking");
  for (size_t i = 0; i < insts.size(); ++i) {
    if (insts[i].kind == Kind::jump) continue;
    std::string after = marked[i] ? "deleted" : "kept";
    if (marked[i] && insts[i].kind == Kind::branch) after = "goto " + insts[i].post_dominator;
    std::println("{:<28}{:<13}{}", insts[i].text, counted[i] ? "deleted" : "kept", after);
  }
}

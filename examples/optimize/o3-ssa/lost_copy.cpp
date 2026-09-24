// The lost-copy problem. This SSA form comes from a loop that remembers the
// value it had before its last step, after copy propagation removed the copy
// that used to hold it:
//
//     entry: x0 = 1                               then go to loop
//     loop:  x1 = phi(entry: x0, loop: x2)
//            x2 = x1 + 1
//            if x2 < 4 go to loop, else go to done
//     done:  print x1
//
// It prints 3. The edge loop -> loop is critical: loop has two successors and
// two predecessors. Replacing the phi by a copy at the end of each predecessor
// puts "x1 = x2" before the branch, so it also runs on the way out, and done
// prints an x1 that was already overwritten. Splitting the edge, or giving the
// phi's result its own copy (isolating the phi), keeps the right value.
//
// Each lowering is a program for a tiny machine: x0, x1 and x2 live in
// registers a, b and c, and d is a spare.
//
// Follows: SSA book draft (Rastello and Bouchez Tichadou, eds., 8 June 2018),
// sections 3.2 and 21.1; LLVM 18, PHIElimination.cpp.

#include <array>
#include <print>
#include <vector>

enum Op { Set, Copy, AddOne, JumpIfBelow, Jump, Print };
struct Insn {
  Op op;
  char dst = 'a', src = 'a';
  int value = 0, target = 0;
};

void run(const char *lowering, const std::vector<Insn> &program) {
  std::array<int, 4> reg{};
  auto r = [&](char name) -> int & { return reg[name - 'a']; };
  for (std::size_t pc = 0; pc < program.size();) {
    const Insn &i = program[pc++];
    switch (i.op) {
      case Set: r(i.dst) = i.value; break;
      case Copy: r(i.dst) = r(i.src); break;
      case AddOne: r(i.dst) = r(i.src) + 1; break;
      case JumpIfBelow: if (r(i.src) < i.value) pc = i.target; break;
      case Jump: pc = i.target; break;
      case Print: std::println("{:<42} prints {}", lowering, r(i.src)); break;
    }
  }
}

int main() {
  run("copies at the end of each predecessor", {
      {.op = Set, .dst = 'a', .value = 1},                        // 0 entry: x0 = 1
      {.op = Copy, .dst = 'b', .src = 'a'},                       // 1        x1 = x0
      {.op = AddOne, .dst = 'c', .src = 'b'},                     // 2 loop:  x2 = x1 + 1
      {.op = Copy, .dst = 'b', .src = 'c'},                       // 3        x1 = x2, on both edges
      {.op = JumpIfBelow, .src = 'c', .value = 4, .target = 2},   // 4
      {.op = Print, .src = 'b'}});                                // 5 done:  print x1

  run("copy in a new block on the split edge", {
      {.op = Set, .dst = 'a', .value = 1},                        // 0 entry
      {.op = Copy, .dst = 'b', .src = 'a'},                       // 1
      {.op = AddOne, .dst = 'c', .src = 'b'},                     // 2 loop
      {.op = JumpIfBelow, .src = 'c', .value = 4, .target = 5},   // 3
      {.op = Jump, .target = 7},                                  // 4 leave for done
      {.op = Copy, .dst = 'b', .src = 'c'},                       // 5 the new block: x1 = x2
      {.op = Jump, .target = 2},                                  // 6
      {.op = Print, .src = 'b'}});                                // 7 done

  run("phi isolated: predecessors write d", {
      {.op = Set, .dst = 'a', .value = 1},                        // 0 entry
      {.op = Copy, .dst = 'd', .src = 'a'},                       // 1        d = x0
      {.op = Copy, .dst = 'b', .src = 'd'},                       // 2 loop:  x1 = d
      {.op = AddOne, .dst = 'c', .src = 'b'},                     // 3        x2 = x1 + 1
      {.op = Copy, .dst = 'd', .src = 'c'},                       // 4        d = x2
      {.op = JumpIfBelow, .src = 'c', .value = 4, .target = 2},   // 5
      {.op = Print, .src = 'b'}});                                // 6 done
}

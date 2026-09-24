// A one-pass, backward lowering, in the style Cranelift's instruction
// selector blog post describes: use counts are computed once, then a single
// pass from the end of the program to its start decides, at each
// instruction, whether the value it produces was folded into whatever
// consumed it. A value can only fold into its single consumer: if two
// instructions read it, it must be computed once and kept around.
//
// Different problem from the Vortex exercise: a four-instruction toy IR
// (input, shl, add, load), not Vortex's own address computation.
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

struct Instr {
  int id;
  std::string op;         // "input", "shl", "add", "load"
  std::vector<int> args;  // operand ids, in def order
  long imm = 0;            // shift amount, for "shl"
};

// Counts how many times each instruction's result is read as an operand
// elsewhere in the program. Computed once, ahead of the lowering pass,
// exactly as Cranelift's isel notes describe.
std::map<int, int> use_counts(const std::vector<Instr> &prog) {
  std::map<int, int> counts;
  for (const auto &ins : prog)
    for (int a : ins.args) counts[a]++;
  return counts;
}

const Instr &find(const std::vector<Instr> &prog, int id) {
  for (const auto &ins : prog)
    if (ins.id == id) return ins;
  throw std::runtime_error("no such id");
}

// One backward pass over the program. Every "load" is a root (it may read
// memory, so it is always kept). Walking from the last instruction to the
// first lets the pass decide about an instruction's producer before it
// reaches that producer, which is what makes folding a single pass instead
// of a separate later cleanup.
std::vector<std::string> lower(const std::vector<Instr> &prog) {
  auto counts = use_counts(prog);
  std::set<int> folded_away;
  std::map<int, std::string> lines; // keyed by id, printed in program order

  for (auto it = prog.rbegin(); it != prog.rend(); ++it) {
    const Instr &ins = *it;
    if (folded_away.count(ins.id)) continue; // consumed by a later fold

    if (ins.op == "load") {
      const Instr &addr = find(prog, ins.args[0]);
      bool fused = false;
      if (addr.op == "add" && counts[addr.id] == 1) {
        for (int operand_id : addr.args) {
          const Instr &operand = find(prog, operand_id);
          if (operand.op == "shl" && counts[operand.id] == 1) {
            int base_id = (operand_id == addr.args[0]) ? addr.args[1]
                                                         : addr.args[0];
            lines[ins.id] = "v" + std::to_string(ins.id) + " = load [v" +
                             std::to_string(base_id) + " + v" +
                             std::to_string(operand.args[0]) + " << " +
                             std::to_string(operand.imm) + "]";
            folded_away.insert(addr.id);
            folded_away.insert(operand.id);
            fused = true;
            break;
          }
        }
      }
      if (!fused)
        lines[ins.id] = "v" + std::to_string(ins.id) + " = load [v" +
                         std::to_string(addr.id) + "]";
    } else if (ins.op == "shl") {
      lines[ins.id] = "v" + std::to_string(ins.id) + " = v" +
                       std::to_string(ins.args[0]) + " << " +
                       std::to_string(ins.imm);
    } else if (ins.op == "add") {
      lines[ins.id] = "v" + std::to_string(ins.id) + " = v" +
                       std::to_string(ins.args[0]) + " + v" +
                       std::to_string(ins.args[1]);
    } else { // "input"
      lines[ins.id] = "v" + std::to_string(ins.id) + " = input";
    }
  }

  std::vector<std::string> out;
  for (const auto &ins : prog) {
    auto found = lines.find(ins.id);
    if (found != lines.end()) out.push_back(found->second);
  }
  return out;
}

void run(const std::string &title, const std::vector<Instr> &prog) {
  std::cout << title << ":\n";
  for (const auto &line : lower(prog)) std::cout << "  " << line << "\n";
}

} // namespace

int main() {
  // v0 = base, v1 = index, v2 = index << 2, v3 = base + v2, v4 = load [v3].
  // v2 and v3 are each read exactly once, by the next instruction, so both
  // fold into a single addressing mode on the load.
  std::vector<Instr> single_use = {
      {0, "input", {}, 0}, {1, "input", {}, 0}, {2, "shl", {1}, 2},
      {3, "add", {0, 2}, 0}, {4, "load", {3}, 0}};
  run("index used once", single_use);

  // Same shape, but v2 (the shift) is also read directly by a second load.
  // Its use count is 2, so it can no longer fold into the first load's
  // address: it has to be materialized once and shared.
  std::vector<Instr> shared_use = {
      {0, "input", {}, 0},   {1, "input", {}, 0}, {2, "shl", {1}, 2},
      {3, "add", {0, 2}, 0}, {4, "load", {3}, 0}, {5, "load", {2}, 0}};
  run("index used twice", shared_use);
  return 0;
}

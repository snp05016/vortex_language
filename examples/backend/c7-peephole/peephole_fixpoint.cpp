// A tiny peephole optimizer: four rewrite rules that look at one or two
// adjacent instructions, applied to a fixpoint (repeated over the whole
// list until no rule matches anywhere). Real peephole passes work the same
// way: slide a short window along the code, match on decoded operands
// instead of text, and rewrite when a rule fires. The instructions here are
// operand lists rather than parsed assembly, which is enough to show the
// matching and the fixpoint without writing a parser.
//
// Follows: McKeeman, "Peephole optimization", CACM 1965 (the sliding
// window and repeated-pass idea); Davidson and Fraser, "The Design and
// Application of a Retargetable Peephole Optimizer", TOPLAS 1980 (rules
// keyed on a pair of adjacent instructions).

#include <cstdio>
#include <string>
#include <vector>

struct Instr {
    std::string op;                  // "mov", "mul", "lsl", "str", "ldr", "stp"
    std::vector<std::string> args;   // meaning depends on op, see print()
};

void print(const Instr& in) {
    std::printf("  %s", in.op.c_str());
    for (std::size_t i = 0; i < in.args.size(); ++i) {
        std::printf("%s%s", i == 0 ? " " : ", ", in.args[i].c_str());
    }
    std::printf("\n");
}

void print_program(const char* label, const std::vector<Instr>& prog) {
    std::printf("%s (%zu instructions):\n", label, prog.size());
    for (const auto& in : prog) print(in);
}

// Rule 1: a move whose source and destination are the same register does
// nothing. One instruction, no neighbor to check.
bool rule_self_move(std::vector<Instr>& prog, std::size_t i) {
    const Instr& a = prog[i];
    if (a.op != "mov" || a.args[0] != a.args[1]) return false;
    std::printf("  removed a self-move (%s, %s)\n", a.args[0].c_str(), a.args[1].c_str());
    prog.erase(prog.begin() + static_cast<long>(i));
    return true;
}

// Rule 2: `mul Rd, Rn, #k` where k is a power of two becomes a shift. A
// shifter is narrower and simpler than a multiplier, so a code generator
// that never tried to avoid `mul` leaves this on the table.
bool rule_strength_reduce(std::vector<Instr>& prog, std::size_t i) {
    Instr& a = prog[i];
    if (a.op != "mul") return false;
    int k = std::stoi(a.args[2]);
    int shift = 0;
    int v = k;
    while (v > 1 && v % 2 == 0) {
        v /= 2;
        ++shift;
    }
    if (v != 1) return false;  // not a power of two: the rule does not apply
    std::printf("  strength-reduced a multiply by %d into a shift by %d\n", k, shift);
    a = Instr{"lsl", {a.args[0], a.args[1], std::to_string(shift)}};
    return true;
}

// Rule 3: a store immediately followed by a load from the same address.
// Nothing could have changed the register or the memory in between, because
// the window is exactly these two instructions. If the load names the
// register the store just used, it is dropped outright; otherwise the
// value is already sitting in a register, so the load becomes a move.
bool rule_store_then_load(std::vector<Instr>& prog, std::size_t i) {
    if (i + 1 >= prog.size()) return false;
    const Instr& a = prog[i];
    Instr& b = prog[i + 1];
    if (a.op != "str" || b.op != "ldr") return false;
    if (a.args[1] != b.args[1] || a.args[2] != b.args[2]) return false;  // different address
    if (a.args[0] == b.args[0]) {
        std::printf("  dropped a redundant reload of %s\n", a.args[0].c_str());
        prog.erase(prog.begin() + static_cast<long>(i) + 1);
    } else {
        std::printf("  replaced a reload with a register move\n");
        b = Instr{"mov", {b.args[0], a.args[0]}};
    }
    return true;
}

// Rule 4: two adjacent stores to consecutive 4-byte offsets of the same
// base register become one paired store. AArch64 has `stp` natively; this
// rule is why a code generator prefers it to two plain `str`.
bool rule_pair_stores(std::vector<Instr>& prog, std::size_t i) {
    if (i + 1 >= prog.size()) return false;
    const Instr& a = prog[i];
    const Instr& b = prog[i + 1];
    if (a.op != "str" || b.op != "str") return false;
    if (a.args[1] != b.args[1]) return false;  // different base register
    if (std::stoi(b.args[2]) != std::stoi(a.args[2]) + 4) return false;  // not consecutive
    std::printf("  paired two stores into one stp\n");
    Instr merged{"stp", {a.args[0], b.args[0], a.args[1], a.args[2]}};
    prog[i] = merged;
    prog.erase(prog.begin() + static_cast<long>(i) + 1);
    return true;
}

// One step of the fixpoint: try every rule at every position and stop at
// the first match. The caller loops until a full scan finds nothing left to
// do; restarting the scan after each rewrite is the simplest way to stay
// correct once a rewrite has shortened the list.
bool try_one_rewrite(std::vector<Instr>& prog) {
    for (std::size_t i = 0; i < prog.size(); ++i) {
        if (rule_self_move(prog, i)) return true;
        if (rule_strength_reduce(prog, i)) return true;
        if (rule_store_then_load(prog, i)) return true;
        if (rule_pair_stores(prog, i)) return true;
    }
    return false;
}

int main() {
    // Straight-line code a naive instruction selector or a spill-everything
    // allocator might emit: a self-move left over from failed coalescing, an
    // array index scaled by a power-of-two element size, and three stores of
    // which one is immediately reloaded.
    std::vector<Instr> prog = {
        {"mov", {"x1", "x1"}},
        {"mul", {"x2", "x3", "8"}},
        {"str", {"s0", "x0", "0"}},
        {"str", {"s1", "x0", "4"}},
        {"str", {"s2", "x0", "8"}},
        {"ldr", {"s2", "x0", "8"}},
    };

    print_program("Before", prog);
    std::printf("Rewrites:\n");
    while (try_one_rewrite(prog)) {
    }
    print_program("After", prog);
    return 0;
}

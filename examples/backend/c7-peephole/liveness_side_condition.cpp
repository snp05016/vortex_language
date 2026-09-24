// A peephole rule must check that nothing later reads what a rewrite would
// remove or change. This program models that check directly: given a
// straight-line list of instructions and a candidate to delete, it asks
// whether the register (or flag) the candidate defines is live immediately
// afterward, meaning some later instruction reads it before anything
// redefines it. A real back end computes liveness once per pass and looks
// it up; this is the one-instruction question every peephole rule is
// secretly asking before it touches anything.
//
// Follows: Davidson and Fraser, "The Design and Application of a
// Retargetable Peephole Optimizer", TOPLAS 1980 (side conditions attached
// to a rewrite rule).

#include <cstdio>
#include <string>
#include <vector>

struct Instr {
    std::string op;
    std::vector<std::string> defs;   // registers or flags this instruction writes
    std::vector<std::string> uses;   // registers or flags this instruction reads
};

// Is `reg` live immediately after instruction `at`? Walk forward: a use
// before any redefinition means yes; a redefinition with no use before it
// means the old value was already dead.
bool live_after(const std::vector<Instr>& prog, std::size_t at, const std::string& reg) {
    for (std::size_t i = at + 1; i < prog.size(); ++i) {
        for (const auto& u : prog[i].uses) {
            if (u == reg) return true;
        }
        for (const auto& d : prog[i].defs) {
            if (d == reg) return false;
        }
    }
    return false;  // never used again before the program ends
}

void try_delete(const std::vector<Instr>& prog, std::size_t at) {
    const Instr& in = prog[at];
    std::printf("candidate: %s\n", in.op.c_str());
    for (const auto& reg : in.defs) {
        if (live_after(prog, at, reg)) {
            std::printf("  refused: %s is still live\n", reg.c_str());
            return;
        }
    }
    std::printf("  safe to delete: none of its results are live\n");
}

int main() {
    // Program A: a compare feeds a branch, so its flag result is live.
    std::vector<Instr> guarded = {
        {"cmp", {"nzcv"}, {"x0"}},
        {"mov", {"x1"}, {"x2"}},
        {"b.eq", {}, {"nzcv"}},
    };
    std::printf("Program A: the branch reads the compare's flag\n");
    try_delete(guarded, 0);  // the cmp
    try_delete(guarded, 1);  // the mov: x1 is never read again

    // Program B: the same compare, but nothing after it reads the flag.
    std::vector<Instr> unguarded = {
        {"cmp", {"nzcv"}, {"x0"}},
        {"mov", {"x1"}, {"x2"}},
        {"mov", {"x3"}, {"x1"}},
    };
    std::printf("Program B: nothing reads the flag afterward\n");
    try_delete(unguarded, 0);  // the cmp

    return 0;
}

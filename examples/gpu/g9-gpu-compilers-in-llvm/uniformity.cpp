// Follows: LLVM Project, "Convergence And Uniformity".
// https://llvm.org/docs/ConvergenceAndUniformity.html
//
// A minimal uniformity analysis on a fixed four-block CFG:
//   entry: tid (seed), base (constant), cond = f(tid), branch cond -> then/else
//   then:  a_then = f(base)
//   else:  a_else = f(base)
//   merge: r = phi(a_then, a_else)
// Two rules decide whether a value is divergent, meaning it can differ
// across the lanes of one warp: (1) it reads an operand that is already
// divergent, or (2) it is defined in a block that only some of a divergent
// branch's lanes reach, whatever its operands are. The phi at merge needs no
// third rule: it simply reads both arms as operands, so rule 1 already makes
// it divergent once rule 2 has marked them.

#include <array>
#include <cstddef>
#include <cstdio>

struct Block {
    const char* name;
    bool branch_arm;  // true: only some lanes of a divergent branch reach here
};

struct Value {
    const char* name;
    int block;               // index into blocks
    std::array<int, 2> ops;  // operand indices into values; -1 = seed or constant
};

int main() {
    constexpr std::array<Block, 4> blocks{{
        {"entry", false}, {"then", true}, {"else", true}, {"merge", false},
    }};
    // index: 0 tid, 1 base, 2 cond, 3 a_then, 4 a_else, 5 r
    constexpr std::array<Value, 6> values{{
        {"tid",    0, {-1, -1}},
        {"base",   0, {-1, -1}},
        {"cond",   0, {0, -1}},
        {"a_then", 1, {1, -1}},
        {"a_else", 2, {1, -1}},
        {"r",      3, {3, 4}},
    }};

    std::array<bool, 6> divergent{};
    divergent[0] = true;  // tid: a thread's own id, the one seed

    for (std::size_t i = 0; i < values.size(); ++i) {
        bool d = divergent[i];
        for (int op : values[i].ops)
            if (op >= 0 && divergent[static_cast<std::size_t>(op)])
                d = true;  // rule 1: divergent operand
        if (blocks[static_cast<std::size_t>(values[i].block)].branch_arm)
            d = true;      // rule 2: block only some lanes reach
        divergent[i] = d;
    }

    for (std::size_t i = 0; i < values.size(); ++i)
        std::printf("%-7s%-7s%s\n", values[i].name,
                    blocks[static_cast<std::size_t>(values[i].block)].name,
                    divergent[i] ? "divergent" : "uniform");
}

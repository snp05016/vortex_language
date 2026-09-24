// A brute-force superoptimizer: search every program up to a length limit,
// over a tiny two-kind instruction set on an 8-bit, three-register machine,
// and keep the first one whose result matches x * 10 (mod 256) for every
// one of the 256 possible 8-bit inputs. This is superoptimization taken to
// its most literal form: instead of writing a strength-reduction rule by
// hand, search for one and prove it correct by exhaustive testing, rather
// than by algebra.
//
// Follows: Massalin, "Superoptimizer: A Look at the Smallest Program",
// ASPLOS 1987 (exhaustive search over short programs); Bansal and Aiken,
// "Automatic Generation of Peephole Superoptimizers", ASPLOS 2006 (checking
// a candidate against every input of a fixed width before accepting it).

#include <array>
#include <cstdint>
#include <cstdio>
#include <vector>

using u8 = std::uint8_t;

struct Op {
    enum Kind { Shl, Add } kind;
    int rd, rs1, rs2;  // for Shl, rs2 holds the shift amount instead of a register
};

// Run one candidate program on three registers, r0 starting at `x` and the
// rest at 0, and return r0's final value. Every result wraps at 8 bits,
// matching a real register.
u8 run(const std::vector<Op>& prog, u8 x) {
    std::array<u8, 3> r = {x, 0, 0};
    for (const auto& op : prog) {
        if (op.kind == Op::Shl) {
            r[static_cast<std::size_t>(op.rd)] =
                static_cast<u8>(r[static_cast<std::size_t>(op.rs1)] << op.rs2);
        } else {
            r[static_cast<std::size_t>(op.rd)] = static_cast<u8>(
                r[static_cast<std::size_t>(op.rs1)] + r[static_cast<std::size_t>(op.rs2)]);
        }
    }
    return r[0];
}

bool matches_times_10(const std::vector<Op>& prog) {
    for (int x = 0; x <= 255; ++x) {
        if (run(prog, static_cast<u8>(x)) != static_cast<u8>(x * 10)) return false;
    }
    return true;
}

void print(const std::vector<Op>& prog) {
    for (const auto& op : prog) {
        if (op.kind == Op::Shl) {
            std::printf("  r%d = r%d << %d\n", op.rd, op.rs1, op.rs2);
        } else {
            std::printf("  r%d = r%d + r%d\n", op.rd, op.rs1, op.rs2);
        }
    }
}

// Every single instruction the search is allowed to use, in a fixed order:
// three registers, and shift amounts limited to 1..3 to keep the search
// small. The order matters only for which correct program is found first
// when more than one exists at the shortest length.
std::vector<Op> all_candidates() {
    std::vector<Op> ops;
    for (int rd = 0; rd < 3; ++rd) {
        for (int rs1 = 0; rs1 < 3; ++rs1) {
            for (int shift = 1; shift <= 3; ++shift) {
                ops.push_back(Op{Op::Shl, rd, rs1, shift});
            }
        }
    }
    for (int rd = 0; rd < 3; ++rd) {
        for (int rs1 = 0; rs1 < 3; ++rs1) {
            for (int rs2 = 0; rs2 < 3; ++rs2) {
                ops.push_back(Op{Op::Add, rd, rs1, rs2});
            }
        }
    }
    return ops;
}

// Extend `prog` with every candidate, in order, until it reaches `len`
// instructions; return true (leaving `prog` as the answer) the moment one
// full-length program checks out against all 256 inputs.
bool search(std::vector<Op>& prog, std::size_t len, const std::vector<Op>& candidates) {
    if (prog.size() == len) return matches_times_10(prog);
    for (const auto& c : candidates) {
        prog.push_back(c);
        if (search(prog, len, candidates)) return true;
        prog.pop_back();
    }
    return false;
}

int main() {
    const auto candidates = all_candidates();
    std::printf("Searching %zu single instructions, by increasing program length...\n",
                candidates.size());

    for (std::size_t len = 1; len <= 4; ++len) {
        std::vector<Op> prog;
        if (search(prog, len, candidates)) {
            std::printf("Found a %zu-instruction program computing x * 10 (mod 256):\n", len);
            print(prog);
            std::printf("Verified against all 256 possible 8-bit inputs.\n");
            return 0;
        }
    }
    std::printf("No program up to the length limit works.\n");
    return 1;
}

// Differential testing in miniature. Path 1, the reference, is a plain
// recursive evaluator. Path 2 is a macro expander that emits instructions
// for a two-register machine with a stack of 4-byte slots, followed by a
// small interpreter that runs those instructions. The Sub template has a
// planted bug: it was copied from Add, which is commutative, and loads its
// operands into the wrong registers. Nothing below mentions the bug; the
// comparison finds it.
//
// Follows: Csmith (random programs, differential testing as the oracle)
// and Yang, Chen, Eide, Regehr, "Finding and Understanding Bugs in C
// Compilers", PLDI 2011.
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

enum class Op { Const, Add, Sub, Mul };
struct Node {
    Op op;
    int value = 0;
    std::unique_ptr<Node> lhs, rhs;
};
std::unique_ptr<Node> leaf(int v) {
    auto n = std::make_unique<Node>();
    n->op = Op::Const;
    n->value = v;
    return n;
}
std::unique_ptr<Node> bin(Op op, std::unique_ptr<Node> l, std::unique_ptr<Node> r) {
    auto n = std::make_unique<Node>();
    n->op = op;
    n->lhs = std::move(l);
    n->rhs = std::move(r);
    return n;
}

int reference(const Node& n) {
    switch (n.op) {
        case Op::Const: return n.value;
        case Op::Add: return reference(*n.lhs) + reference(*n.rhs);
        case Op::Sub: return reference(*n.lhs) - reference(*n.rhs);
        case Op::Mul: return reference(*n.lhs) * reference(*n.rhs);
    }
    return 0;
}

// One instruction of the two-register machine: w0 op= w1 for arithmetic.
enum class I { Mov, Ldr, Str, Add, Sub, Mul };
struct Insn { I kind; int reg; int arg; };  // arg: immediate or slot

struct Expander {
    std::vector<Insn> code;
    int next_slot = 0;
    int expand(const Node& n) {
        if (n.op == Op::Const) {
            int s = next_slot++;
            code.push_back({I::Mov, 0, n.value});
            code.push_back({I::Str, 0, s});
            return s;
        }
        int ls = expand(*n.lhs), rs = expand(*n.rhs), s = next_slot++;
        bool swapped = n.op == Op::Sub;  // the planted bug
        code.push_back({I::Ldr, swapped ? 1 : 0, ls});
        code.push_back({I::Ldr, swapped ? 0 : 1, rs});
        I k = n.op == Op::Add ? I::Add : n.op == Op::Sub ? I::Sub : I::Mul;
        code.push_back({k, 0, 0});
        code.push_back({I::Str, 0, s});
        return s;
    }
};

int run(const std::vector<Insn>& code, int result_slot) {
    int w[2] = {0, 0};
    std::vector<int> stack(64, 0);
    for (const Insn& i : code) {
        switch (i.kind) {
            case I::Mov: w[i.reg] = i.arg; break;
            case I::Ldr: w[i.reg] = stack[i.arg]; break;
            case I::Str: stack[i.arg] = w[i.reg]; break;
            case I::Add: w[0] = w[0] + w[1]; break;
            case I::Sub: w[0] = w[0] - w[1]; break;
            case I::Mul: w[0] = w[0] * w[1]; break;
        }
    }
    return stack[result_slot];
}

int main() {
    std::vector<std::pair<std::string, std::unique_ptr<Node>>> cases;
    cases.emplace_back("7", leaf(7));
    cases.emplace_back("7 + 2", bin(Op::Add, leaf(7), leaf(2)));
    cases.emplace_back("7 - 2", bin(Op::Sub, leaf(7), leaf(2)));
    cases.emplace_back("7 * 2", bin(Op::Mul, leaf(7), leaf(2)));
    cases.emplace_back("(7 - 2) * 3", bin(Op::Mul, bin(Op::Sub, leaf(7), leaf(2)), leaf(3)));
    cases.emplace_back("4 - 4", bin(Op::Sub, leaf(4), leaf(4)));

    int agreed = 0;
    for (const auto& [text, tree] : cases) {
        Expander e;
        int slot = e.expand(*tree);
        int expected = reference(*tree), actual = run(e.code, slot);
        agreed += expected == actual;
        std::printf("%-12s reference=%-4d native=%-4d %s\n", text.c_str(), expected,
                    actual, expected == actual ? "agree" : "DISAGREE");
    }
    std::printf("%d of %zu agree\n", agreed, cases.size());
}

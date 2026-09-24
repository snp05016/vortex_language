// Differential testing: run the same expression through two independently
// written evaluators and compare. "ref" is a plain recursive evaluator, the
// stand-in for a trusted reference path. "sim" walks the tree the way a
// macro-expansion back end would, simulating the instructions each template
// emits against a small stack of registers. sim's Sub case has a planted
// bug (its operands are swapped), the kind a non-commutative operation
// invites when a template is copied and edited carelessly. The two
// evaluators agree on Add and Mul but disagree wherever Sub appears, and
// the differential check reports exactly that, without knowing in advance
// what the bug is.
//
// Follows: Xuejun Yang, Yang Chen, Eric Eide, John Regehr, "Finding and
// Understanding Bugs in C Compilers", PLDI 2011 (differential testing of a
// compiler against a second, independent implementation).
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

std::unique_ptr<Node> constant(int v) {
    auto n = std::make_unique<Node>();
    n->op = Op::Const;
    n->value = v;
    return n;
}

std::unique_ptr<Node> binary(Op op, std::unique_ptr<Node> l, std::unique_ptr<Node> r) {
    auto n = std::make_unique<Node>();
    n->op = op;
    n->lhs = std::move(l);
    n->rhs = std::move(r);
    return n;
}

int ref_eval(const Node& n) {
    switch (n.op) {
        case Op::Const: return n.value;
        case Op::Add: return ref_eval(*n.lhs) + ref_eval(*n.rhs);
        case Op::Sub: return ref_eval(*n.lhs) - ref_eval(*n.rhs);
        case Op::Mul: return ref_eval(*n.lhs) * ref_eval(*n.rhs);
    }
    return 0;
}

// A stack-machine simulator standing in for "run the generated code".
int sim_eval(const Node& n) {
    switch (n.op) {
        case Op::Const: return n.value;
        case Op::Add: return sim_eval(*n.lhs) + sim_eval(*n.rhs);
        case Op::Sub: return sim_eval(*n.rhs) - sim_eval(*n.lhs);  // bug: swapped
        case Op::Mul: return sim_eval(*n.lhs) * sim_eval(*n.rhs);
    }
    return 0;
}

struct Case {
    std::string name;
    std::unique_ptr<Node> tree;
};

int main() {
    std::vector<Case> cases;
    cases.push_back({"7", constant(7)});
    cases.push_back({"7 + 2", binary(Op::Add, constant(7), constant(2))});
    cases.push_back({"7 - 2", binary(Op::Sub, constant(7), constant(2))});
    cases.push_back({"7 * 2", binary(Op::Mul, constant(7), constant(2))});
    cases.push_back({"(7 - 2) * 3",
                      binary(Op::Mul, binary(Op::Sub, constant(7), constant(2)),
                             constant(3))});

    int passed = 0;
    for (const auto& c : cases) {
        int expected = ref_eval(*c.tree);
        int actual = sim_eval(*c.tree);
        bool ok = expected == actual;
        passed += ok ? 1 : 0;
        std::printf("%-12s ref=%-4d sim=%-4d %s\n", c.name.c_str(), expected,
                    actual, ok ? "PASS" : "FAIL");
    }
    std::printf("%d/%zu passed\n", passed, cases.size());
}

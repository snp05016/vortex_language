// A macro-expansion code generator for a tiny calculator language: one
// template of AArch64 instructions per node type, no register allocator.
// Every result goes straight to a fresh stack slot, and every operand is
// reloaded from its slot right before it is used, so no register ever
// holds a value that outlives the few instructions of its own template.
//
// Operands are expanded left before right, and the left one is loaded into
// w0, because sub computes w0 - w1.
//
// Follows: Abdulaziz Ghuloum, "An Incremental Approach to Compiler
// Construction" (intermediate values saved in stack locations), and the Arm
// A64 ISA (DDI 0602) for the instructions themselves.
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

enum class Op { Const, Add, Sub, Mul };

struct Node {
    Op op;
    int value = 0;  // used when op == Op::Const
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

class MacroExpander {
public:
    std::vector<std::string> code;
    int next_slot = 0;

    // Returns the stack slot that holds this node's result.
    int expand(const Node& n) {
        if (n.op == Op::Const) {
            int s = next_slot++;
            emit("mov w0, #" + std::to_string(n.value));
            emit("str w0, [sp, #" + std::to_string(offset(s)) + "]");
            return s;
        }
        int ls = expand(*n.lhs);
        int rs = expand(*n.rhs);
        int s = next_slot++;
        emit("ldr w0, [sp, #" + std::to_string(offset(ls)) + "]");
        emit("ldr w1, [sp, #" + std::to_string(offset(rs)) + "]");
        emit(mnemonic(n.op) + " w0, w0, w1");
        emit("str w0, [sp, #" + std::to_string(offset(s)) + "]");
        return s;
    }

private:
    static int offset(int slot) { return slot * 4; }

    static std::string mnemonic(Op op) {
        switch (op) {
            case Op::Add: return "add";
            case Op::Sub: return "sub";
            case Op::Mul: return "mul";
            case Op::Const: break;
        }
        return "";
    }

    void emit(std::string line) { code.push_back(std::move(line)); }
};

int main() {
    // (7 - 2) * 3
    auto tree = binary(Op::Mul,
                        binary(Op::Sub, constant(7), constant(2)),
                        constant(3));

    MacroExpander expander;
    int result_slot = expander.expand(*tree);

    for (const auto& line : expander.code) {
        std::printf("%s\n", line.c_str());
    }
    // sp must stay a multiple of 16, so the frame rounds the slots up.
    int slot_bytes = expander.next_slot * 4;
    int frame = (slot_bytes + 15) / 16 * 16;
    std::printf("result in slot %d; %d bytes of slots, frame of %d bytes\n",
                result_slot, slot_bytes, frame);
}

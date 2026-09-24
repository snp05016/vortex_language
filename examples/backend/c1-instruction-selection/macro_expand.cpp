// Macro-expansion instruction selection: one instruction template per IR
// operation, with no regard for what neighboring operations could share.
// The tree below stands for load(a + ((i * 4 + j) << 2)), an address
// computation shaped like an array element access.
#include <cstdio>
#include <memory>
#include <string>

enum class Kind { Leaf, Add, Mul, Shl, Load };

struct Node {
    Kind kind;
    std::string name;                    // for Leaf only
    std::unique_ptr<Node> a, b;           // operands, in that order
};

std::unique_ptr<Node> leaf(std::string name) {
    return std::make_unique<Node>(Node{Kind::Leaf, std::move(name), nullptr, nullptr});
}
std::unique_ptr<Node> op(Kind k, std::unique_ptr<Node> a, std::unique_ptr<Node> b = nullptr) {
    return std::make_unique<Node>(Node{k, "", std::move(a), std::move(b)});
}

// a + ((i * 4 + j) << 2)
std::unique_ptr<Node> build_tree() {
    auto mul = op(Kind::Mul, leaf("i"), leaf("4"));
    auto add_inner = op(Kind::Add, std::move(mul), leaf("j"));
    auto shl = op(Kind::Shl, std::move(add_inner), leaf("2"));
    auto add_outer = op(Kind::Add, leaf("a"), std::move(shl));
    return op(Kind::Load, std::move(add_outer));
}

int instruction_count = 0;

// Emits one instruction per node and returns the name holding its result.
// A leaf costs nothing: its value is already in a register or is an
// immediate operand.
std::string macro_expand(Node* n) {
    if (n->kind == Kind::Leaf) return n->name;
    std::string lhs = macro_expand(n->a.get());
    std::string rhs = n->b ? macro_expand(n->b.get()) : "";
    std::string result = "t" + std::to_string(++instruction_count);
    switch (n->kind) {
        case Kind::Add:  std::printf("%s = add %s, %s\n", result.c_str(), lhs.c_str(), rhs.c_str()); break;
        case Kind::Mul:  std::printf("%s = mul %s, %s\n", result.c_str(), lhs.c_str(), rhs.c_str()); break;
        case Kind::Shl:  std::printf("%s = shl %s, %s\n", result.c_str(), lhs.c_str(), rhs.c_str()); break;
        case Kind::Load: std::printf("%s = load [%s]\n", result.c_str(), lhs.c_str()); break;
        default: break;
    }
    return result;
}

int main() {
    auto tree = build_tree();
    macro_expand(tree.get());
    std::printf("%d instructions\n", instruction_count);
}

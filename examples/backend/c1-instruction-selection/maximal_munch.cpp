// Maximal munch: at each node, try the largest matching instruction tile
// first, so one tile can cover several tree nodes at once. Same tree as
// macro_expand.cpp: load(a + ((i * 4 + j) << 2)), tiled for AArch64, whose
// register-offset load and madd fuse a shift, a multiply and an add into
// the address computation itself (see A2, "One element of a fixed-shape
// array").
#include <cstdio>
#include <memory>
#include <string>

enum class Kind { Leaf, Add, Mul, Shl, Load };

struct Node {
    Kind kind;
    std::string name;
    std::unique_ptr<Node> a, b;
};

std::unique_ptr<Node> leaf(std::string name) {
    return std::make_unique<Node>(Node{Kind::Leaf, std::move(name), nullptr, nullptr});
}
std::unique_ptr<Node> op(Kind k, std::unique_ptr<Node> a, std::unique_ptr<Node> b = nullptr) {
    return std::make_unique<Node>(Node{k, "", std::move(a), std::move(b)});
}

std::unique_ptr<Node> build_tree() {
    auto mul = op(Kind::Mul, leaf("i"), leaf("4"));
    auto add_inner = op(Kind::Add, std::move(mul), leaf("j"));
    auto shl = op(Kind::Shl, std::move(add_inner), leaf("2"));
    auto add_outer = op(Kind::Add, leaf("a"), std::move(shl));
    return op(Kind::Load, std::move(add_outer));
}

int instruction_count = 0;
std::string fresh() { return "t" + std::to_string(++instruction_count); }

std::string munch(Node* n) {
    if (n->kind == Kind::Leaf) return n->name;

    // Largest tile: ldr <result> = [<base>, <index>, lsl #<amt>]
    // matches load(add(base, shl(index, amt))).
    if (n->kind == Kind::Load && n->a->kind == Kind::Add && n->a->b->kind == Kind::Shl) {
        Node* add = n->a.get();
        Node* shl = add->b.get();
        std::string base = munch(add->a.get());
        std::string index = munch(shl->a.get());
        std::string amt = shl->b->name; // shift amount must be a compile-time constant
        std::string r = fresh();
        std::printf("%s = ldr [%s, %s, lsl #%s]\n", r.c_str(), base.c_str(), index.c_str(), amt.c_str());
        return r;
    }
    // Next tile: madd <result> = <a> * <b> + <c> matches add(mul(a, b), c).
    if (n->kind == Kind::Add && n->a->kind == Kind::Mul) {
        Node* mul = n->a.get();
        std::string a = munch(mul->a.get());
        std::string b = munch(mul->b.get());
        std::string c = munch(n->b.get());
        std::string r = fresh();
        std::printf("%s = madd %s, %s, %s\n", r.c_str(), a.c_str(), b.c_str(), c.c_str());
        return r;
    }
    // Fallback tiles: one instruction per node, same as macro expansion.
    std::string a = munch(n->a.get());
    std::string b = n->b ? munch(n->b.get()) : "";
    std::string r = fresh();
    switch (n->kind) {
        case Kind::Add:  std::printf("%s = add %s, %s\n", r.c_str(), a.c_str(), b.c_str()); break;
        case Kind::Mul:  std::printf("%s = mul %s, %s\n", r.c_str(), a.c_str(), b.c_str()); break;
        case Kind::Shl:  std::printf("%s = shl %s, %s\n", r.c_str(), a.c_str(), b.c_str()); break;
        case Kind::Load: std::printf("%s = load [%s]\n", r.c_str(), a.c_str()); break;
        default: break;
    }
    return r;
}

int main() {
    auto tree = build_tree();
    munch(tree.get());
    std::printf("%d instructions\n", instruction_count);
}

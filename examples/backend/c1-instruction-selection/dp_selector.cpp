// Bottom-up dynamic-programming tiling (the twig/BURS approach: Aho,
// Ganapathi, Tjiang, "Code generation using tree matching and dynamic
// programming", TOPLAS 1989). Instead of greedily taking the largest tile
// at the root, it computes the cheapest cover of every subtree first, then
// reads the choice back down from the root. Same tile set as
// maximal_munch.cpp, plus a second, larger tree to show the table growing.
#include <cstdio>
#include <climits>
#include <memory>
#include <string>
#include <unordered_map>

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
std::unique_ptr<Node> addr_tree(std::string base, std::string idx1, std::string idx2) {
    auto mul = op(Kind::Mul, leaf(idx1), leaf("4"));
    auto add_inner = op(Kind::Add, std::move(mul), leaf(idx2));
    auto shl = op(Kind::Shl, std::move(add_inner), leaf("2"));
    auto add_outer = op(Kind::Add, leaf(base), std::move(shl));
    return op(Kind::Load, std::move(add_outer));
}

std::unordered_map<Node*, int> cost;
std::unordered_map<Node*, int> choice; // 0 = ldr-regoffset, 1 = madd, 2 = fallback

void compute_cost(Node* n) {
    if (n->kind == Kind::Leaf) { cost[n] = 0; return; }
    compute_cost(n->a.get());
    if (n->b) compute_cost(n->b.get());

    int best = INT_MAX, tile = 2;
    if (n->kind == Kind::Load && n->a->kind == Kind::Add && n->a->b->kind == Kind::Shl) {
        Node *add = n->a.get(), *shl = add->b.get();
        int c = 1 + cost[add->a.get()] + cost[shl->a.get()];
        if (c < best) { best = c; tile = 0; }
    }
    if (n->kind == Kind::Add && n->a->kind == Kind::Mul) {
        Node* mul = n->a.get();
        int c = 1 + cost[mul->a.get()] + cost[mul->b.get()] + cost[n->b.get()];
        if (c < best) { best = c; tile = 1; }
    }
    int fallback = 1 + cost[n->a.get()] + (n->b ? cost[n->b.get()] : 0);
    if (fallback < best) { best = fallback; tile = 2; }
    cost[n] = best;
    choice[n] = tile;
}

int instruction_count = 0;
std::string fresh() { return "t" + std::to_string(++instruction_count); }

std::string emit(Node* n) {
    if (n->kind == Kind::Leaf) return n->name;
    if (choice[n] == 0) {
        Node *add = n->a.get(), *shl = add->b.get();
        std::string base = emit(add->a.get()), index = emit(shl->a.get());
        std::string r = fresh();
        std::printf("%s = ldr [%s, %s, lsl #%s]\n", r.c_str(), base.c_str(), index.c_str(), shl->b->name.c_str());
        return r;
    }
    if (choice[n] == 1) {
        Node* mul = n->a.get();
        std::string a = emit(mul->a.get()), b = emit(mul->b.get()), c = emit(n->b.get());
        std::string r = fresh();
        std::printf("%s = madd %s, %s, %s\n", r.c_str(), a.c_str(), b.c_str(), c.c_str());
        return r;
    }
    std::string a = emit(n->a.get());
    std::string b = n->b ? emit(n->b.get()) : "";
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

void run(const char* label, std::unique_ptr<Node> tree) {
    cost.clear(); choice.clear(); instruction_count = 0;
    compute_cost(tree.get());
    std::printf("-- %s --\n", label);
    emit(tree.get());
    std::printf("%d instructions, dp cost %d\n", instruction_count, cost[tree.get()]);
}

int main() {
    run("one address", addr_tree("a", "i", "j"));
    // Two independent addresses added together: the table now has to price
    // two disjoint fused covers plus the outer add, not just one.
    auto both = op(Kind::Add, addr_tree("a", "i", "j"), addr_tree("b", "k", "l"));
    run("two addresses", std::move(both));
}

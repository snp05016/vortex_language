// Maximal munch: at each node, starting at the root, take the first tile in
// the list that matches, then munch the subtrees the tile leaves as register
// operands. Tiles are listed largest first; the run is repeated with two
// tiles of equal size swapped, to show that the greedy result depends on it.
// Same tree as macro_expand.cpp: load(a + ((i * 4 + j) << 2)).
#include <bit>
#include <cstdio>
#include <deque>
#include <format>
#include <string>
#include <vector>

enum class Op { Var, Const, Add, Mul, Shl, Load };
struct Node { Op op; std::string name; long value; std::vector<Node*> kids; };
std::deque<Node> arena;
Node* var(std::string n) { return &arena.emplace_back(Node{Op::Var, n, 0, {}}); }
Node* num(long v) { return &arena.emplace_back(Node{Op::Const, "", v, {}}); }
Node* op(Op o, std::vector<Node*> k) { return &arena.emplace_back(Node{o, "", 0, k}); }

// A pattern is a small tree. Its holes are Reg (any subtree, computed into a
// register first) and Imm (a constant that passes a test, written into the
// instruction itself).
struct Pat { enum Kind { Reg, Imm, Tree } kind; Op op; std::vector<Pat> kids; bool (*ok)(long); };
Pat R() { return {Pat::Reg, Op::Var, {}, nullptr}; }
Pat I(bool (*ok)(long)) { return {Pat::Imm, Op::Const, {}, ok}; }
Pat T(Op o, std::vector<Pat> k) { return {Pat::Tree, o, k, nullptr}; }
bool any(long) { return true; }
bool is2(long v) { return v == 2; }  // a 4-byte load allows only lsl #0 or #2
bool pow2(long v) { return v > 0 && std::has_single_bit(static_cast<unsigned long>(v)); }
int lg(long v) { return std::countr_zero(static_cast<unsigned long>(v)); }

struct Bind { std::vector<Node*> regs; std::vector<long> imms; };
bool match(const Pat& p, Node* n, Bind& b) {
    if (p.kind == Pat::Reg) { b.regs.push_back(n); return true; }
    if (p.kind == Pat::Imm) {
        if (n->op != Op::Const || !p.ok(n->value)) return false;
        b.imms.push_back(n->value);
        return true;
    }
    if (n->op != p.op) return false;
    for (std::size_t i = 0; i < p.kids.size(); ++i)
        if (!match(p.kids[i], n->kids[i], b)) return false;
    return true;
}

using S = std::vector<std::string>;
using K = std::vector<long>;
struct Tile { const char* name; Pat pat; std::string (*text)(const std::string&, const S&, const K&); };

int count = 0;
std::string munch(Node* n, const std::vector<Tile>& tiles) {
    if (n->op == Op::Var) return n->name;
    for (const Tile& t : tiles) {
        Bind b;
        if (!match(t.pat, n, b)) continue;
        S r;
        for (Node* k : b.regs) r.push_back(munch(k, tiles));
        std::string d = "t" + std::to_string(++count);
        std::printf("  %s\n", t.text(d, r, b.imms).c_str());
        return d;
    }
    return "?";  // unreachable: every node kind has a one-node tile below
}

int main() {
    Tile ldr_shifted{"ldr-shifted", T(Op::Load, {T(Op::Add, {R(), T(Op::Shl, {R(), I(is2)})})}),
        [](auto& d, auto& r, auto&) { return std::format("ldr  {}, [{}, {}, lsl #2]", d, r[0], r[1]); }};
    Tile madd{"madd", T(Op::Add, {T(Op::Mul, {R(), R()}), R()}),
        [](auto& d, auto& r, auto&) { return std::format("madd {}, {}, {}, {}", d, r[0], r[1], r[2]); }};
    Tile add_shifted{"add-shifted", T(Op::Add, {T(Op::Mul, {R(), I(pow2)}), R()}),
        [](auto& d, auto& r, auto& k) { return std::format("add  {}, {}, {}, lsl #{}", d, r[1], r[0], lg(k[0])); }};
    std::vector<Tile> small{
        {"ldr", T(Op::Load, {R()}), [](auto& d, auto& r, auto&) { return std::format("ldr  {}, [{}]", d, r[0]); }},
        {"add", T(Op::Add, {R(), R()}), [](auto& d, auto& r, auto&) { return std::format("add  {}, {}, {}", d, r[0], r[1]); }},
        {"mul", T(Op::Mul, {R(), R()}), [](auto& d, auto& r, auto&) { return std::format("mul  {}, {}, {}", d, r[0], r[1]); }},
        {"lsl-imm", T(Op::Shl, {R(), I(any)}), [](auto& d, auto& r, auto& k) { return std::format("lsl  {}, {}, #{}", d, r[0], k[0]); }},
        {"mov", I(any), [](auto& d, auto&, auto& k) { return std::format("mov  {}, #{}", d, k[0]); }},
    };

    Node* index = op(Op::Add, {op(Op::Mul, {var("i"), num(4)}), var("j")});
    Node* tree = op(Op::Load, {op(Op::Add, {var("a"), op(Op::Shl, {index, num(2)})})});

    for (bool madd_first : {false, true}) {
        std::vector<Tile> tiles{ldr_shifted, madd_first ? madd : add_shifted, madd_first ? add_shifted : madd};
        tiles.insert(tiles.end(), small.begin(), small.end());
        count = 0;
        std::printf("%s listed first:\n", tiles[1].name);
        munch(tree, tiles);
        std::printf("  %d instructions\n", count);
    }
}

// Optimal tiling by dynamic programming: label every node bottom-up with the
// cheapest way to compute it into a register, trying every tile that matches
// there, then emit top-down from the root. Order in the tile list no longer
// matters except to break exact ties. Pattern matcher as in maximal_munch.cpp;
// cost = instructions. The same engine then runs an x86-64 tile set.
#include <bit>
#include <climits>
#include <cstdio>
#include <deque>
#include <format>
#include <map>
#include <string>
#include <vector>

enum class Op { Var, Const, Add, Mul, Shl, Load };
struct Node { Op op; std::string name; long value; std::vector<Node*> kids; };
std::deque<Node> arena;
Node* var(std::string n) { return &arena.emplace_back(Node{Op::Var, n, 0, {}}); }
Node* num(long v) { return &arena.emplace_back(Node{Op::Const, std::to_string(v), v, {}}); }
Node* op(Op o, std::vector<Node*> k) { return &arena.emplace_back(Node{o, "", 0, k}); }
std::string show(Node* n) {
    static const char* names[] = {"", "", "add", "mul", "shl", "load"};
    if (n->kids.empty()) return n->name;
    std::string s = std::string(names[static_cast<int>(n->op)]) + "(" + show(n->kids[0]);
    if (n->kids.size() > 1) s += ", " + show(n->kids[1]);
    return s + ")";
}

struct Pat { enum Kind { Reg, Imm, Tree } kind; Op op; std::vector<Pat> kids; bool (*ok)(long); };
Pat R() { return {Pat::Reg, Op::Var, {}, nullptr}; }
Pat I(bool (*ok)(long)) { return {Pat::Imm, Op::Const, {}, ok}; }
Pat T(Op o, std::vector<Pat> k) { return {Pat::Tree, o, k, nullptr}; }
bool any(long) { return true; }
bool is2(long v) { return v == 2; }
bool pow2(long v) { return v > 0 && std::has_single_bit(static_cast<unsigned long>(v)); }
bool scale(long v) { return v == 1 || v == 2 || v == 4 || v == 8; }  // x86-64 SIB scales
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
struct Tile { const char* name; Pat pat; int cost; std::string (*text)(const std::string&, const S&, const K&); };
struct Best { int cost; const Tile* tile; Bind bind; };
std::map<Node*, Best> best;

// Bottom-up: when a node is labelled, every subtree a tile can leave as a
// register operand has its cheapest cost already in the table.
void label(Node* n, const std::vector<Tile>& tiles, bool print) {
    for (Node* k : n->kids) label(k, tiles, print);
    if (n->op == Op::Var) { best[n] = {0, nullptr, {}}; return; }
    Best b{INT_MAX, nullptr, {}};
    if (print) std::printf("%s\n", show(n).c_str());
    for (const Tile& t : tiles) {
        Bind bind;
        if (!match(t.pat, n, bind)) continue;
        int c = t.cost;
        std::string sum = std::to_string(t.cost);
        for (Node* k : bind.regs) { c += best[k].cost; sum += " + " + std::to_string(best[k].cost); }
        if (print) std::printf("  %-12s %s = %d\n", t.name, sum.c_str(), c);
        if (c < b.cost) b = {c, &t, bind};
    }
    best[n] = b;
}

int count = 0;
std::string emit(Node* n) {  // top-down, using the choices the table recorded
    const Best& b = best[n];
    if (!b.tile) return n->name;
    S r;
    for (Node* k : b.bind.regs) r.push_back(emit(k));
    std::string d = "t" + std::to_string(++count);
    std::printf("  %s\n", b.tile->text(d, r, b.bind.imms).c_str());
    return d;
}

void select(const char* target, Node* tree, const std::vector<Tile>& tiles, bool print) {
    best.clear();
    count = 0;
    label(tree, tiles, print);
    std::printf("%s cover of %s, cost %d:\n", target, show(tree).c_str(), best[tree].cost);
    emit(tree);
}

int main() {
    std::vector<Tile> a64{
        {"ldr-shifted", T(Op::Load, {T(Op::Add, {R(), T(Op::Shl, {R(), I(is2)})})}), 1,
         [](auto& d, auto& r, auto&) { return std::format("ldr  {}, [{}, {}, lsl #2]", d, r[0], r[1]); }},
        {"madd", T(Op::Add, {T(Op::Mul, {R(), R()}), R()}), 1,
         [](auto& d, auto& r, auto&) { return std::format("madd {}, {}, {}, {}", d, r[0], r[1], r[2]); }},
        {"add-shifted", T(Op::Add, {T(Op::Mul, {R(), I(pow2)}), R()}), 1,
         [](auto& d, auto& r, auto& k) { return std::format("add  {}, {}, {}, lsl #{}", d, r[1], r[0], lg(k[0])); }},
        {"ldr", T(Op::Load, {R()}), 1, [](auto& d, auto& r, auto&) { return std::format("ldr  {}, [{}]", d, r[0]); }},
        {"add", T(Op::Add, {R(), R()}), 1, [](auto& d, auto& r, auto&) { return std::format("add  {}, {}, {}", d, r[0], r[1]); }},
        {"mul", T(Op::Mul, {R(), R()}), 1, [](auto& d, auto& r, auto&) { return std::format("mul  {}, {}, {}", d, r[0], r[1]); }},
        {"lsl-imm", T(Op::Shl, {R(), I(any)}), 1, [](auto& d, auto& r, auto& k) { return std::format("lsl  {}, {}, #{}", d, r[0], k[0]); }},
        {"mov", I(any), 1, [](auto& d, auto&, auto& k) { return std::format("mov  {}, #{}", d, k[0]); }},
    };
    // x86-64 (Intel syntax). Two-address forms cost a copy: "mov d, x" first.
    std::vector<Tile> x64{
        {"mov-sib", T(Op::Load, {T(Op::Add, {R(), T(Op::Shl, {R(), I(is2)})})}), 1,
         [](auto& d, auto& r, auto&) { return std::format("mov  {}, dword ptr [{} + 4*{}]", d, r[0], r[1]); }},
        {"lea-sib", T(Op::Add, {T(Op::Mul, {R(), I(scale)}), R()}), 1,
         [](auto& d, auto& r, auto& k) { return std::format("lea  {}, [{} + {}*{}]", d, r[1], k[0], r[0]); }},
        {"mov-load", T(Op::Load, {R()}), 1, [](auto& d, auto& r, auto&) { return std::format("mov  {}, dword ptr [{}]", d, r[0]); }},
        {"lea-add", T(Op::Add, {R(), R()}), 1, [](auto& d, auto& r, auto&) { return std::format("lea  {}, [{} + {}]", d, r[0], r[1]); }},
        {"imul-imm", T(Op::Mul, {R(), I(any)}), 1, [](auto& d, auto& r, auto& k) { return std::format("imul {}, {}, {}", d, r[0], k[0]); }},
        {"imul", T(Op::Mul, {R(), R()}), 2, [](auto& d, auto& r, auto&) { return std::format("mov  {}, {}\n  imul {}, {}", d, r[0], d, r[1]); }},
        {"shl-imm", T(Op::Shl, {R(), I(any)}), 2, [](auto& d, auto& r, auto& k) { return std::format("mov  {}, {}\n  shl  {}, {}", d, r[0], d, k[0]); }},
        {"mov-imm", I(any), 1, [](auto& d, auto&, auto& k) { return std::format("mov  {}, {}", d, k[0]); }},
    };

    Node* index = op(Op::Add, {op(Op::Mul, {var("i"), num(4)}), var("j")});
    Node* elem = op(Op::Load, {op(Op::Add, {var("a"), op(Op::Shl, {index, num(2)})})});
    Node* row = op(Op::Add, {op(Op::Mul, {var("i"), var("n")}), var("j")});  // stride in a register

    select("AArch64", elem, a64, true);
    select("x86-64", elem, x64, false);
    select("AArch64", row, a64, false);
    select("x86-64", row, x64, false);
}

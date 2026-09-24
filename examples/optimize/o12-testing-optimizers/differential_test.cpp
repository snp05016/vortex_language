// A random-program differential test, in the style of Csmith: generate small
// integer-expression programs, evaluate each two ways, and report the first
// disagreement. Follows: Yang, Chen, Eide, Regehr, PLDI 2011 (see .toml).
#include <cstdint>
#include <iostream>
#include <string>
#include <variant>
#include <vector>

struct Lit { int value; };
struct Bin { char op; int lhs; int rhs; }; // op in {+,-,*,/}; lhs/rhs are node indices

using Node = std::variant<Lit, Bin>;

// A tiny linear congruential generator: fixed seed, same sequence every run,
// on every machine. std::mt19937 would work too, but this makes the
// generator itself auditable in eight lines.
struct Lcg {
    uint64_t state;
    explicit Lcg(uint64_t seed) : state(seed) {}
    uint32_t next() {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<uint32_t>(state >> 32);
    }
    int small(int lo, int hi) { return lo + static_cast<int>(next() % (hi - lo + 1)); }
};

// Builds a shallow expression tree: each internal node picks two already-built
// nodes as operands, so the tree is always well formed by construction.
std::vector<Node> generateProgram(Lcg& rng, int nodeCount) {
    std::vector<Node> nodes;
    nodes.push_back(Lit{rng.small(-9, 9)});
    nodes.push_back(Lit{rng.small(-9, 9)});
    while (static_cast<int>(nodes.size()) < nodeCount) {
        char ops[] = {'+', '-', '*', '/'};
        char op = ops[rng.small(0, 3)];
        int lhs = rng.small(0, static_cast<int>(nodes.size()) - 1);
        int rhs = rng.small(0, static_cast<int>(nodes.size()) - 1);
        if (op == '/') nodes[rhs] = Lit{2}; // never generate a division by zero
        nodes.push_back(Bin{op, lhs, rhs});
    }
    return nodes;
}

int evalReference(const std::vector<Node>& p, int i) {
    if (auto* l = std::get_if<Lit>(&p[i])) return l->value;
    auto& b = std::get<Bin>(p[i]);
    int lhs = evalReference(p, b.lhs), rhs = evalReference(p, b.rhs);
    switch (b.op) {
        case '+': return lhs + rhs;
        case '-': return lhs - rhs;
        case '*': return lhs * rhs;
        default:  return lhs / rhs; // C++ division truncates toward zero
    }
}

// The "optimizer": same rules, except it strength-reduces x / 2 to x >> 1.
// That rewrite is wrong whenever x is negative, because >> rounds toward
// negative infinity while C++'s / rounds toward zero.
int evalOptimized(const std::vector<Node>& p, int i) {
    if (auto* l = std::get_if<Lit>(&p[i])) return l->value;
    auto& b = std::get<Bin>(p[i]);
    int lhs = evalOptimized(p, b.lhs), rhs = evalOptimized(p, b.rhs);
    switch (b.op) {
        case '+': return lhs + rhs;
        case '-': return lhs - rhs;
        case '*': return lhs * rhs;
        default:  return lhs >> 1; // rhs is always the literal 2
    }
}

int main() {
    Lcg rng(0xC57173D1); // fixed seed: same programs, same result, every run
    const int programCount = 500;
    for (int i = 0; i < programCount; ++i) {
        auto program = generateProgram(rng, 6);
        int root = static_cast<int>(program.size()) - 1;
        int reference = evalReference(program, root);
        int optimized = evalOptimized(program, root);
        if (reference != optimized) {
            std::cout << "mismatch at program " << i
                      << ": reference=" << reference
                      << " optimized=" << optimized << "\n";
            return 0;
        }
    }
    std::cout << programCount << " programs matched\n";
    return 0;
}

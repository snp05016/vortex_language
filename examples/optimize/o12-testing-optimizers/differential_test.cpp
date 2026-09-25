// A random-program differential test: generate small integer expressions
// from a fixed seed, evaluate each one two ways, and report the first
// program on which the two ways disagree.
// Follows: Yang, Chen, Eide and Regehr, PLDI 2011 (see .toml).
#include <cstdint>
#include <iostream>
#include <string>
#include <variant>
#include <vector>

struct Lit { std::int64_t value; };
struct Bin { char op; int lhs; int rhs; };  // operands are earlier node indices
using Node = std::variant<Lit, Bin>;
using Program = std::vector<Node>;           // the last node is the result

// A linear congruential generator with a fixed seed: the same programs on
// every run and every machine, unlike std::random_device.
struct Lcg {
    std::uint64_t state;
    std::uint32_t next() {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<std::uint32_t>(state >> 32);
    }
    int pick(int lo, int hi) { return lo + static_cast<int>(next() % (hi - lo + 1)); }
};

// Two literals in -9..9, then four operations on earlier nodes. At most four
// multiplications of numbers below 10 stay far inside int64_t, and every
// division is by a fresh literal 2: the generator rules out undefined
// behavior by construction, as Csmith must.
Program generate(Lcg& rng) {
    Program p{Lit{rng.pick(-9, 9)}, Lit{rng.pick(-9, 9)}};
    for (int step = 0; step < 4; ++step) {
        char op = "+-*/"[rng.pick(0, 3)];
        int lhs = rng.pick(0, static_cast<int>(p.size()) - 1);
        int rhs = rng.pick(0, static_cast<int>(p.size()) - 1);
        if (op == '/') {
            p.push_back(Lit{2});
            rhs = static_cast<int>(p.size()) - 1;
        }
        p.push_back(Bin{op, lhs, rhs});
    }
    return p;
}

// optimized == false: the reference meaning, where / truncates toward zero.
// optimized == true: the same, except that x / 2 becomes x >> 1, which
// rounds toward negative infinity instead.
std::int64_t evaluate(const Program& p, int i, bool optimized) {
    if (auto* l = std::get_if<Lit>(&p[i])) return l->value;
    const Bin& b = std::get<Bin>(p[i]);
    std::int64_t x = evaluate(p, b.lhs, optimized), y = evaluate(p, b.rhs, optimized);
    switch (b.op) {
        case '+': return x + y;
        case '-': return x - y;
        case '*': return x * y;
        default:  return optimized ? x >> 1 : x / y;
    }
}

std::string show(const Program& p, int i) {
    if (auto* l = std::get_if<Lit>(&p[i])) return std::to_string(l->value);
    const Bin& b = std::get<Bin>(p[i]);
    return "(" + show(p, b.lhs) + " " + b.op + " " + show(p, b.rhs) + ")";
}

int main() {
    Lcg rng{42};
    int withDivision = 0;
    for (int n = 0; n < 500; ++n) {
        Program p = generate(rng);
        int root = static_cast<int>(p.size()) - 1;
        std::string text = show(p, root);
        std::int64_t ref = evaluate(p, root, false), opt = evaluate(p, root, true);
        if (ref != opt) {
            std::cout << "program " << n << ": " << text << "\n"
                      << "reference " << ref << ", optimized " << opt << "\n"
                      << withDivision << " earlier programs divided by 2 and agreed\n";
            return 0;
        }
        if (text.find('/') != std::string::npos) ++withDivision;
    }
    std::cout << "500 programs agreed\n";
}

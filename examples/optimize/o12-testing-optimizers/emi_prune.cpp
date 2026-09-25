// Equivalence modulo inputs in miniature. Run a program on one input and
// record which statements executed. Every variant that deletes only
// statements that did not execute must print the same value on that input.
// Compile the original and each variant with a buggy optimizer and compare.
// Follows: Le, Afshari and Su, PLDI 2014 (see .toml).
#include <cstddef>
#include <iostream>
#include <string>
#include <vector>

// One statement of a toy language with one variable x, starting at 0:
// "x = x <op> k", run only when the input exceeds `guard` (-1: always).
struct Stmt { int guard; char op; int k; };
using Program = std::vector<Stmt>;

int run(const Program& p, int input, std::vector<bool>* executed = nullptr) {
    int x = 0;
    for (std::size_t i = 0; i < p.size(); ++i) {
        if (p[i].guard >= 0 && input <= p[i].guard) continue;
        if (executed) (*executed)[i] = true;
        if (p[i].op == '+') x += p[i].k;
        if (p[i].op == '*') x *= p[i].k;
        if (p[i].op == '/') x /= p[i].k;
    }
    return x;
}

// The optimizer under test. Its one rule deletes an unguarded "x = x / 2"
// that is immediately followed by an unguarded "x = x * 2", as if the two
// cancelled. They do not when x is odd: (7 / 2) * 2 is 6, not 7.
Program optimize(const Program& p) {
    Program out;
    for (std::size_t i = 0; i < p.size(); ++i) {
        bool pair = i + 1 < p.size() && p[i].guard < 0 && p[i + 1].guard < 0
                    && p[i].op == '/' && p[i].k == 2 && p[i + 1].op == '*' && p[i + 1].k == 2;
        if (pair) { ++i; continue; }
        out.push_back(p[i]);
    }
    return out;
}

int main() {
    const Program original = {
        {-1, '+', 7}, {-1, '/', 2}, {5, '+', 1}, {-1, '*', 2}, {9, '+', 4},
    };
    const int input = 3;

    std::vector<bool> executed(original.size(), false);
    const int expected = run(original, input, &executed);
    std::vector<std::size_t> dead;  // statements the input never reached
    for (std::size_t i = 0; i < original.size(); ++i)
        if (!executed[i]) dead.push_back(i);
    std::cout << "input " << input << ": x = " << expected << ", statements never run:";
    for (std::size_t i : dead) std::cout << " s" << i;
    std::cout << "\n";

    // Each bit of `mask` says whether to delete one of the unexecuted statements.
    for (unsigned mask = 0; mask < (1u << dead.size()); ++mask) {
        Program variant;
        std::string deleted;
        for (std::size_t i = 0, d = 0; i < original.size(); ++i) {
            bool isDead = d < dead.size() && dead[d] == i;
            bool drop = isDead && (mask >> d & 1u);
            if (isDead) ++d;
            if (drop) deleted += " s" + std::to_string(i);
            else variant.push_back(original[i]);
        }
        int got = run(optimize(variant), input);
        std::cout << "variant deleting" << (deleted.empty() ? " nothing" : deleted) << ": optimized x = "
                  << got << (got == expected ? "" : "  <- miscompiled") << "\n";
    }
}

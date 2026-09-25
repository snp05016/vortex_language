// Test-case reduction by deleting lines. A candidate counts as "interesting"
// only if it is still a valid program AND it still shows the mismatch.
// Deleting lines one at a time, repeated until a whole pass removes nothing,
// leaves a program from which no single line can go (1-minimal).
// Follows: Zeller and Hildebrandt, TSE 2002; Regehr et al., PLDI 2012.
#include <cctype>
#include <cstddef>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using Program = std::vector<std::string>;
enum class Outcome { Interesting, Boring, Invalid };

// Lines are "v = x", "v = x op y" or "print v"; x and y are names or integer literals.
// Returns Invalid when a line reads a name no earlier line assigned.
Outcome test(const Program& lines) {
    std::map<std::string, long> ref, opt;  // one run without, one with x / 2 -> x >> 1
    std::string outRef, outOpt;
    auto value = [](std::map<std::string, long>& env, const std::string& t, bool& ok) {
        if (std::isdigit(static_cast<unsigned char>(t.back()))) return std::stol(t);
        if (!env.contains(t)) ok = false;
        return env[t];
    };
    for (const std::string& line : lines) {
        std::istringstream in(line);
        std::string a, b, x, op, y;
        in >> a >> b;
        bool ok = true;
        if (a == "print") {
            outRef += std::to_string(value(ref, b, ok)) + " ";
            outOpt += std::to_string(value(opt, b, ok)) + " ";
        } else {
            in >> x >> op >> y;
            for (int pass = 0; pass < 2; ++pass) {
                auto& env = pass == 0 ? ref : opt;
                long l = value(env, x, ok), r = op.empty() ? 0 : value(env, y, ok);
                long v = op.empty() ? l : op == "+" ? l + r : op == "-" ? l - r : op == "*" ? l * r
                       : (pass == 1 && r == 2) ? l >> 1 : l / r;
                env[a] = v;
            }
        }
        if (!ok) return Outcome::Invalid;
    }
    return outRef != outOpt ? Outcome::Interesting : Outcome::Boring;
}

int main() {
    Program p = {
        "a = 5", "b = a - 8", "d = 4", "e = d * 3", "print e",
        "c = b / 2", "f = e / 2", "print c", "print f",
    };
    std::cout << "start: " << p.size() << " lines, "
              << (test(p) == Outcome::Interesting ? "mismatch" : "no mismatch") << "\n";
    for (int pass = 1;; ++pass) {
        std::cout << "pass " << pass << ":";
        bool removed = false;
        for (std::size_t i = 0; i < p.size();) {
            Program candidate = p;
            candidate.erase(candidate.begin() + static_cast<std::ptrdiff_t>(i));
            Outcome o = test(candidate);
            if (o == Outcome::Interesting) {
                std::cout << " -[" << p[i] << "]";
                p = candidate;
                removed = true;  // do not advance: the next line moved into slot i
            } else {
                std::cout << (o == Outcome::Invalid ? " invalid" : " needed");
                ++i;
            }
        }
        std::cout << "\n";
        if (!removed) break;
    }
    std::cout << "result, " << p.size() << " lines:\n";
    for (const std::string& line : p) std::cout << "  " << line << "\n";
}

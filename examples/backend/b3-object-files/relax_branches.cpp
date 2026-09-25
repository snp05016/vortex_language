#include <cstdio>
#include <map>
#include <string>
#include <vector>

// Branch relaxation on a toy with x86-64's two conditional-jump sizes: a
// short form with a signed 8-bit displacement (2 bytes) and a near form with
// a signed 32-bit one (6 bytes). The displacement counts from the end of the
// jump. Every jump starts short; each round lays the code out again and
// widens any jump whose target is out of reach. Jumps only ever grow, so the
// loop stops after at most one round per jump plus one.
//
// Follows: Lattner, "Intro to the LLVM MC Project", section "Assembler
// Backend" (relaxation and branch shortening).

struct Item {
    std::string label;    // defines this label at the item's start, or ""
    std::string target;   // a jump's target label; "" for filler bytes
    int filler = 0;       // size of filler bytes
    bool near = false;    // has this jump been widened?
    int size() const { return target.empty() ? filler : (near ? 6 : 2); }
};

int main() {
    std::vector<Item> code = {
        {"",   "Lx", 0, false},   // jump 0
        {"",   "",   60, false},
        {"",   "Ly", 0, false},   // jump 1
        {"",   "",   64, false},
        {"Lx", "",   70, false},
        {"Ly", "",   0, false},
    };

    for (int round = 1;; ++round) {
        std::map<std::string, int> addr;   // lay out with the current sizes
        std::vector<int> start;
        int pc = 0;
        for (const Item &it : code) {
            if (!it.label.empty()) addr[it.label] = pc;
            start.push_back(pc);
            pc += it.size();
        }
        std::printf("round %d: %d bytes\n", round, pc);
        bool changed = false;
        for (std::size_t i = 0; i < code.size(); ++i) {
            Item &it = code[i];
            if (it.target.empty()) continue;
            int disp = addr.at(it.target) - (start[i] + it.size());
            bool fits = it.near || (disp >= -128 && disp <= 127);
            std::printf("  jump at %3d to %s: disp %d, %s\n", start[i],
                        it.target.c_str(), disp,
                        fits ? (it.near ? "near" : "short") : "too far, widen");
            if (!fits) { it.near = true; changed = true; }
        }
        if (!changed) break;
    }
    return 0;
}

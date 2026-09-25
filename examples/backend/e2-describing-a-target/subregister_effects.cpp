// A toy register file described the way a target description describes one:
// each register lists its subregisters, and nothing else. From that tree
// alone the program derives register units (the leaves, which share no
// storage) and then answers, for a write to one register, what happens to
// every other register: untouched, fully redefined, or only partly changed.
// It assumes each register is exactly the sum of its subregisters (LLVM's
// CoveredBySubRegs); a register with bits of its own needs more than units.
// Follows: LLVM 18.1.8 Target.td (class Register, field SubRegs) and
// MCRegisterInfo::regsOverlap, which compares registers by shared units.

#include <array>
#include <cstdint>
#include <iostream>
#include <string_view>

struct Reg {
    std::string_view name;
    std::array<int, 2> subregs;  // indexes into kRegs, -1 for none
};

// One 32-bit register R with halves RH and RL; RL splits into two bytes.
constexpr std::array<Reg, 5> kRegs = {{
    {"R", {1, 2}},
    {"RH", {-1, -1}},
    {"RL", {3, 4}},
    {"RL_HI", {-1, -1}},
    {"RL_LO", {-1, -1}},
}};

// A register with no subregisters is one unit of storage. Every other
// register is the union of its subregisters' units.
std::uint32_t units(int r, int& next_unit, std::array<std::uint32_t, 5>& memo) {
    if (memo[r] != 0) return memo[r];
    std::uint32_t mask = 0;
    for (int s : kRegs[r].subregs)
        if (s >= 0) mask |= units(s, next_unit, memo);
    if (mask == 0) mask = 1u << next_unit++;
    return memo[r] = mask;
}

std::string_view effect(std::uint32_t written, std::uint32_t other) {
    if ((written & other) == 0) return "untouched";
    if ((written & other) == other) return "fully redefined";
    return "partly changed";  // shares a unit, keeps a unit it had
}

int main() {
    std::array<std::uint32_t, 5> memo{};
    int next_unit = 0;
    for (int r = 0; r < 5; ++r) units(r, next_unit, memo);

    std::cout << "units: " << next_unit << '\n';
    for (int r = 0; r < 5; ++r) {
        std::cout << "  " << kRegs[r].name << " = {";
        std::string_view sep = "";
        for (int u = 0; u < next_unit; ++u)
            if (memo[r] & (1u << u)) {
                std::cout << sep << 'u' << u;
                sep = ", ";
            }
        std::cout << "}\n";
    }

    for (int w : {4, 2}) {  // write RL_LO, then RL
        std::cout << "write " << kRegs[w].name << '\n';
        for (int r = 0; r < 5; ++r)
            if (r != w)
                std::cout << "  " << kRegs[r].name << ": "
                          << effect(memo[w], memo[r]) << '\n';
    }
}

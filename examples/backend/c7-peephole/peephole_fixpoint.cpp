// A small peephole optimizer over straight-line AArch64-style code: five
// rules, each looking at one or two adjacent instructions, applied until no
// rule matches anywhere (a fixpoint). Two rules have a side condition: the
// register they stop writing must be dead afterwards. The same code is
// optimized twice, once with only x3 live at the end and once with x2 live
// too, to show a side condition refusing a rewrite that looks local.
//
// Follows: McKeeman, "Peephole optimization", CACM 1965 (rewriting a few
// adjacent instructions at the end of compilation); Davidson and Fraser,
// TOPLAS 1980 (replacing a pair of adjacent instructions with one cheaper
// instruction); Arm A64 ISA guide, "Loads and stores - load pair and store
// pair".

#include <cstdio>
#include <set>
#include <string>
#include <vector>

// op is one of: str ldr mov movi mul lsl add stp. r holds register names,
// destination first; imm is an offset, a constant or a shift amount.
struct Ins {
    std::string op;
    std::vector<std::string> r;
    long imm = 0;
};

void print(const Ins& in) {
    const auto& r = in.r;
    if (in.op == "str" || in.op == "ldr")
        std::printf("    %s %s, [%s, #%ld]\n", in.op.c_str(), r[0].c_str(), r[1].c_str(), in.imm);
    else if (in.op == "stp")
        std::printf("    stp %s, %s, [%s, #%ld]\n", r[0].c_str(), r[1].c_str(), r[2].c_str(), in.imm);
    else if (in.op == "movi") std::printf("    mov %s, #%ld\n", r[0].c_str(), in.imm);
    else if (in.op == "mov") std::printf("    mov %s, %s\n", r[0].c_str(), r[1].c_str());
    else if (in.op == "lsl") std::printf("    lsl %s, %s, #%ld\n", r[0].c_str(), r[1].c_str(), in.imm);
    else if (in.op == "add" && in.imm > 0)
        std::printf("    add %s, %s, %s, lsl #%ld\n", r[0].c_str(), r[1].c_str(), r[2].c_str(), in.imm);
    else std::printf("    %s %s, %s, %s\n", in.op.c_str(), r[0].c_str(), r[1].c_str(), r[2].c_str());
}

// Which registers an instruction writes and reads. Stores write memory only.
std::vector<std::string> defs(const Ins& in) {
    if (in.op == "str" || in.op == "stp") return {};
    return {in.r[0]};
}
std::vector<std::string> uses(const Ins& in) {
    if (in.op == "str" || in.op == "stp") return in.r;
    return {in.r.begin() + 1, in.r.end()};
}

// The side condition: is `reg` dead after position `at`? Walk forward; a read
// before any write means live. At the end, the live-out set decides.
bool dead_after(const std::vector<Ins>& p, std::size_t at, const std::string& reg,
                const std::set<std::string>& live_out) {
    for (std::size_t j = at + 1; j < p.size(); ++j) {
        for (const auto& u : uses(p[j])) if (u == reg) return false;
        for (const auto& d : defs(p[j])) if (d == reg) return true;
    }
    return !live_out.contains(reg);
}

int log2_exact(long c) {  // -1 unless c is a power of two
    int k = 0;
    while (c > 1 && c % 2 == 0) { c /= 2; ++k; }
    return c == 1 ? k : -1;
}

// Try every rule at position i. Returns the rule's name, or nullptr.
const char* try_rules(std::vector<Ins>& p, std::size_t i, const std::set<std::string>& live_out) {
    Ins& a = p[i];
    if (a.op == "mov" && a.r[0] == a.r[1]) {  // R1: a copy onto itself does nothing
        p.erase(p.begin() + static_cast<long>(i));
        return "R1 delete self-move";
    }
    if (i + 1 >= p.size()) return nullptr;
    Ins& b = p[i + 1];
    // R2: a load of the slot just stored reads the stored register's value.
    if (a.op == "str" && b.op == "ldr" && a.r[1] == b.r[1] && a.imm == b.imm) {
        b = Ins{"mov", {b.r[0], a.r[0]}};
        return "R2 forward store to load";
    }
    // R3: multiply by a power of two held in a register that dies here.
    if (a.op == "movi" && b.op == "mul" && b.r[2] == a.r[0] && b.r[1] != a.r[0] &&
        log2_exact(a.imm) >= 0 && dead_after(p, i + 1, a.r[0], live_out)) {
        Ins shift{"lsl", {b.r[0], b.r[1]}, log2_exact(a.imm)};
        p[i] = shift;
        p.erase(p.begin() + static_cast<long>(i) + 1);
        return "R3 multiply becomes shift";
    }
    // R4: fold a shift into the add that reads it, if nothing else reads it.
    if (a.op == "lsl" && b.op == "add" && b.imm == 0 && b.r[2] == a.r[0] && b.r[1] != a.r[0] &&
        dead_after(p, i + 1, a.r[0], live_out)) {
        Ins fused{"add", {b.r[0], b.r[1], a.r[1]}, a.imm};
        p[i] = fused;
        p.erase(p.begin() + static_cast<long>(i) + 1);
        return "R4 fold shift into add";
    }
    // R5: two 8-byte stores to neighbouring slots off one base become stp,
    // if the offset fits stp's field: a multiple of 8 in [-512, 504].
    if (a.op == "str" && b.op == "str" && a.r[1] == b.r[1] && b.imm == a.imm + 8 &&
        a.imm % 8 == 0 && a.imm >= -512 && a.imm <= 504) {
        Ins pair{"stp", {a.r[0], b.r[0], a.r[1]}, a.imm};
        p[i] = pair;
        p.erase(p.begin() + static_cast<long>(i) + 1);
        return "R5 pair two stores";
    }
    return nullptr;
}

void optimize(std::vector<Ins> p, const std::set<std::string>& live_out) {
    std::printf("Live at the end:");
    for (const auto& r : live_out) std::printf(" %s", r.c_str());
    std::printf("\n");
    for (bool changed = true; changed;) {  // restart from the top after every rewrite
        changed = false;
        for (std::size_t i = 0; i < p.size() && !changed; ++i) {
            if (const char* rule = try_rules(p, i, live_out)) {
                std::printf("  at %zu: %s\n", i, rule);
                changed = true;
            }
        }
    }
    std::printf("  result, %zu instructions:\n", p.size());
    for (const auto& in : p) print(in);
}

int main() {
    // What a naive code generator might emit for a[i] = v; a[i + 1] = w;
    // with i spilled and reloaded, and a leftover copy between the stores.
    const std::vector<Ins> code = {
        {"str", {"x1", "sp"}, 16},  {"ldr", {"x1", "sp"}, 16},   {"movi", {"x9"}, 8},
        {"mul", {"x2", "x1", "x9"}}, {"add", {"x3", "x0", "x2"}}, {"str", {"x4", "x3"}, 0},
        {"mov", {"x5", "x5"}},       {"str", {"x6", "x3"}, 8},
    };
    std::printf("Before, %zu instructions:\n", code.size());
    for (const auto& in : code) print(in);
    optimize(code, {"x3"});
    optimize(code, {"x2", "x3"});  // now something later reads i * 8
}

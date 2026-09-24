// Follows: ARM-software/abi-aa, AAPCS64, section 6.8.2 "Parameter Passing
// Rules". <https://github.com/ARM-software/abi-aa/blob/main/aapcs64/aapcs64.rst>
// Follows: x86-64-ABI (SysV AMD64 psABI), section 3.2.3 "Parameter Passing".
// <https://gitlab.com/x86-psABIs/x86-64-ABI>
//
// A minimal model of how a calling convention assigns scalar arguments to
// registers. Real ABIs also classify aggregates (structs, small arrays)
// field by field; this keeps to the two scalar kinds a Vortex v0.1
// argument is at the machine level, once references have become
// addresses: a general-purpose value, or a floating-point value.

#include <cstdio>
#include <string>
#include <utility>
#include <vector>

enum class Kind { GP, FP };

struct Convention {
    const char* name;
    int gp_regs;
    int fp_regs;
    const char* gp_names[8];
    const char* fp_names[8];
};

void classify(const Convention& abi,
              const std::vector<std::pair<Kind, const char*>>& args) {
    int gp_used = 0, fp_used = 0, stack_bytes = 0;
    std::printf("%s:\n", abi.name);
    for (const auto& [kind, label] : args) {
        char slot[16];
        if (kind == Kind::GP && gp_used < abi.gp_regs) {
            std::snprintf(slot, sizeof slot, "%s", abi.gp_names[gp_used++]);
        } else if (kind == Kind::FP && fp_used < abi.fp_regs) {
            std::snprintf(slot, sizeof slot, "%s", abi.fp_names[fp_used++]);
        } else {
            std::snprintf(slot, sizeof slot, "[sp+%d]", stack_bytes);
            stack_bytes += 8;
        }
        std::printf("  %-6s -> %s\n", label, slot);
    }
}

int main() {
    Convention aapcs64{"AAPCS64", 8, 8,
        {"x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7"},
        {"v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7"}};
    Convention sysv{"SysV AMD64", 6, 8,
        {"rdi", "rsi", "rdx", "rcx", "r8", "r9", "", ""},
        {"xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7"}};

    // A hypothetical call with the shape of a Vortex kernel: three array
    // addresses, two f32 scale factors, then five more integer values.
    // Eight general-purpose arguments in total: AAPCS64's eight
    // general-purpose registers hold all of them, but SysV AMD64 has only
    // six, so the last two spill to the stack.
    std::vector<std::pair<Kind, const char*>> args = {
        {Kind::GP, "a"}, {Kind::GP, "b"}, {Kind::GP, "c"},
        {Kind::FP, "alpha"}, {Kind::FP, "beta"},
        {Kind::GP, "p0"}, {Kind::GP, "p1"}, {Kind::GP, "p2"},
        {Kind::GP, "p3"}, {Kind::GP, "p4"},
    };

    classify(aapcs64, args);
    classify(sysv, args);
}

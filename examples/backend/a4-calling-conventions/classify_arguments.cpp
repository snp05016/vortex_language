// Follows: Arm, AAPCS64, "Parameter passing rules" (stages A to C).
// Follows: Apple, "Writing ARM64 code for Apple platforms",
//   "Pass arguments to functions correctly".
// Follows: x86-64 psABI, "Parameter Passing".
//
// Assigns the scalar arguments of a C call to registers and stack slots under
// three conventions. Scalars only: integers, pointers, float and double.
#include <cstdio>
#include <initializer_list>
#include <vector>

struct Arg { const char* name; bool is_float; int size; };

struct Convention {
    const char* title;
    std::vector<const char*> gp, fp;  // argument registers, in order
    bool packs_stack;                 // Apple: natural size and alignment
};

int round_up(int n, int to) { return (n + to - 1) / to * to; }

void assign(const Convention& cc, const std::vector<Arg>& args) {
    std::size_t next_gp = 0, next_fp = 0;  // two counters, one per bank
    int next_stack = 0;                   // offset from sp at the call
    std::printf("%s:", cc.title);
    for (const Arg& a : args) {
        auto& bank = a.is_float ? cc.fp : cc.gp;
        std::size_t& next = a.is_float ? next_fp : next_gp;
        if (next < bank.size()) {
            std::printf(" %s=%s", a.name, bank[next++]);
            continue;
        }
        // Standard rule: every stack argument takes at least one 8-byte slot.
        int slot = cc.packs_stack ? a.size : round_up(a.size, 8);
        next_stack = round_up(next_stack, cc.packs_stack ? a.size : 8);
        std::printf(" %s=[sp+%d]", a.name, next_stack);
        next_stack += slot;
    }
    std::printf("\n  stack bytes %d, area reserved %d\n", next_stack,
                round_up(next_stack, 16));
}

int main() {
    std::vector<const char*> x = {"x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7"};
    std::vector<const char*> v = {"v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7"};
    Convention aapcs{"AAPCS64", x, v, false};
    Convention apple{"Apple arm64", x, v, true};
    Convention sysv{"SysV AMD64", {"rdi", "rsi", "rdx", "rcx", "r8", "r9"},
                    {"xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6",
                     "xmm7"}, false};

    // take10(int a0, ..., int a9): ten 4-byte integers.
    std::vector<Arg> ten;
    const char* names[] = {"a0", "a1", "a2", "a3", "a4",
                           "a5", "a6", "a7", "a8", "a9"};
    for (const char* n : names) ten.push_back({n, false, 4});

    // A kernel signature: three pointers, two floats, three ints, two longs.
    std::vector<Arg> kernel = {
        {"A", false, 8}, {"B", false, 8}, {"C", false, 8},
        {"alpha", true, 4}, {"beta", true, 4},
        {"m", false, 4}, {"n", false, 4}, {"k", false, 4},
        {"lda", false, 8}, {"ldb", false, 8}};

    for (const auto* args : {&ten, &kernel}) {
        for (const Convention* cc : {&aapcs, &apple, &sysv}) assign(*cc, *args);
        std::printf("\n");
    }
}

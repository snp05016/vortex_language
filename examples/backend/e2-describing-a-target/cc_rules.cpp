// A calling convention as data: an ordered list of rules, each "if the
// argument has this type, take the first free register from this list".
// One small interpreter walks the arguments and the rules. There are no
// counters anywhere: w0 and x0 are two names for one register, so taking
// w0 also uses up x0, and that is what makes the next i64 land in x1.
// The four rules are a subset of LLVM 18.1.8's AArch64 rules, reduced to
// scalars; the last rule sends anything left over to 8-byte stack slots.
// Follows: LLVM 18.1.8 AArch64CallingConvention.td and CCState::AllocateReg.

#include <array>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

enum class Type { I32, I64, F32, F64 };

struct Rule {
    Type type;
    char prefix;  // register name prefix: w, x, s or d
    int bank;     // 0 = general-purpose, 1 = floating-point
};

constexpr std::array<Rule, 4> kRules = {{
    {Type::I32, 'w', 0},
    {Type::I64, 'x', 0},
    {Type::F32, 's', 1},
    {Type::F64, 'd', 1},
}};

std::vector<std::string> place(const std::vector<Type>& args) {
    std::array<std::array<bool, 8>, 2> used{};  // one flag per physical register
    int stack = 0;
    std::vector<std::string> where;
    for (Type t : args) {
        std::string loc;
        for (const Rule& r : kRules) {
            if (r.type != t) continue;
            for (int i = 0; i < 8 && loc.empty(); ++i)
                if (!used[r.bank][i]) {
                    used[r.bank][i] = true;  // marks every name of register i
                    loc = r.prefix + std::to_string(i);
                }
            break;  // the first rule whose type matches decides
        }
        if (loc.empty()) {
            loc = "stack+" + std::to_string(stack);
            stack += 8;
        }
        where.push_back(loc);
    }
    return where;
}

void show(std::string_view label, const std::vector<Type>& args) {
    std::cout << label << ':';
    for (const std::string& loc : place(args)) std::cout << ' ' << loc;
    std::cout << '\n';
}

int main() {
    using enum Type;
    show("mix(i32, f32, i64, f64, i32)", {I32, F32, I64, F64, I32});
    show("nine i64, then f32", {I64, I64, I64, I64, I64, I64, I64, I64, I64, F32});
}

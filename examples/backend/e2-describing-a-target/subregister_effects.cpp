// A toy register file, standing in for TableGen's SubRegIndex mechanism:
// every named "register" below is really the same 32 bits of storage, seen
// through a different bit range. A backend needs to know, for any pair of
// them, whether writing one leaves the other alone, fully replaces it, or
// only partly disturbs it. A real target states this once, as data, in its
// register description; every later pass (liveness, register allocation,
// the assembly printer) reads the same table instead of re-deriving it.
// Follows: LLVM TableGen Backends, "Target-Independent Code Generator"
// section. https://llvm.org/docs/TableGen/BackEnds.html

#include <array>
#include <iostream>
#include <string_view>

struct RegisterView {
    std::string_view name;
    unsigned lo;  // first bit this view covers, inclusive
    unsigned hi;  // last bit, exclusive
};

// One 32-bit root register R, described only through the ranges its named
// views cover. RL_HI and RL_LO are non-overlapping halves of RL, the way a
// TableGen SubRegIndex composes: RL_LO is a subregister of RL, which is
// itself a subregister of R.
constexpr std::array<RegisterView, 5> kViews = {{
    {"R", 0, 32},
    {"RH", 16, 32},
    {"RL", 0, 16},
    {"RL_HI", 8, 16},
    {"RL_LO", 0, 8},
}};

enum class Effect { Unaffected, FullyDefined, PartlyDefined };

constexpr Effect classify(const RegisterView& written, const RegisterView& other) {
    if (written.hi <= other.lo || other.hi <= written.lo) {
        return Effect::Unaffected;
    }
    if (written.lo <= other.lo && other.hi <= written.hi) {
        return Effect::FullyDefined;
    }
    return Effect::PartlyDefined;
}

std::string_view describe(Effect effect) {
    switch (effect) {
        case Effect::Unaffected:
            return "unaffected";
        case Effect::FullyDefined:
            return "fully defined";
        case Effect::PartlyDefined:
            return "partly defined";
    }
    return "?";
}

void report(const RegisterView& written) {
    std::cout << "writing " << written.name << " (bits " << written.lo << ".."
              << written.hi << ")\n";
    for (const auto& view : kViews) {
        if (view.name == written.name) {
            continue;
        }
        std::cout << "  " << view.name << ": " << describe(classify(written, view))
                  << '\n';
    }
}

int main() {
    report(kViews[4]);  // RL_LO
    report(kViews[1]);  // RH
}

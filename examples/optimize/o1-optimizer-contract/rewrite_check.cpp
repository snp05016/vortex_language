// Checks integer rewrite rules against every 8-bit input, under three
// meanings of overflow: the result wraps around (LLVM's plain add), overflow
// is undefined (C's signed int, LLVM's add nsw), or overflow is checked and
// stops the program with an error (Vortex).
//
// A rule is correct when, for every input, the rewritten expression does
// something the original allows. A wrapping or checked original allows exactly
// one outcome, and an error is an outcome, so both sides must agree. An input
// on which the original's overflow is undefined allows anything, so it is
// never a counterexample. Eight bits stand in for 32 so that every input can be
// tried; a rule that holds at 8 bits still has to be checked at 32.
//
// Follows: Lopes et al., "Alive2: Bounded Translation Validation for LLVM",
// PLDI 2021, section 1 (refinement), and the LLVM Language Reference (nsw).

#include <cstdint>
#include <cstdio>

enum class Overflow { wrapping, undefined, checked };
const char* const mode_names[] = {"wrapping", "undefined", "checked"};

// 8-bit arithmetic that records whether any step overflowed.
struct Arith {
    Overflow mode;
    bool overflowed = false;
    int fit(int exact) {
        if (exact >= INT8_MIN && exact <= INT8_MAX) return exact;
        if (mode == Overflow::wrapping) return static_cast<std::int8_t>(exact);
        overflowed = true;  // from here on the value no longer matters
        return 0;
    }
    int add(int a, int b) { return fit(a + b); }
    int sub(int a, int b) { return fit(a - b); }
    int mul(int a, int b) { return fit(a * b); }
};

using Expr = int (*)(Arith&, int x, int y, int z);
struct Rule {
    const char* text;
    int inputs;          // 1: x; 2: x and y; 3: x, y and z
    bool boolean;        // the result is a comparison
    bool (*pre)(int x);  // what the optimizer knows about x, or null
    Expr before, after;
};

const Expr grows = [](Arith& a, int x, int, int) { return int(a.add(x, 1) > x); };
const Expr yes = [](Arith&, int, int, int) { return 1; };
const Rule rules[] = {
    {"x + 1 > x  ==>  true", 1, true, nullptr, grows, yes},
    {"x + 1 > x  ==>  true, knowing x < 127", 1, true,
     [](int x) { return x < 127; }, grows, yes},
    {"(x + y) - y  ==>  x", 2, false, nullptr,
     [](Arith& a, int x, int y, int) { return a.sub(a.add(x, y), y); },
     [](Arith&, int x, int, int) { return x; }},
    {"x * 2  ==>  x + x", 1, false, nullptr,
     [](Arith& a, int x, int, int) { return a.mul(x, 2); },
     [](Arith& a, int x, int, int) { return a.add(x, x); }},
    {"(x + y) + z  ==>  x + (y + z)", 3, false, nullptr,
     [](Arith& a, int x, int y, int z) { return a.add(a.add(x, y), z); },
     [](Arith& a, int x, int y, int z) { return a.add(x, a.add(y, z)); }},
};

void show(const char* label, const Rule& r, const Arith& a, int value) {
    std::printf("%s ", label);
    if (a.overflowed) std::printf("%s", a.mode == Overflow::checked ? "error" : "undefined");
    else if (r.boolean) std::printf("%s", value ? "true" : "false");
    else std::printf("%d", value);
}

void check(const Rule& r, Overflow mode) {
    std::printf("  %-10s ", mode_names[int(mode)]);
    const int last_y = r.inputs >= 2 ? INT8_MAX : INT8_MIN;
    const int last_z = r.inputs >= 3 ? INT8_MAX : INT8_MIN;
    for (int x = INT8_MIN; x <= INT8_MAX; ++x)
        for (int y = INT8_MIN; y <= last_y; ++y)
            for (int z = INT8_MIN; z <= last_z; ++z) {
                if (r.pre && !r.pre(x)) continue;
                Arith s{mode, false}, t{mode, false};
                int sv = r.before(s, x, y, z), tv = r.after(t, x, y, z);
                bool ok = mode == Overflow::undefined
                              ? s.overflowed || (!t.overflowed && sv == tv)
                              : s.overflowed == t.overflowed && (s.overflowed || sv == tv);
                if (ok) continue;
                std::printf("wrong at x = %d", x);
                if (r.inputs >= 2) std::printf(", y = %d", y);
                if (r.inputs >= 3) std::printf(", z = %d", z);
                show(":", r, s, sv);
                show(" becomes", r, t, tv);
                std::printf("\n");
                return;
            }
    std::printf("correct\n");
}

int main() {
    for (const Rule& r : rules) {
        std::printf("%s\n", r.text);
        for (int mode = 0; mode < 3; ++mode) check(r, Overflow(mode));
    }
}

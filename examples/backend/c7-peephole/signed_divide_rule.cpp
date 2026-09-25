// Checking a strength-reduction rule on every input before trusting it.
// "Replace x / 2 with an arithmetic shift right by 1" is right for unsigned
// and non-negative values and wrong for negative odd ones: division rounds
// toward zero, the shift rounds toward minus infinity. On 8-bit values the
// whole input space is 256 cases, so the check is a proof for that width.
//
// Follows: Massalin, "Superoptimizer: A Look at the Smallest Program",
// ASPLOS 1987 (test a candidate on inputs before accepting it);
// cppreference, "Arithmetic operators" (integer division truncates toward
// zero; since C++20, right shift of a negative value is arithmetic).

#include <cstdint>
#include <cstdio>

using i8 = std::int8_t;

i8 divide(i8 x) { return static_cast<i8>(x / 2); }

// Candidate 1: shift only.
i8 shift_only(i8 x) { return static_cast<i8>(x >> 1); }

// Candidate 2: add the sign bit first, so a negative odd value moves up by
// one before the shift. The sign bit is x's top bit, read as 0 or 1.
i8 shift_with_fixup(i8 x) {
    int sign = static_cast<std::uint8_t>(x) >> 7;
    return static_cast<i8>((x + sign) >> 1);
}

void check(const char* name, i8 (*candidate)(i8), int lo, int hi) {
    int wrong = 0;
    int first = 0;
    for (int v = lo; v <= hi; ++v) {
        i8 x = static_cast<i8>(v);
        if (candidate(x) != divide(x)) {
            if (wrong++ == 0) first = v;
        }
    }
    std::printf("%-26s inputs %4d..%3d: ", name, lo, hi);
    if (wrong == 0) {
        std::printf("agrees on all %d\n", hi - lo + 1);
    } else {
        i8 x = static_cast<i8>(first);
        std::printf("%d wrong, first x = %d: x / 2 = %d, rule gives %d\n", wrong, first,
                    divide(x), candidate(x));
    }
}

int main() {
    check("x >> 1", shift_only, -128, 127);
    check("x >> 1, when x >= 0", shift_only, 0, 127);  // the rule with a side condition
    check("(x + sign bit) >> 1", shift_with_fixup, -128, 127);
}

// What AArch64's divide instruction does at the two edges Vortex rules on
// (record 33): a zero divisor, and MIN / -1. sdiv never traps, and the
// remainder is built from it with msub (a - q * b), so a missing check
// shows up as a quiet number, not a crash. x86-64's idiv faults on both
// edges instead; the chapter compares the two.
//
// Runs on arm64 only (see the .toml): the inline assembly is A64.

#include <cstdint>
#include <limits>
#include <print>

static std::int32_t hw_div(std::int32_t a, std::int32_t b) {
    std::int32_t q;
    asm("sdiv %w0, %w1, %w2" : "=r"(q) : "r"(a), "r"(b));
    return q;
}

static std::int32_t hw_rem(std::int32_t a, std::int32_t b) {
    std::int32_t q = hw_div(a, b);
    std::int32_t r;
    asm("msub %w0, %w1, %w2, %w3" : "=r"(r) : "r"(q), "r"(b), "r"(a));
    return r;
}

int main() {
    constexpr std::int32_t min = std::numeric_limits<std::int32_t>::min();
    struct Case { std::int32_t a, b; };
    const Case cases[] = {{-7, 2}, {7, -2}, {7, 0}, {min, -1}};
    for (auto [a, b] : cases) {
        std::println("{:>11} / {:>2}: sdiv gives {:>11}, sdiv+msub gives {:>2}",
                     a, b, hw_div(a, b), hw_rem(a, b));
    }
}

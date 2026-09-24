// Floating-point rewrites that hold for real numbers, tried on sample float
// values and compared bit for bit. Each operation rounds once to the nearest
// float (IEEE 754 binary32), as Vortex requires. Any NaN matches any NaN,
// because Vortex's print writes every NaN the same way. std::to_chars prints
// the shortest decimal that reads back as the same float, which for these
// values has the digits Vortex's print chooses, in C++'s layout (-0 and
// 1e+08, where print writes -0.0 and 100000000.0).
// The example is built with -ffp-contract=off: otherwise a C++ compiler may
// fuse x * y + z itself, and the fma rule would appear to hold.
//
// Follows: Goldberg, "What Every Computer Scientist Should Know About
// Floating-Point Arithmetic" (1991), section "Optimizers", and cppreference
// for std::fma and std::to_chars.

#include <bit>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>

using Expr = float (*)(float x, float y, float z);
struct Rule {
    const char* text;
    int inputs;  // 1: x only; 3: x, y and z
    Expr before, after;
};

const float inf = std::numeric_limits<float>::infinity();
const float one_input[] = {0.0f, -0.0f, 1.0f, 9.0f, 0x1p-149f,
                           std::numeric_limits<float>::max(), inf, -inf,
                           std::numeric_limits<float>::quiet_NaN()};
// 1 + 2^-12 and -(1 + 2^-11) make x * y round, and x * y + z cancel.
const float three_inputs[] = {1.0f, 0x1.001p0f, -0x1.002p0f, 1e8f, -1e8f};

const Rule rules[] = {
    {"x + 0.0  ==>  x", 1, [](float x, float, float) { return x + 0.0f; },
     [](float x, float, float) { return x; }},
    {"x + (-0.0)  ==>  x", 1, [](float x, float, float) { return x + -0.0f; },
     [](float x, float, float) { return x; }},
    {"x - x  ==>  0.0", 1, [](float x, float, float) { return x - x; },
     [](float, float, float) { return 0.0f; }},
    {"x / 2.0  ==>  x * 0.5", 1, [](float x, float, float) { return x / 2.0f; },
     [](float x, float, float) { return x * 0.5f; }},
    {"x / 10.0  ==>  x * 0.1", 1, [](float x, float, float) { return x / 10.0f; },
     [](float x, float, float) { return x * 0.1f; }},
    {"(x + y) + z  ==>  x + (y + z)", 3,
     [](float x, float y, float z) { return (x + y) + z; },
     [](float x, float y, float z) { return x + (y + z); }},
    {"(x + y) + z  ==>  the same sum in double, rounded once", 3,
     [](float x, float y, float z) { return (x + y) + z; },
     [](float x, float y, float z) { return float(double(x) + double(y) + double(z)); }},
    {"x * y + z  ==>  fma(x, y, z)", 3,
     [](float x, float y, float z) { return x * y + z; },
     [](float x, float y, float z) { return std::fma(x, y, z); }},
};

std::string text(float v) {
    if (std::isnan(v)) return "NaN";
    char buffer[32];
    return std::string(buffer, std::to_chars(buffer, buffer + sizeof buffer, v).ptr);
}

bool same(float a, float b) {
    if (std::isnan(a) || std::isnan(b)) return std::isnan(a) && std::isnan(b);
    return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b);
}

// Returns false and prints the first counterexample, if there is one.
bool check(const Rule& r, float x, float y, float z) {
    float before = r.before(x, y, z), after = r.after(x, y, z);
    if (same(before, after)) return true;
    std::printf("  wrong at x = %s", text(x).c_str());
    if (r.inputs == 3) std::printf(", y = %s, z = %s", text(y).c_str(), text(z).c_str());
    std::printf(": %s becomes %s\n", text(before).c_str(), text(after).c_str());
    return false;
}

bool check_all(const Rule& r) {
    if (r.inputs == 1) {
        for (float x : one_input)
            if (!check(r, x, 0, 0)) return false;
        return true;
    }
    for (float x : three_inputs)
        for (float y : three_inputs)
            for (float z : three_inputs)
                if (!check(r, x, y, z)) return false;
    return true;
}

int main() {
    for (const Rule& r : rules) {
        std::printf("%s\n", r.text);
        if (check_all(r)) std::printf("  correct on every sample\n");
    }
}

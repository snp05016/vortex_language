#include <cstdint>
#include <cstdio>
#include <cstring>

// Adds v[first..last) in strict left-to-right order: what one thread does
// with its share of k, and what Vortex's strict floating-point rule requires
// of the whole sum.
float ordered_sum(const float* v, int first, int last) {
    float total = 0.0f;
    for (int i = first; i < last; ++i) {
        total += v[i];
    }
    return total;
}

// A pc-style split into `parts` equal slices: each slice gets its own
// zero-initialized partial sum, and the partials are combined afterwards, in
// slice order. Running the slices on threads would not change these bits,
// because the grouping is fixed by the split, not by timing.
float split_sum(const float* v, int n, int parts) {
    float total = 0.0f;
    for (int p = 0; p < parts; ++p) {
        total += ordered_sum(v, p * n / parts, (p + 1) * n / parts);
    }
    return total;
}

// The exact bits of a float, so results are compared bit for bit.
std::uint32_t bits(float f) {
    std::uint32_t b;
    std::memcpy(&b, &f, sizeof(b));
    return b;
}

int main() {
    // 0.1 has no finite binary representation, so almost every addition
    // rounds, and the rounding errors depend on how the terms are grouped.
    constexpr int kTerms = 2048;
    float v[kTerms];
    for (float& x : v) {
        x = 0.1f;
    }

    const float serial = ordered_sum(v, 0, kTerms);
    std::printf("parts  sum        bits      same as serial\n");
    constexpr int kParts[] = {1, 2, 4, 8};
    for (int parts : kParts) {
        const float s = split_sum(v, kTerms, parts);
        std::printf("%5d  %-9.7g  %08x  %s\n", parts, static_cast<double>(s),
                    bits(s), bits(s) == bits(serial) ? "yes" : "no");
    }
    return 0;
}

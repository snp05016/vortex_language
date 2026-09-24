#include <cstdint>
#include <cstdio>
#include <cstring>

// Adds n values in strict left-to-right order: what one thread does with no
// reduction, and what Vortex's strict floating-point rule always does.
float seq_sum(const float* v, int n) {
    float total = 0.0f;
    for (int i = 0; i < n; ++i) {
        total += v[i];
    }
    return total;
}

// The exact bytes of a float, so two results can be compared bit for bit
// instead of with a tolerance.
std::uint32_t bits(float f) {
    std::uint32_t b;
    std::memcpy(&b, &f, sizeof(b));
    return b;
}

int main() {
    // The same repeated term, chosen only so that no partial sum stays exact:
    // 0.1 has no finite binary representation, so every addition rounds.
    constexpr int kTerms = 2048;
    float v[kTerms];
    for (int i = 0; i < kTerms; ++i) {
        v[i] = 0.1f;
    }

    // One thread, no reduction: every term is added in the order it was
    // written, the only order the strict rule allows.
    float sequential = seq_sum(v, kTerms);

    // Two threads, split down the middle, pc-loop style: each sums its own
    // half, then the halves are added. Still every input, still the same
    // mathematical sum, but a different grouping of the additions.
    const int half = kTerms / 2;
    float left = seq_sum(v, half);
    float right = seq_sum(v + half, kTerms - half);
    float split = left + right;

    std::printf("sequential: %.7g (bits %08x)\n", static_cast<double>(sequential),
                bits(sequential));
    std::printf("split:      %.7g (bits %08x)\n", static_cast<double>(split),
                bits(split));
    std::printf("identical:  %s\n", bits(sequential) == bits(split) ? "yes" : "no");
    return 0;
}

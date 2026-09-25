#include <bit>
#include <cstdint>
#include <cstdio>

// Thirty-two f32 values, one per lane of a warp, summed four ways. The exact
// sum is 30: two values of 2^24 and -2^24 cancel, and thirty 1s remain. At
// 2^24, adjacent f32 values are 2 apart, so 2^24 + 1 is a tie that rounds to
// even, back to 2^24: a 1 added next to the large value is lost.
constexpr int n = 32;

float value(int i) {
    if (i == 0) return 16777216.0f;    // 2^24
    if (i == 17) return -16777216.0f;
    return 1.0f;
}

// One running total, left to right: the order a single thread uses.
float sequential(const float* a) {
    float acc = 0.0f;
    for (int i = 0; i < n; ++i) acc = acc + a[i];
    return acc;
}

// Pairs i and i + stride, stride 16, 8, 4, 2, 1: a shared-memory tree.
float tree(const float* a) {
    float buf[n];
    for (int i = 0; i < n; ++i) buf[i] = a[i];
    for (int stride = n / 2; stride >= 1; stride /= 2)
        for (int i = 0; i < stride; ++i) buf[i] = buf[i] + buf[i + stride];
    return buf[0];
}

// Every lane adds the value of lane (lane XOR mask), for mask 16 down to 1,
// as a warp does with xor shuffles. All lanes step at once, so read the old
// values before writing any new ones.
float butterfly(const float* a) {
    float lane[n], next[n];
    for (int i = 0; i < n; ++i) lane[i] = a[i];
    for (int mask = n / 2; mask >= 1; mask /= 2) {
        for (int i = 0; i < n; ++i) next[i] = lane[i] + lane[i ^ mask];
        for (int i = 0; i < n; ++i) lane[i] = next[i];
    }
    for (int i = 1; i < n; ++i)
        if (std::bit_cast<std::uint32_t>(lane[i]) != std::bit_cast<std::uint32_t>(lane[0]))
            return -1.0f;  // never happens: every lane ends with the same bits
    return lane[0];
}

// Four groups of 8 summed left to right, then the four partials in order.
float segmented(const float* a) {
    float partial[4];
    for (int g = 0; g < 4; ++g) {
        float acc = 0.0f;
        for (int i = 0; i < 8; ++i) acc = acc + a[g * 8 + i];
        partial[g] = acc;
    }
    float acc = 0.0f;
    for (int g = 0; g < 4; ++g) acc = acc + partial[g];
    return acc;
}

void show(const char* name, float v) {
    std::printf("%-10s 0x%08x = %g\n", name, std::bit_cast<std::uint32_t>(v), v);
}

int main() {
    float a[n];
    for (int i = 0; i < n; ++i) a[i] = value(i);
    show("sequential", sequential(a));
    show("tree", tree(a));
    show("butterfly", butterfly(a));
    show("segmented", segmented(a));
}

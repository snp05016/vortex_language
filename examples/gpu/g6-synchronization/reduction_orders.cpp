#include <bit>
#include <cstdint>
#include <cstdio>

constexpr int n = 32;

float make(int i) {
    if (i == 0) return 16777216.0f;
    if (i == 17) return -16777216.0f;
    return 1.0f;
}

float sequential(const float *a) {
    float acc = 0.0f;
    for (int i = 0; i < n; ++i) acc = acc + a[i];
    return acc;
}

float tree(const float *a) {
    float buf[n];
    for (int i = 0; i < n; ++i) buf[i] = a[i];
    for (int stride = 16; stride >= 1; stride /= 2) {
        for (int i = 0; i < stride; ++i) buf[i] = buf[i] + buf[i + stride];
    }
    return buf[0];
}

float segmented(const float *a) {
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

void show(const char *name, float v) {
    std::printf("%-10s 0x%08x = %g\n", name, std::bit_cast<std::uint32_t>(v), v);
}

int main() {
    float a[n];
    for (int i = 0; i < n; ++i) a[i] = make(i);
    show("sequential", sequential(a));
    show("tree", tree(a));
    show("segmented", segmented(a));
}

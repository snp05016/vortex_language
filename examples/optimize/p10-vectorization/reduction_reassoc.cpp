// Summing 64 floats three ways, to see what a vectorized reduction does to
// the bits of the answer.
//
// sum_scalar: vectorization forbidden by a loop hint, so the additions run
// one at a time, left to right.
// sum_auto: no hint. On AArch64, Clang's loop vectorizer may widen it only
// with an ordered reduction, which keeps the left-to-right order.
// sum_reassoc: `#pragma clang fp reassociate(on)` grants permission to
// regroup the additions, so the vectorizer keeps one partial sum per lane
// and combines the partial sums at the end.
//
// The input alternates 1.0e7 and 1.0. A float near 2.0e7 has no room for a
// +1 (neighbouring floats there are 2 apart), so the left-to-right sum loses
// every 1.0; partial sums that hold only 1.0s keep them.
//
// Follows: LLVM, "Auto-Vectorization in LLVM", section "Reductions"; Clang
// Language Extensions, "#pragma clang fp reassociate" and
// "#pragma clang loop". The program itself is original.

#include <cstdio>
#include <cstring>

constexpr int N = 64;

__attribute__((noinline)) float sum_scalar(const float* x) {
    float sum = 0.0f;
#pragma clang loop vectorize(disable) interleave(disable)
    for (int i = 0; i < N; ++i) sum += x[i];
    return sum;
}

__attribute__((noinline)) float sum_auto(const float* x) {
    float sum = 0.0f;
    for (int i = 0; i < N; ++i) sum += x[i];
    return sum;
}

__attribute__((noinline)) float sum_reassoc(const float* x) {
#pragma clang fp reassociate(on)
    float sum = 0.0f;
    for (int i = 0; i < N; ++i) sum += x[i];
    return sum;
}

static unsigned bits(float f) {
    unsigned u;
    std::memcpy(&u, &f, sizeof u);
    return u;
}

static void show(const char* name, float value, float reference) {
    std::printf("%-12s %.1f  bits %08x  same bits as scalar: %s\n", name, value, bits(value),
                bits(value) == bits(reference) ? "yes" : "no");
}

int main() {
    float x[N];
    for (int i = 0; i < N; ++i) x[i] = (i % 2 == 0) ? 1.0e7f : 1.0f;

    const float scalar = sum_scalar(x);
    show("sum_scalar", scalar, scalar);
    show("sum_auto", sum_auto(x), scalar);
    show("sum_reassoc", sum_reassoc(x), scalar);
    std::printf("exact sum    %.1f\n", 32.0 * 1.0e7 + 32.0);
    return 0;
}

// Summing 64 floats three ways, to show what a reduction costs a vectorizer.
//
// sum_scalar never vectorizes (the pragma forbids it): a plain left-to-right
// running total, one add at a time.
//
// sum_auto is the same loop with no pragma at all. At -O2, on an AArch64
// target Clang's vectorizer can still widen it, but only by using an
// "ordered" reduction, one that keeps the same left-to-right rounding as the
// scalar loop instead of reassociating it (LLVM's docs describe this AArch64
// and RISC-V case: ordered reductions "preserve the exact result").
//
// sum_reassoc adds `#pragma clang fp reassociate(on)`, which lets the
// vectorizer group the additions by SIMD lane instead of by position, the
// usual (faster) reduction. That regrouping can change the rounding.
//
// With inputs chosen to stress rounding (alternating a large and a small
// magnitude), sum_auto agrees with sum_scalar bit for bit; sum_reassoc does
// not. Run with -O2 and no -ffast-math: the results below are what this
// program actually computes, not a claim about any other compiler or flags.
//
// Follows: LLVM, "Auto-Vectorization in LLVM", the paragraph on ordered
// floating-point reductions on AArch64 and RISC-V; Clang User's Manual, the
// `#pragma clang fp reassociate` pragma.

#include <cstdio>
#include <cstring>

constexpr int N = 64;

__attribute__((noinline)) float sum_scalar(const float x[N]) {
    float sum = 0.0f;
#pragma clang loop vectorize(disable) interleave(disable)
    for (int i = 0; i < N; ++i) {
        sum += x[i];
    }
    return sum;
}

__attribute__((noinline)) float sum_auto(const float x[N]) {
    float sum = 0.0f;
    for (int i = 0; i < N; ++i) {
        sum += x[i];
    }
    return sum;
}

__attribute__((noinline)) float sum_reassoc(const float x[N]) {
#pragma clang fp reassociate(on)
    float sum = 0.0f;
    for (int i = 0; i < N; ++i) {
        sum += x[i];
    }
    return sum;
}

static unsigned bits_of(float f) {
    unsigned u;
    std::memcpy(&u, &f, sizeof(u));
    return u;
}

int main() {
    float x[N];
    for (int i = 0; i < N; ++i) {
        x[i] = (i % 2 == 0) ? 1.0e7f : 1.0f;
    }

    float scalar = sum_scalar(x);
    float autov = sum_auto(x);
    float reassoc = sum_reassoc(x);

    std::printf("sum_scalar   = %08x\n", bits_of(scalar));
    std::printf("sum_auto     = %08x  same_as_scalar=%s\n", bits_of(autov), autov == scalar ? "yes" : "no");
    std::printf("sum_reassoc  = %08x  same_as_scalar=%s\n", bits_of(reassoc), reassoc == scalar ? "yes" : "no");
    return 0;
}

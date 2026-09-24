// saxpy (y[i] = a*x[i] + y[i]) computed two ways: a plain scalar loop, and an
// explicit AArch64 NEON version that processes four lanes at a time.
//
// Each y[i] is independent: no lane's result feeds another lane, so nothing
// is reassociated. The two loops must therefore agree bit for bit, which the
// program checks itself. This is the "vectorize along independent output
// elements" case; compare it with reduction_reassoc.cpp, where the vectorized
// loop combines lanes into one running total and the order does matter.
//
// Follows: Arm, "Optimizing C code with Neon intrinsics" (the load, FMA,
// store shape, applied here to a different problem: saxpy, not a matmul
// tile); Arm Intrinsics reference for vld1q_f32, vfmaq_f32 and vst1q_f32.

#include <arm_neon.h>
#include <cstdio>

constexpr int N = 16;  // a multiple of the 4-lane width, so there is no epilogue here

void saxpy_scalar(float a, const float x[N], float y[N]) {
    for (int i = 0; i < N; ++i) {
        y[i] = a * x[i] + y[i];
    }
}

void saxpy_neon(float a, const float x[N], float y[N]) {
    float32x4_t va = vdupq_n_f32(a);
    for (int i = 0; i < N; i += 4) {
        float32x4_t vx = vld1q_f32(x + i);
        float32x4_t vy = vld1q_f32(y + i);
        vy = vfmaq_f32(vy, va, vx);  // vy = va*vx + vy, one fused instruction per lane
        vst1q_f32(y + i, vy);
    }
}

int main() {
    float x[N], y_scalar[N], y_neon[N];
    for (int i = 0; i < N; ++i) {
        x[i] = static_cast<float>(i);
        y_scalar[i] = y_neon[i] = static_cast<float>(2 * N - i);
    }
    const float a = 1.5f;
    saxpy_scalar(a, x, y_scalar);
    saxpy_neon(a, x, y_neon);

    bool identical = true;
    for (int i = 0; i < N; ++i) {
        if (y_scalar[i] != y_neon[i]) identical = false;
    }
    std::printf("identical: %s\n", identical ? "yes" : "no");
    for (int i = 0; i < N; ++i) {
        std::printf("y[%d] = %.1f\n", i, y_neon[i]);
    }
    return 0;
}

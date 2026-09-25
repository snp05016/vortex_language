// y[i] = a*x[i] + y[i] over 16 floats, three ways: a scalar loop with every
// operation rounded on its own, a NEON loop that keeps the multiply and the
// add separate (vmulq_f32, then vaddq_f32), and a NEON loop that fuses them
// (vfmaq_f32, one rounding instead of two).
//
// Each y[i] depends only on its own x[i] and y[i], so putting four of them in
// four lanes changes no rounding: the separate-ops version must match the
// scalar loop bit for bit. The fused version changes what one element
// computes, lane or no lane, so it may differ, and here it does.
//
// Follows: Arm, "Optimizing C code with Neon intrinsics" (the load, compute,
// store shape; applied here to saxpy, not a matrix tile); Arm Intrinsics
// reference for vld1q_f32, vdupq_n_f32, vmulq_f32, vaddq_f32, vfmaq_f32 and
// vst1q_f32; Clang Language Extensions, "#pragma clang fp contract".

#include <arm_neon.h>
#include <cstdio>
#include <cstring>

constexpr int N = 16;  // a multiple of 4, so no scalar remainder is needed

void saxpy_scalar(float a, const float* x, float* y) {
#pragma clang fp contract(off)  // round a*x[i], then round the sum
#pragma clang loop vectorize(disable)
    for (int i = 0; i < N; ++i) y[i] = a * x[i] + y[i];
}

void saxpy_neon(float a, const float* x, float* y) {
    const float32x4_t va = vdupq_n_f32(a);  // the same a in all four lanes
    for (int i = 0; i < N; i += 4) {
        float32x4_t prod = vmulq_f32(va, vld1q_f32(x + i));
        vst1q_f32(y + i, vaddq_f32(prod, vld1q_f32(y + i)));
    }
}

void saxpy_neon_fused(float a, const float* x, float* y) {
    const float32x4_t va = vdupq_n_f32(a);
    for (int i = 0; i < N; i += 4) {
        // vfmaq_f32(acc, p, q) = acc + p*q with a single rounding.
        vst1q_f32(y + i, vfmaq_f32(vld1q_f32(y + i), va, vld1q_f32(x + i)));
    }
}

static unsigned bits(float f) {
    unsigned u;
    std::memcpy(&u, &f, sizeof u);
    return u;
}

int main() {
    const float a = 0.1f;
    float x[N], y0[N], y1[N], y2[N];
    for (int i = 0; i < N; ++i) {
        x[i] = 1.0f + 0.3f * static_cast<float>(i);
        y0[i] = y1[i] = y2[i] = -0.1f * static_cast<float>(i);
    }
    saxpy_scalar(a, x, y0);
    saxpy_neon(a, x, y1);
    saxpy_neon_fused(a, x, y2);

    int differ_separate = 0, differ_fused = 0;
    for (int i = 0; i < N; ++i) {
        if (bits(y1[i]) != bits(y0[i])) ++differ_separate;
        if (bits(y2[i]) != bits(y0[i])) ++differ_fused;
    }
    std::printf("neon mul+add: %d of %d elements differ from scalar\n", differ_separate, N);
    std::printf("neon fused:   %d of %d elements differ from scalar\n", differ_fused, N);
    for (int i = 0; i < 4; ++i) {
        std::printf("y[%d]  scalar %08x  mul+add %08x  fused %08x\n", i, bits(y0[i]),
                    bits(y1[i]), bits(y2[i]));
    }
    return 0;
}

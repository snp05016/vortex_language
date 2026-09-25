// Two schedules of one basic block, written by hand in AArch64 assembly:
// the squared length x*x + y*y + z*z of a 3-vector of floats, summed left
// to right. len2_source keeps the order of the source; len2_listed starts
// all three loads first, as a list scheduler does. Both run the same nine
// instructions on the same inputs, so they must return the same bits; the
// C++ reference computes the same sum with no contraction into a fused
// multiply-add (the .toml passes -ffp-contract=off).
//
// Arguments: x0 points at the three floats, x1 at the result.
// s0 to s2 are scratch registers the function may overwrite.

#if !defined(__aarch64__)
#error "This example is AArch64 assembly: build it for an arm64 target."
#endif

#include <bit>
#include <cstdint>
#include <print>

asm(R"(
        .text
        .p2align 2
        .globl  len2_source
        .globl  _len2_source
len2_source:
_len2_source:
        ldr     s0, [x0]            // x
        fmul    s0, s0, s0          // waits for the load
        ldr     s1, [x0, #4]        // y
        fmul    s1, s1, s1          // waits again
        fadd    s0, s0, s1          // x*x + y*y
        ldr     s2, [x0, #8]        // z
        fmul    s2, s2, s2
        fadd    s0, s0, s2          // (x*x + y*y) + z*z
        str     s0, [x1]
        ret

        .p2align 2
        .globl  len2_listed
        .globl  _len2_listed
len2_listed:
_len2_listed:
        ldr     s0, [x0]            // three independent loads start first
        ldr     s1, [x0, #4]
        ldr     s2, [x0, #8]
        fmul    s0, s0, s0
        fmul    s1, s1, s1
        fmul    s2, s2, s2
        fadd    s0, s0, s1          // the same two additions,
        fadd    s0, s0, s2          // in the same grouping
        str     s0, [x1]
        ret
)");

extern "C" void len2_source(const float *v, float *out);
extern "C" void len2_listed(const float *v, float *out);

int main() {
    const float inputs[][3] = {
        {3.0f, 4.0f, 12.0f},
        {1e20f, 1e20f, 1e-20f},
        {0.1f, 0.2f, 0.3f},
        {-7.5f, 1e-30f, 2.0f},
    };
    for (const auto &v : inputs) {
        float a = 0, b = 0;
        len2_source(v, &a);
        len2_listed(v, &b);
        float want = (v[0] * v[0] + v[1] * v[1]) + v[2] * v[2];
        auto bits = [](float f) { return std::bit_cast<std::uint32_t>(f); };
        std::println("{:08x} {:08x} {:08x}  {}", bits(a), bits(b), bits(want),
                     bits(a) == bits(b) && bits(b) == bits(want)
                         ? "same bits" : "DIFFERENT");
    }
}

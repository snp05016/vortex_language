// A dot product of two float arrays: one idea, one loop, two listings.
//
// The loop itself is portable C++ and gives the same answer on every
// target. What differs is the machine code clang writes for the multiply
// and the add inside it; the chapter quotes that code next to this file.
// -ffp-contract=off keeps this comparable to Vortex's rule against fusing
// a multiply and an add into one rounding (decision 56).

#include <print>

extern "C" float dot(const float* a, const float* b, int n) {
    float sum = 0.0f;
    for (int i = 0; i < n; ++i) {
        sum += a[i] * b[i];
    }
    return sum;
}

int main() {
    float a[] = {1.0f, 2.0f, 3.0f, 4.0f};
    float b[] = {5.0f, 6.0f, 7.0f, 8.0f};
    std::println("{}", dot(a, b, 4));
}

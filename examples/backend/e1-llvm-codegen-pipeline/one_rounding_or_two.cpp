// Why fusing a multiply and an add is not a free optimization: the fused
// form rounds once, the separate pair rounds twice, and for some inputs the
// two answers differ. The values are chosen so that the exact product needs
// one bit more than a float holds.
#include <cmath>
#include <cstdio>

int main() {
    // a * a = 1 + 2^-11 + 2^-24 exactly. A float keeps 23 bits after the
    // point, so the last term is exactly half a unit in the last place, and
    // round-to-nearest-even drops it.
    const float a = 1.0f + 0x1p-12f;
    const float c = -(1.0f + 0x1p-11f);

    // Two statements, two roundings. The build turns contraction off
    // (see the .toml), so the compiler may not fuse these itself.
    const float product = a * a;
    const float separate = product + c;

    // One rounding: the 2^-24 survives the multiply and becomes the answer.
    const float fused = std::fma(a, a, c);

    std::printf("product rounded:   %a\n", static_cast<double>(product));
    std::printf("fmul then fadd:    %a\n", static_cast<double>(separate));
    std::printf("fused (one round): %a\n", static_cast<double>(fused));
    std::printf("same answer:       %s\n", separate == fused ? "yes" : "no");
}

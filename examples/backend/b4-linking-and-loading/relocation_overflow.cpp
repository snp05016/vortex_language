#include <cstdint>
#include <cstdio>

// AArch64's BL instruction encodes its target as a 26-bit field holding a
// signed count of instructions (4-byte units), so the byte displacement
// from the call site to the target must fit in a fixed range and be
// 4-byte aligned. When a linker discovers a relocation whose displacement
// does not fit, it cannot patch the instruction as it stands: for a call,
// it inserts a veneer (a short stub, placed in range, that reloads the
// full target address and branches indirectly) and points the BL at the
// veneer instead. A2 introduces veneers as an AAPCS64 mechanism; this
// example computes the check that decides when one is needed.
//
// Follows: MaskRay (Fangrui Song), "Relocation overflow and code models"
// (https://maskray.me/blog/relocation-overflow-and-code-models).

constexpr std::int64_t kMinDisplacement = -(std::int64_t{1} << 27);
constexpr std::int64_t kMaxDisplacement = (std::int64_t{1} << 27) - 4;

bool fits_bl_range(std::int64_t displacement) {
    return displacement >= kMinDisplacement && displacement <= kMaxDisplacement
        && displacement % 4 == 0;
}

int main() {
    const std::int64_t cases[] = {
        kMaxDisplacement,       // the furthest a BL can reach forward
        kMaxDisplacement + 4,   // one instruction past that
        kMinDisplacement,       // the furthest a BL can reach backward
        kMinDisplacement - 4,   // one instruction past that
    };

    for (std::int64_t d : cases) {
        bool ok = fits_bl_range(d);
        std::printf("displacement %+12lld bytes: fits BL imm26? %s%s\n",
                    static_cast<long long>(d), ok ? "yes" : "no",
                    ok ? "" : " -> needs a veneer");
    }
    return 0;
}

// Follows: Apple, "Writing ARM64 code for Apple platforms", section
// "Handle Data Types and Data Alignment Properly".
// <https://developer.apple.com/documentation/xcode/writing-arm64-code-for-apple-platforms>
// Follows: ARM-software/abi-aa, AAPCS64, section 6.8.2 (values narrower
// than 32 bits are passed as 32-bit containers; the standard convention
// leaves their upper bits unspecified).
// <https://github.com/ARM-software/abi-aa/blob/main/aapcs64/aapcs64.rst>
//
// AAPCS64's standard rule does not say who extends a sub-word argument to
// fill its 32-bit register, so the bits above it can be anything left over
// from earlier work. Apple's arm64 ABI instead requires the *caller* to
// sign- or zero-extend narrow arguments before the call. A callee that
// assumes the wrong rule reads garbage above a narrow value.

#include <cstdint>
#include <cstdio>

// The standard convention: the caller is not required to clear or extend
// the bits above the argument, so they can hold anything left over from an
// earlier computation. Modelled here as a fixed, non-zero pattern.
std::uint32_t register_under_standard_rule() {
    std::uint32_t leftover_bits = 0x1234'0000u;
    std::uint8_t narrow_value = static_cast<std::uint8_t>(-5);
    return leftover_bits | narrow_value;
}

// Apple's rule: the caller sign-extends the narrow value across the whole
// register before the call.
std::uint32_t register_under_apple_rule() {
    std::int8_t narrow_value = -5;
    return static_cast<std::uint32_t>(static_cast<std::int32_t>(narrow_value));
}

int main() {
    std::uint32_t standard = register_under_standard_rule();
    std::uint32_t apple = register_under_apple_rule();

    std::printf("standard convention: register = 0x%08x\n", standard);
    std::printf("  masked to the narrow width first: %d\n",
                static_cast<int>(static_cast<std::int8_t>(standard)));
    std::printf("  widened without masking:          %d\n",
                static_cast<std::int32_t>(standard));

    std::printf("apple convention:    register = 0x%08x\n", apple);
    std::printf("  masked to the narrow width first: %d\n",
                static_cast<int>(static_cast<std::int8_t>(apple)));
    std::printf("  widened without masking:          %d\n",
                static_cast<std::int32_t>(apple));
}

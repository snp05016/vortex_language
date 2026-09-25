// Follows: Arm, AAPCS64, "Parameter passing rules" (unused bits of an
//   argument register have unspecified value).
// Follows: Apple, "Writing ARM64 code for Apple platforms" (the caller
//   extends arguments narrower than 32 bits).
//
// Models the 64-bit register that carries a signed 8-bit argument of -5,
// as each kind of caller may leave it, and three ways a callee can read it.
#include <cstdint>
#include <cstdio>

// Bits left over from earlier work. The standard lets a caller leave them.
constexpr std::uint64_t kJunk = 0xdeadbeef'cafe'1200;

// AAPCS64: only the low 8 bits are the argument; bits 8 to 63 may be junk.
std::uint64_t standard_caller(std::int8_t v) {
    return (kJunk & ~0xffull) | static_cast<std::uint8_t>(v);
}

// Apple: the caller sign-extends to 32 bits; bits 32 to 63 may still be junk.
std::uint64_t apple_caller(std::int8_t v) {
    auto low32 = static_cast<std::uint32_t>(static_cast<std::int32_t>(v));
    return (kJunk & ~0xffff'ffffull) | low32;
}

// Three callee readings, named after the instruction that implements each.
std::int64_t read_sxtb(std::uint64_t r) {  // extend from bit 7
    return static_cast<std::int8_t>(r & 0xff);
}
std::int64_t read_sxtw(std::uint64_t r) {  // extend from bit 31
    return static_cast<std::int32_t>(r & 0xffff'ffff);
}
std::int64_t read_x(std::uint64_t r) {     // trust all 64 bits
    return static_cast<std::int64_t>(r);
}

void show(const char* who, std::uint64_t r) {
    std::printf("%s leaves x0 = 0x%016llx\n", who,
                static_cast<unsigned long long>(r));
    std::printf("  sxtb x0, w0 -> %lld\n", static_cast<long long>(read_sxtb(r)));
    std::printf("  sxtw x0, w0 -> %lld\n", static_cast<long long>(read_sxtw(r)));
    std::printf("  use x0      -> %lld\n", static_cast<long long>(read_x(r)));
}

int main() {
    show("AAPCS64 caller", standard_caller(-5));
    show("Apple caller  ", apple_caller(-5));
}

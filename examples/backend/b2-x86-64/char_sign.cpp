// Not every target difference is an instruction. Plain `char` (with no
// `signed` or `unsigned`) has an implementation-defined sign, and clang
// picks it per target, not per source file: signed on x86-64 System V and
// on Apple's arm64 (a deliberate deviation from the base AArch64 rule),
// unsigned on a plain Linux arm64 target.
//
// This is checked on macOS arm64 and Linux x86-64 only (see the .toml):
// both agree here, so a bug that depends on this only shows up on the
// third runner, Linux arm64.

#include <limits>
#include <print>

int main() {
    std::println("plain char is signed: {}", std::numeric_limits<char>::is_signed);
}

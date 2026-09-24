// A bounds check with one comparison. An int32_t index may be negative.
// sxtw widens it to 64 bits and keeps its sign; then one unsigned comparison
// with the length rejects both kinds of bad index, because a negative number
// read as unsigned is at least 2^63, far above any length.
//
// index_flags() runs the same sxtw and cmp and returns the NZCV flags they
// leave behind, so the table shows why the check must use "lo" (unsigned
// lower, C clear) and not "lt" (signed less than, N and V differ).
//
// An int32_t argument fills only the low 32 bits of its register; the
// procedure call standard leaves the upper bits unspecified. That is one more
// reason to widen the index explicitly before it takes part in an address.

#if !defined(__aarch64__)
#error "This example is AArch64 assembly: build it for an arm64 target."
#endif

#include <cstdint>
#include <limits>
#include <print>

asm(R"(
        .text
        .p2align 2
        .globl  load_or
        .globl  _load_or
load_or:                                // (table, index, fallback)
_load_or:
        sxtw    x9, w1                  // x9 = index, sign-extended to 64 bits
        cmp     x9, #4                  // compare with the length, 4
        b.hs    1f                      // unsigned higher or same: out of bounds
        ldr     w0, [x0, x9, lsl #2]    // in bounds: w0 = table[index]
        ret
1:      mov     w0, w2                  // out of bounds: w0 = fallback
        ret

        .p2align 2
        .globl  index_flags
        .globl  _index_flags
index_flags:                            // (index)
_index_flags:
        sxtw    x9, w0
        cmp     x9, #4
        mrs     x0, nzcv                // N, Z, C and V are bits 31 to 28
        lsr     x0, x0, #28
        ret
)");

extern "C" std::int32_t load_or(const std::int32_t *table, std::int32_t index,
                                std::int32_t fallback);
extern "C" std::uint32_t index_flags(std::int32_t index);

int main() {
  const std::int32_t table[4] = {10, 20, 30, 40};
  const std::int32_t indexes[] = {std::numeric_limits<std::int32_t>::min(), -1, 0, 3, 4,
                                  std::numeric_limits<std::int32_t>::max()};
  std::println("{:>11}  N Z C V  lt   lo   load_or", "index");
  for (const std::int32_t index : indexes) {
    const std::uint32_t f = index_flags(index);
    const bool n = f & 8, z = f & 4, c = f & 2, v = f & 1;
    std::println("{:>11}  {:d} {:d} {:d} {:d}  {:<4} {:<4} {}", index, n, z, c, v,
                 n != v ? "yes" : "no", c ? "no" : "yes", load_or(table, index, -99));
  }
}

// Reaching a global array from hand-written code. adrp puts the address of
// the 4 KiB page that holds the table into x8; the next instruction adds the
// table's offset inside that page. The linker fills in both numbers.
//
// The instructions are the same on macOS and Linux, but three spellings
// differ, so the function is written out once per object-file format:
//   * the symbol name: Mach-O adds a leading underscore, ELF does not;
//   * the page offset: sym@PAGE and sym@PAGEOFF on Mach-O, sym and
//     :lo12:sym on ELF;
//   * the read-only data section: __TEXT,__const on Mach-O, .rodata on ELF.

#if !defined(__aarch64__)
#error "This example is AArch64 assembly: build it for an arm64 target."
#endif

#include <cstdint>
#include <print>

#if defined(__APPLE__)
asm(R"(
        .section __TEXT,__const
        .p2align 2
_squares:
        .long   0, 1, 4, 9, 16

        .text
        .p2align 2
        .globl  _square_of
_square_of:                                     // int32_t square_of(uint64_t i)
        adrp    x8, _squares@PAGE               // the page that holds the table
        add     x8, x8, _squares@PAGEOFF        // plus the offset inside it
        ldr     w0, [x8, x0, lsl #2]            // squares[i]
        ret
)");
#else
asm(R"(
        .section .rodata
        .p2align 2
squares:
        .long   0, 1, 4, 9, 16

        .text
        .p2align 2
        .globl  square_of
square_of:                                      // int32_t square_of(uint64_t i)
        adrp    x8, squares                     // the page that holds the table
        add     x8, x8, :lo12:squares           // plus the offset inside it
        ldr     w0, [x8, x0, lsl #2]            // squares[i]
        ret
)");
#endif

extern "C" std::int32_t square_of(std::uint64_t i);

int main() {
  for (std::uint64_t i = 0; i < 5; ++i)
    std::println("square_of({}) = {}", i, square_of(i));
}

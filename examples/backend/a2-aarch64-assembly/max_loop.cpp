// Two hand-written AArch64 loops that find the largest element of an int32
// array, checked against a C++ reference. The first keeps an index i and lets
// the load scale it: [x0, x9, lsl #2] reads the word at x0 + i * 4. The second
// moves the pointer itself with post-indexed addressing: [x0], #4 reads the
// word at x0 and then adds 4 to x0. Different instructions, same answers.
//
// Arguments arrive in x0 (the array) and x1 (the count, at least 1), and the
// result leaves in w0. x8 to x10 are scratch registers the function may
// overwrite without saving them.
//
// Mach-O (macOS) gives C names a leading underscore and ELF (Linux) does not,
// so each function carries both labels. Numeric labels such as 1: are local:
// 1b means "the nearest 1: backwards" and 2f "the nearest 2: forwards".

#if !defined(__aarch64__)
#error "This example is AArch64 assembly: build it for an arm64 target."
#endif

#include <cstdint>
#include <print>

asm(R"(
        .text
        .p2align 2
        .globl  max_indexed
        .globl  _max_indexed
max_indexed:
_max_indexed:
        ldr     w8, [x0]                // best = v[0]
        mov     x9, #1                  // i = 1
        b       2f                      // test the condition before the body
1:      ldr     w10, [x0, x9, lsl #2]   // w10 = v[i]
        cmp     w10, w8                 // compare as 32-bit values
        csel    w8, w10, w8, gt         // signed greater: best = v[i]
        add     x9, x9, #1              // i = i + 1
2:      cmp     x9, x1                  // compare i with the count
        b.lo    1b                      // unsigned lower: go round again
        mov     w0, w8
        ret

        .p2align 2
        .globl  max_postinc
        .globl  _max_postinc
max_postinc:
_max_postinc:
        ldr     w8, [x0], #4            // best = *v, then v advances 4 bytes
        subs    x1, x1, #1              // one element used, and the flags set
        b.eq    2f                      // it was the only one
1:      ldr     w10, [x0], #4           // w10 = *v, then v advances 4 bytes
        cmp     w10, w8
        csel    w8, w10, w8, gt
        subs    x1, x1, #1              // count down, Z is set at zero
        b.ne    1b
2:      mov     w0, w8
        ret
)");

extern "C" std::int32_t max_indexed(const std::int32_t *values, std::uint64_t count);
extern "C" std::int32_t max_postinc(const std::int32_t *values, std::uint64_t count);

// The same job in C++. Compile this file with -S (add -fno-vectorize to keep
// the loop scalar) and compare the loop clang writes with max_postinc.
extern "C" std::int32_t max_reference(const std::int32_t *values, std::uint64_t count) {
  std::int32_t best = values[0];
  for (std::uint64_t i = 1; i < count; ++i)
    if (values[i] > best)
      best = values[i];
  return best;
}

void show(const char *name, const std::int32_t *values, std::uint64_t count) {
  std::println("{:<9} indexed {:>3}, post-indexed {:>3}, C++ {:>3}", name,
               max_indexed(values, count), max_postinc(values, count),
               max_reference(values, count));
}

int main() {
  const std::int32_t mixed[] = {3, -5, 7, 2, 7, -1};
  const std::int32_t negative[] = {-8, -3, -12};
  const std::int32_t single[] = {42};
  show("mixed", mixed, 6);
  show("negative", negative, 3);
  show("single", single, 1);
}

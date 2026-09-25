// One SIMD&FP register, several names. v0 is 128 bits wide; q0, d0 and s0
// name its low 128, 64 and 32 bits. The name chooses the size of the
// operation, as w0 and x0 do for the integer registers.
//
// Three things to see:
//   1. s0 is the low 32 bits of d0, not a converted value. fmov copies bits
//      between register files without conversion; fcvt converts.
//   2. Writing a scalar (here with fadd s0, s0, s0) sets the rest of the
//      128-bit register to zero, as writing w0 zeroes the top of x0.
//   3. With the name v0.4s, one fadd works on four 32-bit lanes at once.
// Finally it reads FPCR, the control register that selects the rounding mode
// and whether subnormal numbers are flushed to zero.
//
// Mach-O (macOS) gives C names a leading underscore and ELF (Linux) does not,
// so each function carries both labels.

#if !defined(__aarch64__)
#error "This example is AArch64 assembly: build it for an arm64 target."
#endif

#include <cstdint>
#include <print>

asm(R"(
        .text
        .p2align 2
        .globl  low_view
        .globl  _low_view
low_view:                               // (double d) -> bits of s0
_low_view:
        fmov    w0, s0                  // copy 32 bits, no conversion
        ret

        .p2align 2
        .globl  narrowed
        .globl  _narrowed
narrowed:                               // (double d) -> bits of float(d)
_narrowed:
        fcvt    s0, d0                  // convert: round to single precision
        fmov    w0, s0
        ret

        .p2align 2
        .globl  double_lane0
        .globl  _double_lane0
double_lane0:                           // (float *four)
_double_lane0:
        ldr     q0, [x0]                // all 128 bits: four floats
        fadd    s0, s0, s0              // scalar write: lanes 1 to 3 become 0
        str     q0, [x0]                // store all 128 bits back
        ret

        .p2align 2
        .globl  add_lanes
        .globl  _add_lanes
add_lanes:                              // (float *out, const float *a, const float *b)
_add_lanes:
        ldr     q0, [x1]
        ldr     q1, [x2]
        fadd    v0.4s, v0.4s, v1.4s     // four additions, one per lane
        str     q0, [x0]
        ret

        .p2align 2
        .globl  read_fpcr
        .globl  _read_fpcr
read_fpcr:
_read_fpcr:
        mrs     x0, fpcr
        ret
)");

extern "C" std::uint32_t low_view(double d);
extern "C" std::uint32_t narrowed(double d);
extern "C" void double_lane0(float *four);
extern "C" void add_lanes(float *out, const float *a, const float *b);
extern "C" std::uint64_t read_fpcr();

int main() {
  const double d = 0x1.0000000000001p+0;  // 1 + 2^-52: low bit of d set
  std::println("s0 view of d0 = 1 + 2^-52: {:#010x}", low_view(d));
  std::println("fcvt s0, d0 of the same:   {:#010x}", narrowed(d));

  float q[4] = {1, 2, 3, 4};
  std::println("q0 before fadd s0, s0, s0: {} {} {} {}", q[0], q[1], q[2], q[3]);
  double_lane0(q);
  std::println("q0 after:                  {} {} {} {}", q[0], q[1], q[2], q[3]);

  const float a[4] = {1, 2, 3, 4}, b[4] = {10, 20, 30, 40};
  float sum[4];
  add_lanes(sum, a, b);
  std::println("fadd v0.4s: {} {} {} {}", sum[0], sum[1], sum[2], sum[3]);

  const std::uint64_t fpcr = read_fpcr();
  std::println("FPCR.RMode = {} (0 is round to nearest, ties to even)", (fpcr >> 22) & 3);
  std::println("FPCR.FZ    = {} (0 keeps subnormal numbers)", (fpcr >> 24) & 1);
}

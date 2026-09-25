// Does a block's pair of shared-memory tiles fit, and how many such blocks
// can share one core's scratchpad? Shared memory is counted here and nothing
// else: registers and thread limits cap residency too (G5).
//
// The limits, each from a named source:
//   NVIDIA H100 (Hopper Tuning Guide): 228 KB of shared memory per SM; CUDA
//     reserves 1 KB per resident block, so one block may address up to
//     227 KB; a static __shared__ allocation is limited to 48 KB, and more
//     needs a dynamic allocation with an explicit opt-in.
//   Apple GPU families Apple4 and later (Metal Feature Set Tables): 32 KB of
//     threadgroup memory per threadgroup. MTLDevice.maxThreadgroupMemoryLength
//     read 32768 on the owner's M4 Pro, macOS 27.0, 2026-09-23. Apple does not
//     publish how much threadgroup memory one GPU core holds, so the number of
//     threadgroups per core is left unknown rather than guessed.
// Follows: NVIDIA, Hopper Tuning Guide; Apple, Metal Feature Set Tables.

#include <cstddef>
#include <print>

constexpr std::size_t KB = 1024;

// NVIDIA H100
constexpr std::size_t h100_per_sm = 228 * KB;
constexpr std::size_t h100_reserved_per_block = 1 * KB;
constexpr std::size_t h100_per_block = h100_per_sm - h100_reserved_per_block;
constexpr std::size_t h100_static_limit = 48 * KB;

// Apple4 and later
constexpr std::size_t apple_per_threadgroup = 32 * KB;

int main() {
  std::println("tile pair = one T x T tile of a and one of b, f32");
  std::println("{:>4} {:>8} | {:<24} {:>10} | {}", "T", "bytes",
               "H100 allocation", "blocks/SM", "Apple");
  for (std::size_t t : {16, 32, 64, 128}) {
    std::size_t bytes = 2 * t * t * sizeof(float);
    const char *how = bytes > h100_per_block      ? "does not fit"
                      : bytes > h100_static_limit ? "dynamic, opt-in"
                                                  : "static is enough";
    std::size_t per_sm = bytes > h100_per_block
                             ? 0
                             : h100_per_sm / (bytes + h100_reserved_per_block);
    const char *apple = bytes <= apple_per_threadgroup ? "fits" : "too large";
    std::println("{:>4} {:>8} | {:<24} {:>10} | {}", t, bytes, how, per_sm,
                 apple);
  }
}

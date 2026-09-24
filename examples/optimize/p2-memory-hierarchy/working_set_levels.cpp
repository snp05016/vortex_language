// program: valid
// Follows: Ulrich Drepper, "What Every Programmer Should Know About Memory,"
// section 3.1 (a working set that does or does not fit in a cache level).
#include <cstdio>
#include <cstddef>

// Apple M4 Pro P-core, `sysctl hw.perflevel0.l1dcachesize` and
// `hw.perflevel0.l2cachesize`, measured 2026-09-23.
constexpr std::size_t kL1Bytes = 128 * 1024;
constexpr std::size_t kL2Bytes = 16 * 1024 * 1024;

const char* level_for(std::size_t bytes) {
    if (bytes <= kL1Bytes) return "L1";
    if (bytes <= kL2Bytes) return "L2";
    return "DRAM";
}

int main() {
    // A pointer-chasing latency test sweeps working sets like these; each
    // step doubles the set twice (a factor of 4) from 4 KiB to 256 MiB.
    for (std::size_t kib = 4; kib <= 256 * 1024; kib *= 4) {
        std::size_t bytes = kib * 1024;
        std::printf("%8zu KiB -> fits in %s\n", kib, level_for(bytes));
    }
    return 0;
}

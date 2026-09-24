// Follows: NVIDIA, "CUDA C++ Best Practices Guide", section 11.1, "Occupancy".
// Table numbers follow NVIDIA, "CUDA Programming Guide", section 5.1.3,
// Table 30 (threads, warps and blocks per SM) and Table 31 (registers and
// shared memory per SM), and the "Hopper Tuning Guide", section 1.4.1.1.
//
// One streaming multiprocessor (SM) has a fixed supply of threads, warps,
// registers and shared memory. A thread block runs on an SM only if all four
// budgets have room for it. This computes, for a few kernel shapes, how many
// blocks of a Hopper (compute capability 9.0) SM can hold at once, and which
// budget ran out first. It simplifies one thing real hardware does not:
// register and shared-memory use are rounded up to an allocation
// granularity before this division, so a real profiler's blocks-per-SM can
// be lower than this model's.
#include <cstdint>
#include <cstdio>

struct SmLimits {
    int max_threads;     // resident threads per SM
    int max_warps;       // resident warps per SM
    int max_blocks;      // resident thread blocks per SM
    int regs;            // 32-bit registers available to the whole SM
    int64_t smem_bytes;  // shared memory available to the whole SM
};

struct KernelConfig {
    int threads_per_block;
    int regs_per_thread;
    int64_t smem_per_block;  // bytes; 0 means the kernel uses none
};

struct Occupancy {
    int blocks;
    int warps;
    int percent;
    const char* limit;
};

Occupancy occupancy_of(const SmLimits& sm, const KernelConfig& k) {
    int warps_per_block = (k.threads_per_block + 31) / 32;
    int by_threads = sm.max_threads / k.threads_per_block;
    int by_regs = sm.regs / (k.regs_per_thread * k.threads_per_block);
    int by_smem = k.smem_per_block > 0
                      ? static_cast<int>(sm.smem_bytes / k.smem_per_block)
                      : sm.max_blocks;

    int blocks = sm.max_blocks;
    const char* limit = "blocks/SM";
    if (by_threads < blocks) {
        blocks = by_threads;
        limit = "threads/block";
    }
    if (by_regs < blocks) {
        blocks = by_regs;
        limit = "registers";
    }
    if (by_smem < blocks) {
        blocks = by_smem;
        limit = "shared memory";
    }

    int warps = blocks * warps_per_block;
    int percent = warps * 100 / sm.max_warps;
    return {blocks, warps, percent, limit};
}

int main() {
    // Hopper (compute capability 9.0).
    SmLimits hopper{2048, 64, 32, 65536, 228 * 1024};

    KernelConfig kernels[] = {
        {256, 32, 0},
        {256, 64, 0},
        {256, 128, 0},
        {1024, 32, 0},
        {256, 32, 32 * 1024},
        {256, 32, 96 * 1024},
    };

    std::printf("threads regs  smem blocks warps occupancy limit\n");
    for (const auto& k : kernels) {
        Occupancy o = occupancy_of(hopper, k);
        std::printf("%7d %4d %5lld %6d %5d %8d%% %s\n", k.threads_per_block,
                    k.regs_per_thread,
                    static_cast<long long>(k.smem_per_block), o.blocks,
                    o.warps, o.percent, o.limit);
    }
}

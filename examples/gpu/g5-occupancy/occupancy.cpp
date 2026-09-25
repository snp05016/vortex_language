// Follows: NVIDIA, "CUDA C++ Best Practices Guide", section 11.1.1,
// "Calculating Occupancy" (the 37-register example); NVIDIA, "CUDA
// Programming Guide", section 2.3.7, "Kernel Launch and Occupancy" (Table 2);
// Nsight Compute Profiling Guide, "Hardware Model" (four SM sub partitions,
// each with its own registers); "Hopper Tuning Guide", section 1.4.1.1.
//
// An SM holds a block only if every budget has room: warp slots, block
// slots, registers and shared memory. The register budget is modelled three
// ways, to show which rules are needed to match the vendor's own numbers.
#include <algorithm>
#include <cstdio>

struct Sm {
    int warps, blocks, regs;  // per-SM limits; regs = 0: not modelled
    long smem;                // bytes of shared memory per SM
};
struct Kernel {
    int threads, regs;  // regs per thread; 0: unknown, skip that budget
    long smem;          // bytes of shared memory per block
};
enum Model { kPlain, kRounded, kPartitioned };

int blocks_by_regs(const Sm& sm, const Kernel& k, Model m) {
    if (sm.regs == 0 || k.regs == 0) return sm.blocks;
    int warps_per_block = (k.threads + 31) / 32;
    if (m == kPlain) return sm.regs / (k.regs * k.threads);
    int per_warp = (k.regs * 32 + 255) / 256 * 256;  // 256-register units
    if (m == kRounded) return sm.regs / (per_warp * warps_per_block);
    int warps = 4 * (sm.regs / 4 / per_warp);  // each quarter holds whole warps
    return warps / warps_per_block;
}

void report(const Sm& sm, const Kernel& k, Model m) {
    int warps_per_block = (k.threads + 31) / 32;
    int blocks = std::min({sm.blocks, sm.warps / warps_per_block,
                           blocks_by_regs(sm, k, m)});
    if (k.smem > 0) blocks = std::min<long>(blocks, sm.smem / k.smem);
    int warps = blocks * warps_per_block;
    std::printf("%7d %4d %6ld %6d %5d %9.1f%%\n", k.threads, k.regs, k.smem,
                blocks, warps, 100.0 * warps / sm.warps);
}

int main() {
    const char* header = "threads regs   smem blocks warps occupancy\n";
    // Compute capability 7.0, as the Best Practices Guide gives it. The
    // guide names no block limit, and neither kernel could reach one.
    Sm volta{64, 1 << 30, 65536, 0};
    const char* names[] = {"plain", "rounded", "partitioned"};
    for (Model m : {kPlain, kRounded, kPartitioned}) {
        std::printf("CC 7.0, %s registers\n%s", names[m], header);
        report(volta, {128, 37, 0}, m);
        report(volta, {320, 37, 0}, m);
    }
    // Compute capability 10.0, Table 2 of the Programming Guide.
    Sm cc10{64, 32, 0, 233472};
    std::printf("CC 10.0, Programming Guide\n%s", header);
    report(cc10, {768, 0, 0}, kPartitioned);
    report(cc10, {32, 0, 0}, kPartitioned);
    report(cc10, {256, 0, 100 * 1024}, kPartitioned);
    // Hopper, compute capability 9.0 (Hopper Tuning Guide).
    Sm hopper{64, 32, 65536, 228 * 1024};
    std::printf("CC 9.0, Hopper\n%s", header);
    for (Kernel k : {Kernel{256, 32, 0}, Kernel{256, 64, 0},
                     Kernel{256, 128, 0}, Kernel{256, 32, 32 * 1024},
                     Kernel{256, 32, 96 * 1024}})
        report(hopper, k, kPartitioned);
}

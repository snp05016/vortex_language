// Follows: Vasily Volkov, "Better Performance at Lower Occupancy", GTC 2010,
// slides 7, 8, 10, 11 and 12 to 14.
//
// Little's law says a machine needs (latency x throughput) operations in
// flight to run at full throughput. On the G80-GT200 architecture Volkov
// measured, arithmetic latency is about 24 cycles and one SM completes about
// 8 operations per cycle at peak, so about 192 operations must be in flight
// at once (slide 11). Those operations can come from more warps
// (thread-level parallelism, TLP) or from more independent operations per
// thread (instruction-level parallelism, ILP); Little's law does not care
// which. This computes, for several (warps, independent operations per
// thread) pairs, how close each comes to that target, in the style of
// Volkov's slides 16 and 18, which plot the same shape against measured
// hardware.
#include <algorithm>
#include <cstdio>

constexpr int kWarpSize = 32;
constexpr int kNeededParallelism = 192;  // Volkov, slide 11, G80-GT200 row

struct Config {
    int warps;
    int ilp;  // independent operations issued per thread
};

int main() {
    Config configs[] = {
        {1, 1}, {2, 1}, {1, 2}, {4, 1}, {2, 2}, {6, 1}, {3, 2}, {1, 6},
    };

    std::printf("warps ilp in_flight utilization\n");
    for (const auto& c : configs) {
        int in_flight = c.warps * kWarpSize * c.ilp;
        int utilization = std::min(100, in_flight * 100 / kNeededParallelism);
        std::printf("%5d %3d %9d %10d%%\n", c.warps, c.ilp, in_flight,
                    utilization);
    }
}

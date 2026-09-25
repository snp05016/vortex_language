// Follows: Vasily Volkov, "Better Performance at Lower Occupancy", GTC 2010:
// slide 11 (latency, throughput and needed parallelism for three GPUs) and
// slides 16 to 21 (threads per SM needed for full throughput on a GTX480 at
// ILP 1 to 4, read from his measured curves).
//
// Little's law: operations in flight = latency x throughput. Volkov counts
// throughput in cores per SM, so the product is the number of independent
// thread-operations an SM must hold. With `ilp` independent operations per
// thread, the threads needed fall to that number divided by `ilp`.
#include <cstdio>

struct Gpu {
    const char* name;
    int latency;     // cycles, approximate (slide 11)
    int throughput;  // cores per SM (slide 11)
};

int main() {
    const Gpu gpus[] = {{"G80-GT200", 24, 8}, {"GF100", 18, 32},
                        {"GF104", 18, 48}};
    std::printf("gpu        latency  throughput  in flight\n");
    for (const auto& g : gpus)
        std::printf("%-10s %7d %11d %10d\n", g.name, g.latency, g.throughput,
                    g.latency * g.throughput);

    // GTX480 is a GF100 part. Measured: the smallest thread count at which
    // Volkov's curves reach 100 percent of peak (slides 16, 18, 20, 21).
    const int needed = 18 * 32;
    const int measured[] = {576, 320, 256, 192};
    std::printf("\nilp  predicted threads  measured threads (GTX480)\n");
    for (int ilp = 1; ilp <= 4; ++ilp)
        std::printf("%3d %18d %17d\n", ilp, needed / ilp, measured[ilp - 1]);
}

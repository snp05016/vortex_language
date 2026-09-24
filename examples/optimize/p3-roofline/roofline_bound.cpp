// Follows: Williams, Waterman, Patterson, "Roofline: An Insightful Visual
// Performance Model for Floating-Point Programs and Multicore
// Architectures", CACM 52(4), 2009, section 3 (the bound formula and the
// ridge point) and section 4 (Figure 1).
//
// Applies the roofline bound, attainable = min(peak, bandwidth * intensity),
// to the paper's own numbers for a 2.2 GHz dual-socket AMD Opteron X2
// (model 2214): a peak of 17.6 GFlop/s and a peak DRAM bandwidth of 15 GB/s,
// both measured by the paper's own microbenchmarks, not by this program.
// The intensities are the ones intensity_model.cpp computes for a naive and
// a tiled 64x64x64 matmul; this program prints no timing of its own.
#include <cstdio>
#include <algorithm>
#include <initializer_list>

struct Rung { const char* label; double intensity; };

double attainable(double peak_gflops, double bandwidth_gbs, double intensity) {
    return std::min(peak_gflops, bandwidth_gbs * intensity);
}

int main() {
    const double peak = 17.6;      // GFlop/s, Opteron X2 (section 3)
    const double bandwidth = 15.0; // GB/s, Opteron X2 (section 3)
    const double ridge = peak / bandwidth;

    std::printf("peak = %.1f GFlop/s, bandwidth = %.1f GB/s, ridge = %.3f flops/byte\n",
                peak, bandwidth, ridge);

    for (Rung r : {Rung{"naive", 0.485}, Rung{"tiled t=4", 1.778}, Rung{"tiled t=8", 3.200},
                    Rung{"tiled t=16", 5.333}, Rung{"tiled t=32", 8.000}}) {
        double bound = attainable(peak, bandwidth, r.intensity);
        const char* regime = (r.intensity < ridge) ? "memory-bound" : "compute-bound";
        std::printf("%-11s intensity=%6.3f bound=%6.3f GFlop/s (%s)\n",
                     r.label, r.intensity, bound, regime);
    }
}

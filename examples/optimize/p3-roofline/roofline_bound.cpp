// Follows: Williams, Waterman and Patterson, "Roofline: An Insightful
// Visual Performance Model for Multicore Architectures", CACM 52(4), 2009,
// section 3: attainable = min(peak, bandwidth x intensity).
//
// Places tiled schedules of an n = 2048 matmul under the roofline of the
// paper's dual-socket 2.2 GHz AMD Opteron X2 (model 2214): 17.6 GFlop/s
// double-precision peak and 15 GB/s from the authors' own bandwidth
// benchmark. Because that peak is for doubles, the matrices here are f64.
// The bounds are computed, not measured.
#include <algorithm>
#include <cstdio>
#include <initializer_list>

constexpr double peak = 17.6;     // GFlop/s
constexpr double bandwidth = 15.0; // GB/s

// The same traffic model as intensity_model.cpp, with 8-byte elements.
double intensity(double n, double t) {
    return (2 * n * n * n) / ((n / t) * n * n * 8 + 2 * n * n * 8);
}

int main() {
    const double ridge = peak / bandwidth;
    std::printf("ridge point = %.3f flops/byte\n", ridge);
    for (double t : {1.0, 2.0, 4.0, 8.0, 16.0, 32.0}) {
        double oi = intensity(2048, t);
        double bound = std::min(peak, bandwidth * oi);
        std::printf("t = %2.0f  intensity %6.3f  bound %6.3f GFlop/s  %s\n", t,
                    oi, bound, oi < ridge ? "memory-bound" : "compute-bound");
    }
}

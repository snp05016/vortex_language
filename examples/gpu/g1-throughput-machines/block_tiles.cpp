// Follows: Samuel Williams, Andrew Waterman and David Patterson, "Roofline",
// Communications of the ACM 52(4), 2009, section 3 (attainable performance is
// the smaller of the peak and bandwidth x operational intensity), and Vasily
// Volkov, GTC 2010, slide 8 (a GTX480 completes 480 multiply-adds, 960 flops,
// and 32 four-byte loads, 128 bytes, per cycle).
//
// A block of threads that computes a t x t tile of c walks k in steps of t.
// At each step it loads a t x t tile of a and one of b, and every value it
// loads serves t multiply-adds. Over the whole n x n product, a and b are
// each read n / t times and c is written once. With no cache catching a
// repeat, that is the traffic; t = 1 is the one-thread-per-output kernel.
#include <algorithm>
#include <cstdio>

int main() {
    const double flops_per_cycle = 960;
    const double bytes_per_cycle = 128;
    const double ridge = flops_per_cycle / bytes_per_cycle;
    const double n = 1024;
    const double flops = 2 * n * n * n;
    const double matrix_bytes = n * n * 4;

    std::printf("n = %.0f, ridge point %.2f flops per byte\n\n", n, ridge);
    std::printf("%4s %14s %11s %13s  %s\n", "t", "bytes", "flops/byte",
                "flops/cycle", "bound by");
    const int tiles[] = {1, 2, 4, 8, 16, 32, 64};
    for (int t : tiles) {
        const double bytes = 2 * (n / t) * matrix_bytes + matrix_bytes;
        const double intensity = flops / bytes;
        const double bound =
            std::min(flops_per_cycle, bytes_per_cycle * intensity);
        std::printf("%4d %14.0f %11.3f %13.1f  %s\n", t, bytes, intensity,
                    bound, intensity < ridge ? "bandwidth" : "peak");
    }
}

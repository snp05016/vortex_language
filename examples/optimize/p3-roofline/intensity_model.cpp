// Follows: Williams, Waterman, Patterson, "Roofline: An Insightful Visual
// Performance Model for Floating-Point Programs and Multicore
// Architectures", CACM 52(4), 2009, section 5 ("Tying the 3Cs to
// Operational Intensity").
//
// A static model of operational intensity (flops per byte of DRAM traffic)
// for a square matmul C = A*B of dimension n, at a few tile sizes. This
// counts flops and an ESTIMATE of DRAM bytes from the loop structure; it is
// a teaching model, not a hardware measurement, and it prints no timing.
#include <cstdio>
#include <initializer_list>

// One fused multiply-add per inner-loop step, counted as two flops (a
// multiply and an add), matching the paper's convention (section 3).
long long flop_count(long long n) { return 2LL * n * n * n; }

// Bytes moved for a schedule that re-reads the n*n panel of B from DRAM
// once for every group of t rows of C (n/t times in all), while A and C
// are each read or written once. t = 1 is the naive, unblocked loop; t = n
// reads B exactly once, which is the least any correct schedule can do
// (the paper's "compulsory" traffic, section 5).
long long bytes_moved(long long n, long long t) {
    if (t < 1) t = 1;
    if (t > n) t = n;
    return (n * n * n / t) * 4 /* B, re-read n/t times */
         + 2LL * n * n * 4 /* A and C, touched once each */;
}

double intensity(long long n, long long t) {
    return static_cast<double>(flop_count(n)) / static_cast<double>(bytes_moved(n, t));
}

int main() {
    const long long n = 64; // Vortex's stage-10 matmul kernel size

    std::printf("n = %lld, flops = %lld\n", n, flop_count(n));
    std::printf("%-10s %10s %10s %12s\n", "schedule", "tile", "bytes", "flops/byte");
    std::printf("%-10s %10s %10lld %12.3f\n", "naive", "-", bytes_moved(n, 1), intensity(n, 1));
    for (long long t : {4LL, 8LL, 16LL, 32LL}) {
        std::printf("%-10s %10lld %10lld %12.3f\n", "tiled", t, bytes_moved(n, t), intensity(n, t));
    }
    std::printf("%-10s %10s %10lld %12.3f\n", "compulsory", "n", bytes_moved(n, n), intensity(n, n));
}

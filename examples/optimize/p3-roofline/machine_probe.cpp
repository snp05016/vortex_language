// Follows: J. D. McCalpin, STREAM (the triad kernel and its byte count),
// and Williams, Waterman and Patterson, "Roofline", CACM 52(4), 2009,
// section 3 (find peak bandwidth and peak flops with microbenchmarks).
//
// Compile-checked only. Its output is timings, which change from run to
// run, so the examples harness builds it and never runs it. Run it yourself
// under P1's rules, and write down the machine and the date.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

template <class Work> double median_seconds(Work&& work, int runs) {
    std::vector<double> times;
    for (int r = 0; r < runs; ++r) {
        auto start = std::chrono::steady_clock::now();
        work();
        std::chrono::duration<double> d = std::chrono::steady_clock::now() - start;
        times.push_back(d.count());
    }
    std::sort(times.begin(), times.end());
    return times[times.size() / 2];
}

int main() {
    // Bandwidth: the triad a[i] = b[i] + s * c[i] on arrays far larger than
    // the caches, so every pass streams from DRAM. STREAM counts 24 bytes per
    // element and ignores the extra read a write-allocate cache makes of a.
    const std::size_t n = std::size_t{1} << 24; // 128 MiB per array
    std::vector<double> a(n, 0.0), b(n, 1.0), c(n, 2.0);
    const double s = 3.0;
    double tb = median_seconds([&] {
        for (std::size_t i = 0; i < n; ++i) a[i] = b[i] + s * c[i];
    }, 11);
    std::printf("triad: %.2f GB/s\n", 24.0 * static_cast<double>(n) / tb / 1e9);

    // Flops: independent chains of fused multiply-adds. One chain would wait
    // for its own previous result every step and measure latency, not peak.
    constexpr int chains = 16;
    double acc[chains];
    for (int j = 0; j < chains; ++j) acc[j] = j;
    const long long steps = 50'000'000;
    double tf = median_seconds([&] {
        for (long long i = 0; i < steps; ++i)
            for (int j = 0; j < chains; ++j) acc[j] = std::fma(acc[j], 0.999999, 1e-9);
    }, 5);
    std::printf("fma chains: %.2f GFlop/s\n", 2.0 * chains * steps / tf / 1e9);

    // Print results the optimizer cannot predict, so the work stays.
    double sum = a[n / 2];
    for (double x : acc) sum += x;
    std::printf("checksum: %g\n", sum);
}

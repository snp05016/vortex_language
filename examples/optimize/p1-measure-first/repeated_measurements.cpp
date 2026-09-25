// Fifteen made-up launch times, in arbitrary ticks: a worked example of the
// arithmetic, not a measurement. Thirteen launches ran undisturbed; two were
// interrupted. The program prints what each common summary makes of them.
//
// Follows: Hoefler and Belli, "Scientific Benchmarking of Parallel Computing
// Systems", section 3.1.3 (the confidence interval of the median from ranks,
// after Le Boudec). The bootstrap interval is the textbook percentile method.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    std::size_t n = v.size();
    return n % 2 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2.0;
}

// A 64-bit linear congruential generator, written out so that every standard
// library produces the same resamples (std::uniform_int_distribution does not).
struct Lcg {
    std::uint64_t state;
    std::size_t below(std::size_t n) {
        state = state * 6364136223846793005u + 1442695040888963407u;
        return static_cast<std::size_t>((state >> 33) % n);
    }
};

int main() {
    const std::vector<double> t = {104, 98, 231, 101, 97, 99, 103, 96,
                                   275, 100, 102, 99, 105, 98, 101};
    const std::size_t n = t.size();
    std::vector<double> sorted = t;
    std::sort(sorted.begin(), sorted.end());

    double mean = 0;
    for (double x : t) mean += x;
    mean /= static_cast<double>(n);
    std::printf("n=%zu min=%.0f median=%.0f mean=%.1f max=%.0f\n", n, sorted[0],
                median(t), mean, sorted[n - 1]);

    // From ranks: no assumption about the shape of the distribution, and no
    // random numbers. Ranks are 1-based, as in the paper.
    const double z = 1.96, root = std::sqrt(static_cast<double>(n));
    auto lo = static_cast<std::size_t>(std::floor((n - z * root) / 2));
    auto hi = static_cast<std::size_t>(std::ceil(1 + (n + z * root) / 2));
    std::printf("ranks %zu..%zu: median in [%.0f, %.0f]\n", lo, hi,
                sorted[lo - 1], sorted[hi - 1]);

    // Bootstrap: resample the launches with replacement, take the median of
    // each resample, and read off the 2.5th and 97.5th percentiles.
    Lcg rng{2026};
    std::vector<double> medians, resample(n);
    for (int b = 0; b < 2000; ++b) {
        for (auto& x : resample) x = t[rng.below(n)];
        medians.push_back(median(resample));
    }
    std::sort(medians.begin(), medians.end());
    std::printf("bootstrap, 2000 resamples: median in [%.0f, %.0f]\n",
                medians[50], medians[1949]);
}

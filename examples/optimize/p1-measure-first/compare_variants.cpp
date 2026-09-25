// Two versions of a kernel, A and B, each launched eleven times (made-up
// times in arbitrary ticks). The question is how much faster B is, and how
// sure we can be. One pair of launches answers it wrongly; the ratio of the
// medians, with a bootstrap interval, answers it with its uncertainty.
//
// Follows: Kalibera and Jones, "Rigorous Benchmarking in Reasonable Time"
// (report an effect size with a confidence interval), using a bootstrap
// interval for a ratio of medians instead of their interval for a ratio of
// means.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <vector>

double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    std::size_t n = v.size();
    return n % 2 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2.0;
}

struct Lcg {  // the same portable generator as repeated_measurements.cpp
    std::uint64_t state;
    std::size_t below(std::size_t n) {
        state = state * 6364136223846793005u + 1442695040888963407u;
        return static_cast<std::size_t>((state >> 33) % n);
    }
};

int main() {
    // Interleaved launches: A1 B1 A2 B2 ..., so slow drift hits both.
    const std::vector<double> a = {97, 101, 104, 100, 99, 180, 102, 98, 103, 101, 100};
    const std::vector<double> b = {99, 92, 94, 91, 96, 93, 95, 160, 92, 94, 93};

    std::printf("first launch only: A=%.0f B=%.0f\n", a[0], b[0]);
    const double ma = median(a), mb = median(b);
    std::printf("medians: A=%.0f B=%.0f ratio A/B=%.3f\n", ma, mb, ma / mb);

    // Resample each variant's launches on its own, then take the ratio.
    Lcg rng{7};
    std::vector<double> ratios, ra(a.size()), rb(b.size());
    for (int r = 0; r < 2000; ++r) {
        for (auto& x : ra) x = a[rng.below(a.size())];
        for (auto& x : rb) x = b[rng.below(b.size())];
        ratios.push_back(median(ra) / median(rb));
    }
    std::sort(ratios.begin(), ratios.end());
    std::printf("95%% interval for A/B: [%.3f, %.3f]\n", ratios[50], ratios[1949]);
}

// A worked example, not a real measurement: a fixed, synthetic trace of ten
// invocation costs, in arbitrary ticks, standing in for what a benchmark
// harness would hand you. The point is the arithmetic that turns many noisy
// readings into a summary worth publishing, not the numbers themselves.
//
// Follows: Kalibera & Jones, "Rigorous Benchmarking in Reasonable Time"
// (percentile bootstrap confidence interval for a summary statistic) and
// Georges, Buytaert & Eeckhout, "Statistically Rigorous Java Performance
// Evaluation" (report a distribution, not a single best run).
#include <algorithm>
#include <cstdio>
#include <random>
#include <vector>

// Percentile bootstrap: resample the trace with replacement `resamples`
// times, compute the statistic on each resample, and take the 2.5th and
// 97.5th percentiles of the results as a 95% interval. The PRNG is seeded
// so the interval is exactly reproducible, not just "close enough".
template <typename Statistic>
std::pair<double, double> bootstrapCI(const std::vector<int>& trace,
                                       Statistic statistic,
                                       int resamples,
                                       unsigned seed) {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<std::size_t> pick(0, trace.size() - 1);
    std::vector<double> replicates;
    replicates.reserve(static_cast<std::size_t>(resamples));

    std::vector<int> resample(trace.size());
    for (int r = 0; r < resamples; ++r) {
        for (std::size_t i = 0; i < trace.size(); ++i) resample[i] = trace[pick(rng)];
        replicates.push_back(statistic(resample));
    }
    std::sort(replicates.begin(), replicates.end());
    std::size_t lo = static_cast<std::size_t>(0.025 * static_cast<double>(replicates.size()));
    std::size_t hi = static_cast<std::size_t>(0.975 * static_cast<double>(replicates.size()));
    return {replicates[lo], replicates[hi]};
}

double median(std::vector<int> values) {
    std::sort(values.begin(), values.end());
    std::size_t n = values.size();
    if (n % 2 == 1) return values[n / 2];
    return (values[n / 2 - 1] + values[n / 2]) / 2.0;
}

int main() {
    const std::vector<int> trace = {104, 98, 231, 101, 97, 305, 99, 103, 96, 275};

    int best = *std::min_element(trace.begin(), trace.end());
    double mean = 0;
    for (int t : trace) mean += t;
    mean /= static_cast<double>(trace.size());
    double med = median(trace);

    auto [lo, hi] = bootstrapCI(
        trace, [](const std::vector<int>& sample) { return median(sample); }, 2000, 12345u);

    std::printf("n=%zu best=%d mean=%.1f median=%.1f ci95=[%.1f, %.1f]\n",
                trace.size(), best, mean, med, lo, hi);
}

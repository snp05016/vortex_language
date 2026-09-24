// A percentile bootstrap confidence interval for a median, over a fixed
// synthetic sample (arbitrary units, not a real timing). Reporting a
// single number, or the best of several runs, hides how much a benchmark
// varies; a confidence interval says so directly. The resampling uses a
// small hand-written generator, not std::mt19937 or std::rand, so the
// output is the same integer sequence on every standard library.
#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <vector>

class SplitLcg {
   public:
    explicit SplitLcg(std::uint32_t seed) : state_(seed) {}
    std::uint32_t next() {
        state_ = state_ * 1103515245u + 12345u;
        return (state_ >> 16) & 0x7fffu;
    }

   private:
    std::uint32_t state_;
};

double median_of(std::vector<double> values) {
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];  // odd-sized samples throughout
}

int main() {
    // A synthetic sample of nine "iteration costs", not a measurement:
    // eight ordinary runs and one outlier, the shape a noisy benchmark
    // often has.
    const std::array<double, 9> sample{12, 11, 13, 12, 50, 11, 12, 13, 12};
    const std::vector<double> data(sample.begin(), sample.end());
    const double point_median = median_of(data);

    const int resamples = 1000;
    std::vector<double> resample_medians;
    resample_medians.reserve(resamples);
    SplitLcg rng(1);
    for (int r = 0; r < resamples; ++r) {
        std::vector<double> resample;
        resample.reserve(data.size());
        for (std::size_t i = 0; i < data.size(); ++i) {
            resample.push_back(data[rng.next() % data.size()]);
        }
        resample_medians.push_back(median_of(std::move(resample)));
    }
    std::sort(resample_medians.begin(), resample_medians.end());
    // 95% interval: drop the lowest and highest 2.5% of the 1000 resample
    // medians.
    const double low = resample_medians[25];
    const double high = resample_medians[974];

    std::cout << "sample size = " << data.size() << "\n";
    std::cout << "median = " << point_median << "\n";
    std::cout << "95% bootstrap CI = [" << low << ", " << high << "]\n";
    return 0;
}

// Does one rung beat the one before it? Compare two sets of timings with a
// percentile bootstrap interval for the ratio of their medians. The
// samples are made-up ticks, not measurements; a small fixed generator
// replaces std::mt19937 so every standard library prints the same lines.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <vector>

struct Lcg {  // 64-bit linear congruential generator, top bits only
    std::uint64_t state;
    std::size_t below(std::size_t bound) {
        state = state * 6364136223846793005u + 1442695040888963407u;
        return static_cast<std::size_t>(state >> 33) % bound;
    }
};

double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];  // every sample here has an odd size
}

std::vector<double> resample(const std::vector<double>& v, Lcg& rng) {
    std::vector<double> r;
    for (std::size_t i = 0; i < v.size(); ++i) r.push_back(v[rng.below(v.size())]);
    return r;
}

// Ratio old/new of the medians: above 1.0 means the new rung is faster.
void compare(const char* label, const std::vector<double>& old_rung,
             const std::vector<double>& new_rung) {
    Lcg rng{2026};
    std::vector<double> ratios;
    for (int r = 0; r < 2000; ++r)
        ratios.push_back(median(resample(old_rung, rng)) / median(resample(new_rung, rng)));
    std::sort(ratios.begin(), ratios.end());
    const double low = ratios[50], high = ratios[1949];  // middle 95% of 2000
    std::printf("%s: medians %.0f -> %.0f, ratio %.2f, 95%% CI [%.2f, %.2f]: %s\n", label,
                median(old_rung), median(new_rung), median(old_rung) / median(new_rung), low,
                high, low > 1.0 ? "faster" : (high < 1.0 ? "slower" : "no difference shown"));
}

int main() {
    const std::vector<double> rung_a{412, 405, 398, 431, 402, 409, 520, 400, 407, 415, 403};
    const std::vector<double> rung_b{301, 296, 310, 305, 299, 380, 303, 298, 307, 302, 300};
    const std::vector<double> rung_c{297, 305, 290, 312, 301, 293, 360, 299, 308, 295, 302};
    compare("a -> b", rung_a, rung_b);
    compare("b -> c", rung_b, rung_c);
}

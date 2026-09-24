// Follows: Tri Dao et al., "FlashAttention: Fast and Memory-Efficient Exact
// Attention with IO-Awareness", NeurIPS 2022 (arXiv:2205.14135), the running
// rescale their tiled loop applies to a softmax normalizer.
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>

// Two-pass softmax: one pass finds the maximum, a second exponentiates and
// sums. Every input value is read twice.
template <std::size_t N>
std::array<double, N> two_pass_softmax(const std::array<double, N>& x) {
    double m = x[0];
    for (double v : x) {
        if (v > m) m = v;
    }
    std::array<double, N> p{};
    double l = 0.0;
    for (std::size_t i = 0; i < N; ++i) {
        p[i] = std::exp(x[i] - m);
        l += p[i];
    }
    for (std::size_t i = 0; i < N; ++i) {
        p[i] /= l;
    }
    return p;
}

// Online softmax: one pass. Whenever a larger value raises the running
// maximum m, everything accumulated so far (the running sum l and every
// entry written into p) is rescaled by exp(old_m - new_m) before the new
// value is folded in. Each input value is read once.
template <std::size_t N>
std::array<double, N> online_softmax(const std::array<double, N>& x) {
    std::array<double, N> p{};
    double m = -std::numeric_limits<double>::infinity();
    double l = 0.0;
    for (std::size_t i = 0; i < N; ++i) {
        if (x[i] > m) {
            double scale = std::exp(m - x[i]); // exp(-inf) = 0 on the first value
            l *= scale;
            for (std::size_t j = 0; j < i; ++j) {
                p[j] *= scale;
            }
            m = x[i];
        }
        p[i] = std::exp(x[i] - m);
        l += p[i];
    }
    for (std::size_t i = 0; i < N; ++i) {
        p[i] /= l;
    }
    return p;
}

int main() {
    std::array<double, 6> values = {2.0, 0.5, 3.0, -1.0, 1.5, 3.0};

    std::array<double, 6> p_two_pass = two_pass_softmax(values);
    std::array<double, 6> p_online = online_softmax(values);

    bool match = true;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (std::abs(p_two_pass[i] - p_online[i]) > 1e-9) {
            match = false;
        }
    }

    std::printf("elements: %zu\n", values.size());
    std::printf("two-pass reads of the input: %zu\n", 2 * values.size());
    std::printf("online reads of the input: %zu\n", values.size());
    std::printf("outputs match within tolerance: %s\n", match ? "yes" : "no");
    return 0;
}

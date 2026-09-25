// Follows: Dao, "FlashAttention-2: Faster Attention with Better Parallelism
// and Work Partitioning", 2023 (arXiv:2307.08691), section 3.1.1: one query
// row swept over key/value blocks with a running max, a running sum and an
// output that is divided by the sum only once, at the end.
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>

constexpr std::size_t D = 2;   // head dimension
constexpr std::size_t NK = 4;  // number of keys and values
constexpr std::size_t B = 2;   // keys per block

using Vec = std::array<double, D>;

double dot(const Vec& a, const Vec& b) {
    double s = 0.0;
    for (std::size_t i = 0; i < D; ++i) s += a[i] * b[i];
    return s;
}

// The three steps as written: all NK scores exist at once.
Vec naive_row(const Vec& q, const std::array<Vec, NK>& k, const std::array<Vec, NK>& v) {
    std::array<double, NK> s{};
    double m = -std::numeric_limits<double>::infinity();
    for (std::size_t j = 0; j < NK; ++j) {
        s[j] = dot(q, k[j]);
        m = std::fmax(m, s[j]);
    }
    double l = 0.0;
    Vec out{};
    for (std::size_t j = 0; j < NK; ++j) {
        double p = std::exp(s[j] - m);
        l += p;
        for (std::size_t c = 0; c < D; ++c) out[c] += p * v[j][c];
    }
    for (std::size_t c = 0; c < D; ++c) out[c] /= l;
    return out;
}

// One block of B scores at a time. The running output is kept unscaled
// (not yet divided by l); a rising maximum rescales it and l together.
Vec tiled_row(const Vec& q, const std::array<Vec, NK>& k, const std::array<Vec, NK>& v) {
    double m = -std::numeric_limits<double>::infinity();
    double l = 0.0;
    Vec acc{};
    for (std::size_t start = 0; start < NK; start += B) {
        std::array<double, B> s{};
        double block_max = -std::numeric_limits<double>::infinity();
        for (std::size_t j = 0; j < B; ++j) {
            s[j] = dot(q, k[start + j]);
            block_max = std::fmax(block_max, s[j]);
        }
        double new_m = std::fmax(m, block_max);
        double scale = std::exp(m - new_m);  // exp(-inf) is 0 for the first block
        l *= scale;
        for (std::size_t c = 0; c < D; ++c) acc[c] *= scale;
        for (std::size_t j = 0; j < B; ++j) {
            double p = std::exp(s[j] - new_m);
            l += p;
            for (std::size_t c = 0; c < D; ++c) acc[c] += p * v[start + j][c];
        }
        m = new_m;
        std::printf("block %zu: scores %4.1f %4.1f  m %4.1f  scale %.4f  l %.4f  acc (%.4f, %.4f)\n",
                    start / B, s[0], s[1], m, scale, l, acc[0], acc[1]);
    }
    for (std::size_t c = 0; c < D; ++c) acc[c] /= l;
    return acc;
}

int main() {
    const Vec q = {1.0, 0.0};
    const std::array<Vec, NK> k = {{{0.0, 1.0}, {-1.0, 0.0}, {1.0, 0.0}, {1.0, 1.0}}};
    const std::array<Vec, NK> v = {{{1.0, 2.0}, {3.0, 4.0}, {5.0, 6.0}, {7.0, 8.0}}};

    Vec tiled = tiled_row(q, k, v);
    Vec naive = naive_row(q, k, v);
    std::printf("tiled output (%.4f, %.4f)\n", tiled[0], tiled[1]);
    std::printf("naive output (%.4f, %.4f)\n", naive[0], naive[1]);
    std::printf("scores held at once: naive %zu, tiled %zu\n", NK, B);
    return 0;
}

// Follows: Tri Dao et al., "FlashAttention: Fast and Memory-Efficient Exact
// Attention with IO-Awareness", NeurIPS 2022 (arXiv:2205.14135), algorithm 1's
// sweep over key/value blocks with a running max, sum and output.
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>

constexpr std::size_t D = 2;   // head dimension
constexpr std::size_t NK = 4;  // number of key/value rows
constexpr std::size_t B = 2;   // key/value block size

using Vec = std::array<double, D>;

double dot(const Vec& a, const Vec& b) {
    double s = 0.0;
    for (std::size_t i = 0; i < D; ++i) {
        s += a[i] * b[i];
    }
    return s;
}

// Naive attention for one query row: build the whole score row (NK
// elements), softmax it, then take the weighted sum of every value row.
// The score row is the largest amount of score data resident at once.
Vec naive_row(const Vec& q, const std::array<Vec, NK>& k, const std::array<Vec, NK>& v) {
    std::array<double, NK> s{};
    for (std::size_t j = 0; j < NK; ++j) {
        s[j] = dot(q, k[j]);
    }
    double m = s[0];
    for (double x : s) {
        if (x > m) m = x;
    }
    double l = 0.0;
    std::array<double, NK> p{};
    for (std::size_t j = 0; j < NK; ++j) {
        p[j] = std::exp(s[j] - m);
        l += p[j];
    }
    Vec out{};
    for (std::size_t j = 0; j < NK; ++j) {
        for (std::size_t d = 0; d < D; ++d) {
            out[d] += p[j] * v[j][d];
        }
    }
    for (std::size_t d = 0; d < D; ++d) {
        out[d] /= l;
    }
    return out;
}

// Tiled attention for one query row: sweep key/value blocks of size B,
// keeping only the running max m, running sum l and running weighted output
// on chip. A block of B scores is the largest amount of score data resident
// at any moment; the full NK-long row is never assembled.
Vec tiled_row(const Vec& q, const std::array<Vec, NK>& k, const std::array<Vec, NK>& v) {
    double m = -std::numeric_limits<double>::infinity();
    double l = 0.0;
    Vec out{};
    for (std::size_t block = 0; block < NK; block += B) {
        std::array<double, B> s{};
        double block_max = -std::numeric_limits<double>::infinity();
        for (std::size_t j = 0; j < B; ++j) {
            s[j] = dot(q, k[block + j]);
            if (s[j] > block_max) block_max = s[j];
        }
        double new_m = m > block_max ? m : block_max;
        double scale = std::exp(m - new_m); // exp(-inf) = 0 on the first block
        l *= scale;
        for (std::size_t d = 0; d < D; ++d) {
            out[d] *= scale;
        }
        for (std::size_t j = 0; j < B; ++j) {
            double p = std::exp(s[j] - new_m);
            l += p;
            for (std::size_t d = 0; d < D; ++d) {
                out[d] += p * v[block + j][d];
            }
        }
        m = new_m;
    }
    for (std::size_t d = 0; d < D; ++d) {
        out[d] /= l;
    }
    return out;
}

int main() {
    std::array<Vec, 2> q = {{ {1.0, 0.0}, {0.0, 1.0} }};
    std::array<Vec, NK> k = {{ {1.0, 0.0}, {0.0, 1.0}, {1.0, 1.0}, {-1.0, 0.0} }};
    std::array<Vec, NK> v = {{ {1.0, 2.0}, {3.0, 4.0}, {5.0, 6.0}, {7.0, 8.0} }};

    bool match = true;
    for (std::size_t i = 0; i < q.size(); ++i) {
        Vec a = naive_row(q[i], k, v);
        Vec b = tiled_row(q[i], k, v);
        for (std::size_t d = 0; d < D; ++d) {
            if (std::abs(a[d] - b[d]) > 1e-9) match = false;
        }
    }

    std::printf("sequence length: %zu\n", NK);
    std::printf("block size: %zu\n", B);
    std::printf("naive score buffer: %zu bytes\n", NK * sizeof(double));
    std::printf("tiled score buffer: %zu bytes\n", B * sizeof(double));
    std::printf("outputs match within tolerance: %s\n", match ? "yes" : "no");
    return 0;
}

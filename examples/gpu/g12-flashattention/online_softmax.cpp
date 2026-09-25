// Follows: Milakov and Gimelshein, "Online normalizer calculation for
// softmax", 2018 (arXiv:1805.02867), algorithms 2 and 3 and the merge
// operator of section 3.
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>

constexpr std::size_t N = 6;

// Every read or write of the vector goes through these, so the program can
// count memory accesses the way the paper does: per element of the vector.
struct Counted {
    std::array<double, N> data{};
    std::size_t loads = 0;
    std::size_t stores = 0;
    double load(std::size_t i) { ++loads; return data[i]; }
    void store(std::size_t i, double v) { ++stores; data[i] = v; }
};

// Safe softmax: pass 1 finds the maximum, pass 2 sums exp(x - max),
// pass 3 writes the outputs. Three loads and one store per element.
void safe_softmax(Counted& x, Counted& y) {
    double m = -std::numeric_limits<double>::infinity();
    for (std::size_t i = 0; i < N; ++i) m = std::fmax(m, x.load(i));
    double l = 0.0;
    for (std::size_t i = 0; i < N; ++i) l += std::exp(x.load(i) - m);
    for (std::size_t i = 0; i < N; ++i) y.store(i, std::exp(x.load(i) - m) / l);
}

// The running pair (maximum, sum of exp(x - maximum)).
struct Stats { double m; double l; };

// Fold one more value into the pair. When the maximum rises, the old sum was
// measured against the wrong maximum, so it is rescaled by exp(old - new).
Stats fold(Stats s, double x) {
    double m = std::fmax(s.m, x);
    return {m, s.l * std::exp(s.m - m) + std::exp(x - m)};
}

// Combine the pairs of two halves: the same rescale, applied to both sides.
Stats merge(Stats a, Stats b) {
    double m = std::fmax(a.m, b.m);
    return {m, a.l * std::exp(a.m - m) + b.l * std::exp(b.m - m)};
}

// Online softmax: pass 1 builds the pair, pass 2 writes the outputs.
// Two loads and one store per element.
Stats online_softmax(Counted& x, Counted& y) {
    Stats s{-std::numeric_limits<double>::infinity(), 0.0};
    for (std::size_t i = 0; i < N; ++i) s = fold(s, x.load(i));
    for (std::size_t i = 0; i < N; ++i) y.store(i, std::exp(x.load(i) - s.m) / s.l);
    return s;
}

int main() {
    const std::array<double, N> values = {2.0, 0.5, 3.0, -1.0, 1.5, 3.5};

    Counted x1{values}, y1{};
    safe_softmax(x1, y1);
    Counted x2{values}, y2{};
    Stats whole = online_softmax(x2, y2);

    // Two workers each fold half of the vector; merging their pairs must
    // give the pair that one sequential sweep gave.
    Stats left{-std::numeric_limits<double>::infinity(), 0.0};
    Stats right = left;
    for (std::size_t i = 0; i < N / 2; ++i) left = fold(left, values[i]);
    for (std::size_t i = N / 2; i < N; ++i) right = fold(right, values[i]);
    Stats merged = merge(left, right);

    bool outputs_match = true;
    for (std::size_t i = 0; i < N; ++i) {
        if (std::fabs(y1.data[i] - y2.data[i]) > 1e-12) outputs_match = false;
    }
    bool pairs_match = merged.m == whole.m && std::fabs(merged.l - whole.l) < 1e-12;

    std::printf("safe softmax:   %zu loads, %zu stores\n", x1.loads, y1.stores);
    std::printf("online softmax: %zu loads, %zu stores\n", x2.loads, y2.stores);
    std::printf("outputs match within 1e-12: %s\n", outputs_match ? "yes" : "no");
    std::printf("running max %.1f, running sum %.4f\n", whole.m, whole.l);
    std::printf("merged halves give the same pair: %s\n", pairs_match ? "yes" : "no");
    return 0;
}

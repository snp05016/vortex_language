// Follows: the rescale step of Milakov and Gimelshein, "Online normalizer
// calculation for softmax", 2018 (arXiv:1805.02867). Built with
// -ffp-contract=off so that no compiler turns a * s + b * s into a fused
// multiply-add, which would round once where the source rounds twice.
#include <cstdio>

int main() {
    // Two ways to rescale a running sum of two terms by a factor s:
    // add first, then scale once (what a running sum does when the maximum
    // rises), or scale each term, then add (what a two-pass sum does).
    // In exact arithmetic they are equal; in doubles each step rounds.
    const double s = 3.0;
    int pairs = 0;
    int differ = 0;
    double first_a = 0.0, first_b = 0.0;
    for (int i = 1; i <= 9; ++i) {
        for (int j = 1; j <= 9; ++j) {
            double a = i / 10.0;
            double b = j / 10.0;
            double sum_then_scale = (a + b) * s;
            double scale_then_sum = a * s + b * s;
            ++pairs;
            if (sum_then_scale != scale_then_sum) {
                if (differ == 0) { first_a = a; first_b = b; }
                ++differ;
            }
        }
    }
    std::printf("pairs tried: %d, results that differ: %d\n", pairs, differ);
    std::printf("first: a = %.1f, b = %.1f\n", first_a, first_b);
    std::printf("  (a + b) * 3 = %.17g\n", (first_a + first_b) * s);
    std::printf("  a*3 + b*3   = %.17g\n", first_a * s + first_b * s);
    return 0;
}

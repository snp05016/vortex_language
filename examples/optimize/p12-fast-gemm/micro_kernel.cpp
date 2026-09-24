// A 4x4 register-blocked micro-kernel over packed, k-major panels, checked
// against a plain triple loop over the same values. The panel covers the
// whole reduction in one pass (no split over k), so the accumulators start
// at zero and add each k in increasing order: the same order the naive loop
// uses, so the two must agree bit for bit. Splitting k into several panels
// changes that order (see the page); this kernel does not.
//
// Follows: Goto & van de Geijn, TOMS 34(3) 2008 (packed operands, register
// accumulators); Van Zee & van de Geijn, TOMS 41(3) 2015, and the BLIS
// KernelsHowTo (the micro-kernel's C11 += A1*B1 contract).
#include <cstdio>
#include <vector>

constexpr int mr = 4, nr = 4, K = 5;

int main() {
    // Logical operands: a is mr x K, b is K x nr. Small integer-valued
    // floats keep every product and sum exactly representable.
    std::vector<std::vector<float>> a(mr, std::vector<float>(K));
    std::vector<std::vector<float>> b(K, std::vector<float>(nr));
    for (int i = 0; i < mr; ++i)
        for (int k = 0; k < K; ++k) a[i][k] = static_cast<float>(i * K + k + 1);
    for (int k = 0; k < K; ++k)
        for (int j = 0; j < nr; ++j) b[k][j] = static_cast<float>(k * nr + j + 1);

    // Pack both operands k-major: one step of k reads mr (or nr) contiguous
    // values, exactly what the micro-kernel below loads.
    std::vector<float> ap(mr * K), bp(K * nr);
    for (int k = 0; k < K; ++k) {
        for (int i = 0; i < mr; ++i) ap[k * mr + i] = a[i][k];
        for (int j = 0; j < nr; ++j) bp[k * nr + j] = b[k][j];
    }

    // The micro-kernel: mr*nr accumulators, mr+nr loads per step of k.
    float acc[mr][nr] = {};
    for (int k = 0; k < K; ++k) {
        for (int i = 0; i < mr; ++i)
            for (int j = 0; j < nr; ++j) acc[i][j] += ap[k * mr + i] * bp[k * nr + j];
    }

    // The naive triple loop over the same logical operands, same k order.
    bool match = true;
    for (int i = 0; i < mr; ++i) {
        for (int j = 0; j < nr; ++j) {
            float sum = 0.0f;
            for (int k = 0; k < K; ++k) sum += a[i][k] * b[k][j];
            if (sum != acc[i][j]) match = false;
        }
    }

    std::printf("match: %s\n", match ? "yes" : "no");
    std::printf("loads per multiply-add: (mr+nr)/(mr*nr) = %d/%d\n", mr + nr, mr * nr);
    return 0;
}

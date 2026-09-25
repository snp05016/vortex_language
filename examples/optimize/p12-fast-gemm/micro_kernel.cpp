// A 4x4 register-blocked micro-kernel over packed panels, run with k split
// into two kc-panels, in the two ways real kernels are written:
//   C-initialized: the accumulators start from the c tile and each panel
//     continues the same running sums, so every c element still adds its
//     products one at a time, in increasing k;
//   zero-initialized: each panel sums into fresh zeros and the panel total
//     is then added to c (the shape of C11 := beta*C11 + alpha*A1*B1).
// Both are compared bit for bit with the naive triple loop. The .toml adds
// -ffp-contract=off so no multiply and add are fused (Vortex decision 56).
//
// Follows: BLIS KernelsHowTo (the gemm micro-kernel contract and packed
// micropanel layout); Goto & van de Geijn, TOMS 34(3) 2008, section 6.1.
#include <cstdio>

constexpr int mr = 4, nr = 4, K = 8, kc = 4;
float a[mr][K], b[K][nr];
float ap[K][mr], bp[K][nr];  // packed: step k reads mr (or nr) neighbours

void c_initialized(float c[mr][nr]) {
    for (int p0 = 0; p0 < K; p0 += kc) {
        float acc[mr][nr];
        for (int i = 0; i < mr; ++i)
            for (int j = 0; j < nr; ++j) acc[i][j] = c[i][j];  // load the tile
        for (int k = p0; k < p0 + kc; ++k)
            for (int i = 0; i < mr; ++i)
                for (int j = 0; j < nr; ++j) acc[i][j] += ap[k][i] * bp[k][j];
        for (int i = 0; i < mr; ++i)
            for (int j = 0; j < nr; ++j) c[i][j] = acc[i][j];
    }
}

void zero_initialized(float c[mr][nr]) {
    for (int p0 = 0; p0 < K; p0 += kc) {
        float ab[mr][nr] = {};
        for (int k = p0; k < p0 + kc; ++k)
            for (int i = 0; i < mr; ++i)
                for (int j = 0; j < nr; ++j) ab[i][j] += ap[k][i] * bp[k][j];
        for (int i = 0; i < mr; ++i)
            for (int j = 0; j < nr; ++j) c[i][j] = c[i][j] + ab[i][j];
    }
}

int main() {
    // Values whose sums round, so the order of additions can show.
    for (int k = 0; k < K; ++k) {
        for (int i = 0; i < mr; ++i) a[i][k] = 1.0f / float(3 + i + 2 * k);
        for (int j = 0; j < nr; ++j) b[k][j] = 1.0f / float(7 + 3 * j + k);
        for (int i = 0; i < mr; ++i) ap[k][i] = a[i][k];  // pack a
        for (int j = 0; j < nr; ++j) bp[k][j] = b[k][j];  // pack b
    }
    float c1[mr][nr] = {}, c2[mr][nr] = {};
    c_initialized(c1);
    zero_initialized(c2);

    int same1 = 0, same2 = 0;
    for (int i = 0; i < mr; ++i)
        for (int j = 0; j < nr; ++j) {
            float sum = 0.0f;  // the naive loop's order
            for (int k = 0; k < K; ++k) sum += a[i][k] * b[k][j];
            same1 += (c1[i][j] == sum);
            same2 += (c2[i][j] == sum);
        }
    std::printf("K = %d split into panels of kc = %d\n", K, kc);
    std::printf("C-initialized accumulators: %d of %d elements equal to naive\n", same1, mr * nr);
    std::printf("zero-initialized per panel: %d of %d elements equal to naive\n", same2, mr * nr);
    return 0;
}

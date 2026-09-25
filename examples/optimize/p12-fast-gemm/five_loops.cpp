// The five loops around one micro-kernel, on a shape that no block size
// divides: m = 7, n = 10, k = 9. The pc loop packs a kc x nc slab of b, the
// ic loop packs an mc x kc block of a, both zero-padded to whole micro-panels,
// and the micro-kernel continues c's running sums (C-initialized), writing
// back only the elements that exist. The counters show what each loop costs.
//
// Follows: Smith et al., IPDPS 2014, Figure 2 (the five loops); Van Zee & van
// de Geijn, TOMS 41(3) 2015, section 5.1 (zero-padded packing for edges).
#include <algorithm>
#include <cstdio>
#include <vector>

constexpr int m = 7, n = 10, k = 9;
constexpr int nc = 6, kc = 4, mc = 4, nr = 3, mr = 2;
float a[m][k], b[k][n], c[m][n], naive[m][n];
long copied_b = 0, copied_a = 0, calls = 0, madds = 0;

int main() {
    for (int i = 0; i < m; ++i)
        for (int p = 0; p < k; ++p) a[i][p] = 1.0f / float(2 + i + 3 * p);
    for (int p = 0; p < k; ++p)
        for (int j = 0; j < n; ++j) b[p][j] = 1.0f / float(5 + 2 * j + p);

    std::vector<float> bt, at;  // the packed buffers
    for (int jc = 0; jc < n; jc += nc) {
        int nb = std::min(nc, n - jc), npanels = (nb + nr - 1) / nr;
        for (int pc = 0; pc < k; pc += kc) {
            int kb = std::min(kc, k - pc);
            bt.assign(npanels * kb * nr, 0.0f);  // pack b: kb x nr panels, row by row
            for (int q = 0; q < npanels; ++q)
                for (int p = 0; p < kb; ++p)
                    for (int j = 0; j < nr; ++j, ++copied_b)
                        if (q * nr + j < nb) bt[(q * kb + p) * nr + j] = b[pc + p][jc + q * nr + j];
            for (int ic = 0; ic < m; ic += mc) {
                int mb = std::min(mc, m - ic), mpanels = (mb + mr - 1) / mr;
                at.assign(mpanels * kb * mr, 0.0f);  // pack a: kb x mr panels, column by column
                for (int s = 0; s < mpanels; ++s)
                    for (int p = 0; p < kb; ++p)
                        for (int i = 0; i < mr; ++i, ++copied_a)
                            if (s * mr + i < mb) at[(s * kb + p) * mr + i] = a[ic + s * mr + i][pc + p];
                for (int jr = 0; jr < npanels; ++jr)
                    for (int ir = 0; ir < mpanels; ++ir) {
                        // The micro-kernel: load the c tile, add kb rank-1 updates, store.
                        ++calls;
                        float acc[mr][nr] = {};
                        auto row = [&](int i) { return ic + ir * mr + i; };
                        auto col = [&](int j) { return jc + jr * nr + j; };
                        for (int i = 0; i < mr; ++i)
                            for (int j = 0; j < nr; ++j)
                                if (row(i) < m && col(j) < n) acc[i][j] = c[row(i)][col(j)];
                        for (int p = 0; p < kb; ++p)
                            for (int i = 0; i < mr; ++i)
                                for (int j = 0; j < nr; ++j, ++madds)
                                    acc[i][j] += at[(ir * kb + p) * mr + i] * bt[(jr * kb + p) * nr + j];
                        for (int i = 0; i < mr; ++i)
                            for (int j = 0; j < nr; ++j)
                                if (row(i) < m && col(j) < n) c[row(i)][col(j)] = acc[i][j];
                    }
            }
        }
    }

    int same = 0;
    for (int i = 0; i < m; ++i)
        for (int j = 0; j < n; ++j) {
            for (int p = 0; p < k; ++p) naive[i][j] += a[i][p] * b[p][j];
            same += (naive[i][j] == c[i][j]);
        }
    std::printf("micro-kernel calls: %ld\n", calls);
    std::printf("multiply-adds: %ld (m*n*k = %d)\n", madds, m * n * k);
    std::printf("elements copied into packed b: %ld (k*n = %d)\n", copied_b, k * n);
    std::printf("elements copied into packed a: %ld (m*k = %d)\n", copied_a, m * k);
    std::printf("bitwise equal to naive: %d of %d\n", same, m * n);
    return 0;
}

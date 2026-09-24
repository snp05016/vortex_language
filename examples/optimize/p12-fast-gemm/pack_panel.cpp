// Packing an mc-row, kc-column block of a row-major matrix into a panel
// buffer ordered the way a register-blocked micro-kernel reads it: grouped
// into mr-row strips, each strip stored k-major so that one step of k reads
// mr contiguous values. When mc is not a multiple of mr, the last strip is
// padded with zeros rather than left short, so every strip the micro-kernel
// reads is exactly mr wide.
//
// Follows: Goto & van de Geijn, TOMS 34(3) 2008 (the packing layout);
// Van Zee & van de Geijn, TOMS 41(3) 2015, and Smith et al., IPDPS 2014
// (packing edges by zero-padding).
#include <cstdio>
#include <vector>

// A row-major matrix: value(r, c) lives at values[r * cols + c].
struct Matrix {
    int rows, cols;
    std::vector<float> values;
    float at(int r, int c) const {
        return (r < rows && c < cols) ? values[r * cols + c] : 0.0f;
    }
};

// Packs mc rows starting at row0, all kc columns starting at col0, into
// strips of mr rows. mc need not be a multiple of mr or fit inside `rows`:
// missing rows read as zero.
std::vector<float> pack_a(const Matrix& a, int row0, int mc, int col0, int kc, int mr) {
    std::vector<float> panel;
    int strips = (mc + mr - 1) / mr;
    for (int s = 0; s < strips; ++s) {
        for (int k = 0; k < kc; ++k) {
            for (int r = 0; r < mr; ++r) {
                panel.push_back(a.at(row0 + s * mr + r, col0 + k));
            }
        }
    }
    return panel;
}

int main() {
    // 5 rows, 3 columns: values 0..14 in row-major order, so a[r][c] = r*3+c.
    Matrix a{5, 3, {}};
    for (int r = 0; r < 5; ++r)
        for (int c = 0; c < 3; ++c) a.values.push_back(static_cast<float>(r * 3 + c));

    const int mr = 2;
    auto panel = pack_a(a, 0, 5, 0, 3, mr);  // mc = 5: the last strip is padded

    int strips = (5 + mr - 1) / mr;
    int idx = 0;
    for (int s = 0; s < strips; ++s) {
        std::printf("strip %d (rows %d-%d):\n", s, s * mr, s * mr + mr - 1);
        for (int k = 0; k < 3; ++k) {
            std::printf("  k=%d:", k);
            for (int r = 0; r < mr; ++r) std::printf(" %.1f", panel[idx++]);
            std::printf("\n");
        }
    }
    return 0;
}

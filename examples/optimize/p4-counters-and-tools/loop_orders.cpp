// Follows: Ulrich Drepper, "What Every Programmer Should Know About Memory",
// section 6.2.1 (matrix multiplication and its access pattern), and the perf wiki
// tutorial, "Counting with perf stat".
//
// A target for counters, not a timer: it prints no times. Run it with no
// argument and it checks that the two loop orders agree bit for bit. Run it
// with `ijk` or `ikj` and it computes only that order, so a profiler sees
// one loop order per run:
//
//   perf stat -e '{cycles,instructions}' ./loop_orders ikj        (Linux)
//   xcrun xctrace record --template 'CPU Counters' \
//       --launch -- ./loop_orders ikj                              (macOS)
#include <cstdio>
#include <cstring>
#include <vector>

constexpr int n = 512;  // B's rows are 2 KiB apart: far beyond one line
using Matrix = std::vector<float>;  // row-major, n by n

// Small whole numbers: every product and partial sum is exact in f32.
void fill(Matrix& m, int seed) {
    for (int i = 0; i < n * n; ++i) m[i] = float((i * 7 + seed) % 5) - 2.0f;
}

// Inner loop walks down a column of B: one element per row, n*4 bytes apart.
void multiply_ijk(const Matrix& a, const Matrix& b, Matrix& c) {
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j) {
            float sum = 0.0f;
            for (int k = 0; k < n; ++k) sum += a[i * n + k] * b[k * n + j];
            c[i * n + j] = sum;
        }
}

// Inner loop walks along a row of B and a row of C: 4 bytes apart.
// Each c[i][j] still receives its products in increasing k.
void multiply_ikj(const Matrix& a, const Matrix& b, Matrix& c) {
    for (int i = 0; i < n * n; ++i) c[i] = 0.0f;
    for (int i = 0; i < n; ++i)
        for (int k = 0; k < n; ++k) {
            const float aik = a[i * n + k];
            for (int j = 0; j < n; ++j) c[i * n + j] += aik * b[k * n + j];
        }
}

double checksum(const Matrix& c) {
    double s = 0.0;
    for (int i = 0; i < n * n; ++i) s += double(c[i]) * double(i % 13 + 1);
    return s;
}

int main(int argc, char** argv) {
    Matrix a(n * n), b(n * n), c1(n * n), c2(n * n);
    fill(a, 1);
    fill(b, 3);
    if (argc > 1) {  // one order only, for a profiler
        bool ijk = std::strcmp(argv[1], "ijk") == 0;
        if (ijk) multiply_ijk(a, b, c1); else multiply_ikj(a, b, c1);
        std::printf("%s checksum %.1f\n", ijk ? "ijk" : "ikj", checksum(c1));
        return 0;
    }
    multiply_ijk(a, b, c1);
    multiply_ikj(a, b, c2);
    std::printf("n = %d\n", n);
    std::printf("ijk checksum %.1f\n", checksum(c1));
    std::printf("ikj checksum %.1f\n", checksum(c2));
    bool same = std::memcmp(c1.data(), c2.data(), c1.size() * sizeof(float)) == 0;
    std::printf("same bits: %s\n", same ? "yes" : "no");
}

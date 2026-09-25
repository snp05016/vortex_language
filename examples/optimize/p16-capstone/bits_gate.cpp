// A bits gate for a ladder of 8x8 matrix products. Every variant is
// compared with rung 0, element by element, as 32-bit patterns: `==` is
// not enough, because it calls +0.0 and -0.0 equal. Built with
// -ffp-contract=off, so each * and + rounds once unless a variant asks
// for std::fma on purpose.
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>

constexpr int n = 8;
using Matrix = std::array<float, n * n>;  // row r, column c at r * n + c
using Rung = Matrix (*)(const Matrix&, const Matrix&);

float at(const Matrix& m, int r, int c) { return m[r * n + c]; }

Matrix rung0(const Matrix& a, const Matrix& b) {  // naive ijk
    Matrix c{};
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j) {
            float sum = 0.0f;
            for (int k = 0; k < n; ++k) sum += at(a, i, k) * at(b, k, j);
            c[i * n + j] = sum;
        }
    return c;
}

Matrix rung2(const Matrix& a, const Matrix& b) {  // ikj: order across elements
    Matrix c{};
    for (int i = 0; i < n; ++i)
        for (int k = 0; k < n; ++k)
            for (int j = 0; j < n; ++j) c[i * n + j] += at(a, i, k) * at(b, k, j);
    return c;
}

Matrix rung6b(const Matrix& a, const Matrix& b) {  // zero-started panels of 4
    Matrix c{};
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j)
            for (int k0 = 0; k0 < n; k0 += 4) {
                float acc = 0.0f;
                for (int k = k0; k < k0 + 4; ++k) acc += at(a, i, k) * at(b, k, j);
                c[i * n + j] += acc;  // one add per panel, not one per product
            }
    return c;
}

Matrix rung7(const Matrix& a, const Matrix& b) {  // fused multiply-add
    Matrix c{};
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j) {
            float sum = 0.0f;
            for (int k = 0; k < n; ++k) sum = std::fma(at(a, i, k), at(b, k, j), sum);
            c[i * n + j] = sum;
        }
    return c;
}

Matrix first_term(const Matrix& a, const Matrix& b) {  // "0 + x is x"
    Matrix c{};
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j) {
            float sum = at(a, i, 0) * at(b, 0, j);
            for (int k = 1; k < n; ++k) sum += at(a, i, k) * at(b, k, j);
            c[i * n + j] = sum;
        }
    return c;
}

void gate(const char* input, const char* name, const Matrix& ref, const Matrix& got) {
    int bits = 0, values = 0;
    for (int e = 0; e < n * n; ++e) {
        bits += std::bit_cast<std::uint32_t>(ref[e]) != std::bit_cast<std::uint32_t>(got[e]);
        values += ref[e] != got[e];
    }
    if (bits == 0) std::printf("%-6s %-22s identical\n", input, name);
    else std::printf("%-6s %-22s differs in %d of %d (== sees %d)\n", input, name, bits, n * n, values);
}

int main() {
    Matrix a{}, b{}, neg_zero{}, ones{};
    for (int e = 0; e < n * n; ++e) {
        a[e] = static_cast<float>((e * 37) % 23 - 11) / 7.0f;  // sevenths: mostly inexact in binary
        b[e] = static_cast<float>((e * 29) % 19 - 9) / 3.0f;
        neg_zero[e] = -0.0f;
        ones[e] = 1.0f;
    }
    const struct { const char* name; Rung run; } ladder[] = {
        {"2 interchange ikj", rung2}, {"6b zero-started panels", rung6b},
        {"7 fused multiply-add", rung7}, {"x start at first term", first_term}};
    for (const auto& r : ladder) gate("mixed", r.name, rung0(a, b), r.run(a, b));
    for (const auto& r : ladder) gate("zeros", r.name, rung0(neg_zero, ones), r.run(neg_zero, ones));
}

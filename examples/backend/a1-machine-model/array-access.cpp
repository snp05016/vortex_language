// Two small array accesses whose compiled code differs between a
// load/store machine and a register-memory machine. The page shows what
// clang made of `bump` and `element` for AArch64 and for x86-64; main
// only checks that the functions do what their names say.

#include <cstdio>

// Read one element, add 1, write it back: a read-modify-write of memory.
void bump(int* a, long i) { a[i] += 1; }

// One element of a row-major matrix with three columns. The row stride,
// 3 * 4 = 12 bytes, is not a power of two; the element size, 4, is.
float element(const float (*m)[3], long i, long k) { return m[i][k]; }

int main() {
    int counts[4] = {10, 20, 30, 40};
    bump(counts, 2);
    std::printf("counts: %d %d %d %d\n", counts[0], counts[1], counts[2], counts[3]);

    const float m[2][3] = {{1.0f, 2.0f, 3.0f}, {4.0f, 5.0f, 6.0f}};
    std::printf("m[1][2] = %.1f\n", element(m, 1, 2));
    return 0;
}

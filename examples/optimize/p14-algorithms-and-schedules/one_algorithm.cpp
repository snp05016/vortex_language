// One algorithm, five schedules. The algorithm fixes what each element of C
// is: start at 0, then add a[i][k] * b[k][j] for k = 0, 1, ..., N-1, in that
// order. A schedule only chooses the order of the loops and which thread runs
// them. Four schedules keep every element's chain of additions; the fifth
// splits it and is shown to change bits.
#include <array>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <thread>
#include <vector>

constexpr int N = 48;
constexpr int T = 16;  // tile size for the tiled schedule
using Matrix = std::array<float, N * N>;

Matrix A, B;

// --- The algorithm: the only place the arithmetic is written. ---
void init(Matrix& c, int i, int j) { c[i * N + j] = 0.0f; }
void update(Matrix& c, int i, int j, int k) { c[i * N + j] += A[i * N + k] * B[k * N + j]; }

// --- Schedules: loop orders around the same two definitions. ---
void rows(Matrix& c) {  // i, j, k: the reference order
    for (int i = 0; i < N; ++i)
        for (int j = 0; j < N; ++j) {
            init(c, i, j);
            for (int k = 0; k < N; ++k) update(c, i, j, k);
        }
}

void k_outer(Matrix& c) {  // k, i, j: each element still sees k in order
    for (int i = 0; i < N; ++i)
        for (int j = 0; j < N; ++j) init(c, i, j);
    for (int k = 0; k < N; ++k)
        for (int i = 0; i < N; ++i)
            for (int j = 0; j < N; ++j) update(c, i, j, k);
}

void tiled(Matrix& c) {  // split i, j, k by T; k blocks run in increasing order
    for (int i = 0; i < N; ++i)
        for (int j = 0; j < N; ++j) init(c, i, j);
    for (int io = 0; io < N; io += T)
        for (int jo = 0; jo < N; jo += T)
            for (int ko = 0; ko < N; ko += T)
                for (int i = io; i < io + T; ++i)
                    for (int j = jo; j < jo + T; ++j)
                        for (int k = ko; k < ko + T; ++k) update(c, i, j, k);
}

void threads(Matrix& c) {  // parallel over i: each thread owns whole rows
    std::vector<std::thread> pool;
    for (int t = 0; t < 4; ++t)
        pool.emplace_back([&c, t] {
            for (int i = t * N / 4; i < (t + 1) * N / 4; ++i)
                for (int j = 0; j < N; ++j) {
                    init(c, i, j);
                    for (int k = 0; k < N; ++k) update(c, i, j, k);
                }
        });
    for (auto& th : pool) th.join();
}

void split_k(Matrix& c) {  // NOT valid under strict rules: two partial sums per element
    Matrix high{};
    for (int i = 0; i < N; ++i)
        for (int j = 0; j < N; ++j) {
            init(c, i, j);
            init(high, i, j);
            for (int k = 0; k < N / 2; ++k) update(c, i, j, k);
            for (int k = N / 2; k < N; ++k) update(high, i, j, k);
            c[i * N + j] += high[i * N + j];  // regroups the additions
        }
}

std::uint32_t bits_hash(const Matrix& c) {  // FNV-1a over the bit patterns
    std::uint32_t h = 2166136261u;
    for (float v : c) {
        h ^= std::bit_cast<std::uint32_t>(v);
        h *= 16777619u;
    }
    return h;
}

int main() {
    for (int r = 0; r < N; ++r)
        for (int s = 0; s < N; ++s) {
            A[r * N + s] = 1.0f / float(r + s + 1);
            B[r * N + s] = 1.0f / float(r + 2 * s + 3);
        }
    struct Schedule { const char* name; void (*run)(Matrix&); };
    const Schedule schedules[] = {{"rows", rows}, {"k_outer", k_outer}, {"tiled", tiled},
                                  {"threads", threads}, {"split_k", split_k}};
    Matrix reference{};
    rows(reference);
    for (const Schedule& s : schedules) {
        Matrix c{};
        s.run(c);
        int differ = 0;
        for (int e = 0; e < N * N; ++e)
            differ += std::bit_cast<std::uint32_t>(c[e]) != std::bit_cast<std::uint32_t>(reference[e]);
        std::printf("%-8s hash %08x  elements differing from rows: %d\n", s.name, bits_hash(c), differ);
    }
}

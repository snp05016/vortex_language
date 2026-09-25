// Follows: Bondhugula et al., "A Practical Automatic Polyhedral Parallelizer
// and Locality Optimizer", PLDI 2008, section 2.1 (a polyhedron is the set of
// integer vectors x with A x + b >= 0), and Verdoolaege, "Presburger Formulas
// and Polyhedral Compilation", section 5.2 (the instance set).
//
// The loop nest
//
//   for (int i = 0; i < N; ++i)
//     for (int j = 0; j <= i; ++j)
//       body(i, j);
//
// visits a triangle. Here the triangle is written a second way, as rows of an
// integer matrix: each row (a_i, a_j, a_N, b) says a_i*i + a_j*j + a_N*N + b >= 0.
// N is a parameter: the same four rows describe the domain for every N.
// The program checks, point by point over a box larger than the triangle, that
// the rows admit exactly the points the loop visits.

#include <array>
#include <cstdio>
#include <set>
#include <utility>

struct Row {
    int a_i, a_j, a_N, b;
    const char* text;
};

constexpr std::array<Row, 4> domain{{
    {1, 0, 0, 0, "i >= 0"},
    {-1, 0, 1, -1, "N - 1 - i >= 0"},
    {0, 1, 0, 0, "j >= 0"},
    {1, -1, 0, 0, "i - j >= 0"},
}};

bool satisfies(int i, int j, int N) {
    for (const Row& r : domain)
        if (r.a_i * i + r.a_j * j + r.a_N * N + r.b < 0) return false;
    return true;
}

int main() {
    for (const Row& r : domain)
        std::printf("row (%2d, %2d, %2d, %2d): %s\n", r.a_i, r.a_j, r.a_N, r.b, r.text);

    for (int N = 1; N <= 6; ++N) {
        std::set<std::pair<int, int>> visited;
        for (int i = 0; i < N; ++i)
            for (int j = 0; j <= i; ++j) visited.insert({i, j});

        int admitted = 0, disagreements = 0;
        for (int i = -2; i < N + 2; ++i)      // a box two points wider on every side
            for (int j = -2; j < N + 2; ++j) {
                bool in = satisfies(i, j, N);
                admitted += in;
                disagreements += in != visited.contains({i, j});
            }
        std::printf("N = %d: loop visits %2zu, rows admit %2d, disagreements %d\n", N,
                    visited.size(), admitted, disagreements);
    }
}

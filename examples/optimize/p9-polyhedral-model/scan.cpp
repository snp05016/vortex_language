// Follows: Bondhugula et al., "A Practical Automatic Polyhedral Parallelizer
// and Locality Optimizer", PLDI 2008, section 5 (a code generator such as
// CLooG scans a polyhedron in the order a schedule gives), and Verdoolaege,
// "Presburger Formulas and Polyhedral Compilation", note 5.26 (polyhedral
// scanning).
//
// A schedule is not code. To run the triangle { (i, j) : 0 <= j <= i < N } in
// the order of theta1(i, j) = (i + j, j), a compiler must write new loops over
// w = i + j and c = j. Substituting i = w - c, j = c into the four inequalities
// and eliminating c (Fourier-Motzkin) gives the bounds used below:
//
//   0 <= w <= 2N - 2,   max(0, w - N + 1) <= c <= floor(w / 2).
//
// The program runs those loops and checks them against a plain sort of the
// domain by theta1: same points, same order, each point once.

#include <algorithm>
#include <cstdio>
#include <utility>
#include <vector>

constexpr int N = 5;
using Point = std::pair<int, int>;  // (i, j)

int main() {
    std::vector<Point> generated;
    for (int w = 0; w <= 2 * N - 2; ++w) {
        std::printf("w = %d:", w);
        for (int c = std::max(0, w - N + 1); c <= w / 2; ++c) {
            generated.push_back({w - c, c});
            std::printf(" (%d, %d)", w - c, c);
        }
        std::printf("\n");
    }

    std::vector<Point> sorted;
    for (int i = 0; i < N; ++i)
        for (int j = 0; j <= i; ++j) sorted.push_back({i, j});
    std::sort(sorted.begin(), sorted.end(), [](Point a, Point b) {
        return std::pair{a.first + a.second, a.second} < std::pair{b.first + b.second, b.second};
    });

    std::printf("%zu points generated, %zu in the domain, same order: %s\n", generated.size(),
                sorted.size(), generated == sorted ? "yes" : "no");
}

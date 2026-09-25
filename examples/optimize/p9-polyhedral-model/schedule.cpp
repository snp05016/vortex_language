// Follows: Bondhugula et al., "A Practical Automatic Polyhedral Parallelizer
// and Locality Optimizer", PLDI 2008, whose schedules are one integer-valued
// affine function per statement, compared lexicographically.
// https://doi.org/10.1145/1375581.1375595
//
// A schedule maps each point of an iteration domain to a "logical time": a
// vector compared lexicographically, the same comparison P7 used for distance
// vectors. The loop as written is one schedule among many. This toy compares
// it with a skewed schedule on the triangular domain { (i, j) : 0 <= j <= i
// < N } from domain.cpp, and prints each point in the order its schedule
// gives it.

#include <algorithm>
#include <cstdio>
#include <utility>
#include <vector>

constexpr int N = 5;

using Point = std::pair<int, int>;  // (i, j)

Point original(Point p) { return {p.first, p.second}; }             // theta0(i, j) = (i, j)
Point skewed(Point p)   { return {p.first + p.second, p.second}; }  // theta1(i, j) = (i + j, j)

template <typename Schedule>
void print_order(const std::vector<Point>& domain, Schedule theta, const char* name) {
    std::vector<Point> order = domain;
    std::sort(order.begin(), order.end(),
              [&](Point a, Point b) { return theta(a) < theta(b); });
    std::printf("%s:\n", name);
    for (auto [i, j] : order) {
        auto [w, c] = theta({i, j});
        std::printf("  (%d, %d) -> time (%d, %d)\n", i, j, w, c);
    }
}

int main() {
    std::vector<Point> domain;
    for (int i = 0; i < N; ++i)
        for (int j = 0; j <= i; ++j) domain.push_back({i, j});

    print_order(domain, original, "theta0 (row-major)");
    print_order(domain, skewed, "theta1 (skewed by i + j)");
}

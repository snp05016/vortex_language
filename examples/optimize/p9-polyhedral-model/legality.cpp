// Follows: Wolf and Lam, "A Data Locality Optimizing Algorithm", PLDI 1991
// (also cited in P7), whose legality test, T applied to every dependence
// distance must stay lexicographically positive, is generalized here from a
// unimodular matrix T to an arbitrary affine schedule theta.
// https://doi.org/10.1145/113445.113449
//
// The recurrence this domain computes reads (i - 1, j) and (i - 1, j - 1)
// when those points exist in the domain (this is Pascal's triangle: each
// entry is the sum of the two above it), and writes (i, j). dependences()
// finds every such reader-writer pair by brute force, independent of any
// schedule. legal() then asks, for one schedule at a time, whether every
// dependence still points forward in time under it.

#include <cstdio>
#include <utility>
#include <vector>

constexpr int N = 5;
using Point = std::pair<int, int>;

std::vector<std::pair<Point, Point>> dependences(const std::vector<Point>& domain) {
    auto in_domain = [](Point p) {
        return p.first >= 0 && p.second >= 0 && p.second <= p.first;
    };
    std::vector<std::pair<Point, Point>> deps;
    for (auto [i, j] : domain) {
        if (Point above{i - 1, j}; in_domain(above)) deps.push_back({above, {i, j}});
        if (Point diag{i - 1, j - 1}; in_domain(diag)) deps.push_back({diag, {i, j}});
    }
    return deps;
}

template <typename Schedule>
bool legal(const std::vector<std::pair<Point, Point>>& deps, Schedule theta, const char* name) {
    for (auto [s, t] : deps) {
        auto [sw, sc] = theta(s);
        auto [tw, tc] = theta(t);
        int dw = tw - sw, dc = tc - sc;
        bool positive = dw > 0 || (dw == 0 && dc > 0);
        if (!positive) {
            std::printf("%s: illegal, (%d,%d) -> (%d,%d) gives time delta (%d,%d)\n", name,
                        s.first, s.second, t.first, t.second, dw, dc);
            return false;
        }
    }
    std::printf("%s: legal for all %zu dependences\n", name, deps.size());
    return true;
}

int main() {
    std::vector<Point> domain;
    for (int i = 0; i < N; ++i)
        for (int j = 0; j <= i; ++j) domain.push_back({i, j});
    auto deps = dependences(domain);

    legal(deps, [](Point p) { return Point{p.first, p.second}; }, "theta0 (row-major)");
    legal(deps, [](Point p) { return Point{p.first + p.second, p.second}; }, "theta1 (skewed)");
    legal(deps, [](Point p) { return Point{-p.first, p.second}; }, "theta2 (reversed i)");
}

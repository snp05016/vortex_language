// Follows: Bondhugula et al., "A Practical Automatic Polyhedral Parallelizer
// and Locality Optimizer", PLDI 2008, section 3.1, Lemma 1: a schedule
// dimension may be tiled when every dependence moves forward or stays put
// along it (difference >= 0), not merely when the whole vector is
// lexicographically positive.
//
// The nest is P6's skewed stencil on a 4 x 4 square,
//   a[i][j] = a[i - 1][j + 1] + 1,
// whose only dependence has distance (1, -1). For each schedule the program
// asks three questions: is it legal, is every dimension non-negative for every
// dependence (a fully permutable band), and is the schedule still legal after
// cutting its two dimensions into 2 x 2 tiles that run one after another?

#include <cstdio>
#include <functional>
#include <vector>

constexpr int N = 4, T = 2;
struct Pt { int i, j; };
struct Time { int a, b; };
using Schedule = std::function<Time(Pt)>;

bool lex_positive(const std::vector<int>& d) {
    for (int x : d)
        if (x != 0) return x > 0;
    return false;
}

int floor_div(int x, int t) { return (x >= 0 ? x : x - t + 1) / t; }

void check(const char* name, const Schedule& theta, const std::vector<std::pair<Pt, Pt>>& deps) {
    const char *legal = "yes", *permutable = "yes", *tiled = "yes";
    for (auto [s, t] : deps) {
        Time u = theta(s), v = theta(t);
        if (!lex_positive({v.a - u.a, v.b - u.b})) legal = "no";
        if (v.a - u.a < 0 || v.b - u.b < 0) permutable = "no";
        // Tiled schedule: (tile of a, tile of b, a, b), compared lexicographically.
        std::vector<int> d{floor_div(v.a, T) - floor_div(u.a, T),
                           floor_div(v.b, T) - floor_div(u.b, T), v.a - u.a, v.b - u.b};
        if (!lex_positive(d)) tiled = "no";
    }
    std::printf("%-22s legal: %-3s  permutable: %-3s  2x2 tiles legal: %s\n", name, legal,
                permutable, tiled);
}

int main() {
    std::vector<std::pair<Pt, Pt>> deps;  // (writer, reader)
    for (int i = 1; i < N; ++i)
        for (int j = 0; j + 1 < N; ++j) deps.push_back({{i - 1, j + 1}, {i, j}});
    std::printf("%zu dependences, each with distance (1, -1)\n", deps.size());

    check("(i, j) as written", [](Pt p) { return Time{p.i, p.j}; }, deps);
    check("(j, i) interchanged", [](Pt p) { return Time{p.j, p.i}; }, deps);
    check("(i, i + j) skewed", [](Pt p) { return Time{p.i, p.i + p.j}; }, deps);
}

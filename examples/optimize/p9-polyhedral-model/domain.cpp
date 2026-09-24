// Follows: Bondhugula et al., "A Practical Automatic Polyhedral Parallelizer
// and Locality Optimizer", PLDI 2008, whose iteration domains are sets of
// integer points bounded by affine inequalities.
// https://doi.org/10.1145/1375581.1375595
//
// A loop nest's iteration domain is not "the loop": it is the set of integer
// points the loop visits, independent of any order the loop happens to visit
// them in. This toy enumerates the domain of
//
//   for (int i = 0; i < N; ++i)
//     for (int j = 0; j <= i; ++j)
//       body(i, j);
//
// which is the triangle { (i, j) : 0 <= j <= i < N }: one affine inequality
// per loop bound, plus one relating i and j. in_domain re-tests each point
// against that description directly, so the loop's shape and the domain's
// description can be checked against each other instead of trusted blindly.

#include <cstdio>

constexpr int N = 6;

bool in_domain(int i, int j) {
    return 0 <= i && i < N && 0 <= j && j <= i;
}

int main() {
    int count = 0;
    for (int i = 0; i < N; ++i) {
        for (int j = 0; j <= i; ++j) {
            if (!in_domain(i, j)) continue;  // never taken: the loop already matches the domain
            std::printf("(%d, %d)\n", i, j);
            ++count;
        }
    }
    std::printf("%d points, N = %d, N*(N+1)/2 = %d\n", count, N, N * (N + 1) / 2);
}

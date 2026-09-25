// program: valid
// Follows: Ulrich Drepper, "What Every Programmer Should Know About Memory",
// section 6.2.1 (matrix multiplication and cache access patterns).
#include <cstdio>

// Apple M4 Pro, `sysctl hw.cachelinesize`, checked 2026-09-24. Query it on
// your own machine; this constant is only for a repeatable example.
constexpr int kLineBytes = 128;
constexpr int kFloatBytes = 4;
constexpr int kElemsPerLine = kLineBytes / kFloatBytes;

constexpr int kRows = 8;
constexpr int kCols = 64;

// Row-major storage: element (r, c) sits at offset (r * kCols + c) elements
// from the start, matching a Vortex `[f32; kRows, kCols]` array's layout
// (contiguous, last index fastest).
int line_of(int r, int c) {
    return (r * kCols + c) / kElemsPerLine;
}

// Counts how many times consecutive visits land in a different cache line.
// A traversal that stays inside one line for a while has few transitions;
// one that jumps lines on every step has almost as many transitions as visits.
int line_transitions(bool row_major_order) {
    int prev_line = -1;
    int transitions = 0;
    auto visit = [&](int r, int c) {
        int line = line_of(r, c);
        if (line != prev_line) {
            ++transitions;
            prev_line = line;
        }
    };
    if (row_major_order) {
        for (int r = 0; r < kRows; ++r)
            for (int c = 0; c < kCols; ++c)
                visit(r, c);
    } else {
        for (int c = 0; c < kCols; ++c)
            for (int r = 0; r < kRows; ++r)
                visit(r, c);
    }
    return transitions;
}

int main() {
    const int total_visits = kRows * kCols;
    std::printf("elements visited: %d, elements per line: %d\n", total_visits, kElemsPerLine);
    std::printf("lines the array occupies: %d\n", total_visits / kElemsPerLine);
    std::printf("line transitions, row order (stride 1 element):     %d\n", line_transitions(true));
    std::printf("line transitions, column order (stride %d elements): %d\n", kCols, line_transitions(false));
    return 0;
}

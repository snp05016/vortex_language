#include <array>
#include <cstdio>
#include <functional>
#include <thread>
#include <vector>

namespace {

constexpr int kRows = 8;
constexpr int kCols = 8;

// A fixed, deterministic grid: no input file, no randomness.
int cell(int row, int col) { return row * kCols + col + 1; }

// Sums one contiguous range of rows into out[first..last).
// Every call touches a different, disjoint slice of out, so no two threads
// ever write the same element: no lock is needed to make this correct.
void sum_rows(int first, int last, std::array<int, kRows>& out) {
    for (int row = first; row < last; ++row) {
        int total = 0;
        for (int col = 0; col < kCols; ++col) {
            total += cell(row, col);
        }
        out[row] = total;
    }
}

}  // namespace

int main() {
    std::array<int, kRows> out{};
    constexpr int kThreads = 4;
    static_assert(kRows % kThreads == 0);
    constexpr int kRowsPerThread = kRows / kThreads;

    std::vector<std::thread> workers;
    for (int t = 0; t < kThreads; ++t) {
        int first = t * kRowsPerThread;
        int last = first + kRowsPerThread;
        // Thread arguments are copied unless wrapped: std::ref shares `out`.
        workers.emplace_back(sum_rows, first, last, std::ref(out));
    }
    // Join before reading `out`, and before a joinable thread is destroyed,
    // which would call std::terminate.
    for (std::thread& w : workers) {
        w.join();
    }

    int total = 0;
    for (int row = 0; row < kRows; ++row) {
        std::printf("row %d: %d\n", row, out[row]);
        total += out[row];
    }
    std::printf("total: %d\n", total);
    return 0;
}

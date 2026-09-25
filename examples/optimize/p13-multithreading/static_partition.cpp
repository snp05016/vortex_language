#include <cstdio>

// A static schedule for a word-frequency job: `items` pages of text are
// handed out to `workers` threads as contiguous ranges, decided before any
// thread starts. Thread t gets [t * items / workers, (t + 1) * items / workers).
struct Range {
    int first;
    int last;  // one past the end
};

Range share(int t, int items, int workers) {
    return {t * items / workers, (t + 1) * items / workers};
}

// The two facts a parallel split must establish before it runs: the ranges
// do not overlap, and together they cover every item exactly once. Checked
// here by counting how many ranges claim each item.
bool is_partition(int items, int workers) {
    for (int i = 0; i < items; ++i) {
        int owners = 0;
        for (int t = 0; t < workers; ++t) {
            const Range r = share(t, items, workers);
            owners += (r.first <= i && i < r.last) ? 1 : 0;
        }
        if (owners != 1) {
            return false;
        }
    }
    return true;
}

int main() {
    constexpr int kItems = 8;
    constexpr int kWorkerCounts[] = {2, 3, 4, 5};
    for (int workers : kWorkerCounts) {
        std::printf("%d workers:", workers);
        int largest = 0;
        for (int t = 0; t < workers; ++t) {
            const Range r = share(t, kItems, workers);
            std::printf(" [%d,%d)", r.first, r.last);
            if (r.last - r.first > largest) {
                largest = r.last - r.first;
            }
        }
        // With equal-speed cores the job ends when the largest share ends;
        // the ideal would be kItems / workers.
        std::printf("  largest share %d of %d, partition %s\n", largest, kItems,
                    is_partition(kItems, workers) ? "ok" : "BROKEN");
    }
    return 0;
}

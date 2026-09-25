// Follows: Williams, Waterman and Patterson, "Roofline: An Insightful
// Visual Performance Model for Multicore Architectures", CACM 52(4), 2009,
// section 3 (operational intensity counts DRAM traffic) and section 5
// (compulsory misses set the least traffic).
//
// Counts the flops of C = A * B for n x n f32 matrices and estimates the
// bytes that cross between the last cache and DRAM for three kinds of
// schedule. The byte counts come from a model with stated assumptions;
// nothing here is measured.
#include <cstdio>
#include <initializer_list>

constexpr long long elem = 4; // bytes in one f32

// One multiply-add per innermost step, counted as two flops.
long long flops(long long n) { return 2 * n * n * n; }

// Assumption: a block of B stays in cache while t rows of C use it and is
// then evicted, so all of B crosses from DRAM n / t times; A and C cross
// once each. t = 1 is the plain loop on a cache that cannot keep B from
// one row of C to the next. t = n moves every byte once: the compulsory
// traffic, which no schedule can go below.
long long dram_bytes(long long n, long long t) {
    return (n / t) * n * n * elem + 2 * n * n * elem;
}

void table(long long n) {
    std::printf("n = %lld: %lld flops, working set %lld KiB\n", n, flops(n),
                3 * n * n * elem / 1024);
    for (long long t : {1LL, 4LL, 16LL, 32LL, n}) {
        char name[32];
        if (t == 1) std::snprintf(name, sizeof name, "naive");
        else if (t == n) std::snprintf(name, sizeof name, "compulsory");
        else std::snprintf(name, sizeof name, "tiled, t = %lld", t);
        long long b = dram_bytes(n, t);
        std::printf("  %-15s %13lld bytes %9.3f flops/byte\n", name, b,
                    static_cast<double>(flops(n)) / static_cast<double>(b));
    }
}

int main() {
    table(64);   // the stage 10 kernel's size
    table(2048); // large enough that the working set outgrows the caches
}

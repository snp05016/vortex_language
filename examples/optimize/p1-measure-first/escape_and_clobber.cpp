// Keeping a timing loop's work alive. `work` is pure and its input never
// changes, so at -O2 a compiler may run it once, or even at compile time, and
// reuse the answer. Two empty inline-assembly statements stop that: one makes
// the compiler assume the input may have changed, the other that the result
// was read. Open the example in Compiler Explorer and delete the two calls to
// see the loop fold to a constant. With them, look at what is left of each
// call's eight steps: the barriers keep the calls, not the work inside them.
//
// Follows: google/benchmark user guide, section "Preventing Optimization"
// (the idea of DoNotOptimize and ClobberMemory, rewritten from scratch).
#include <cstdio>

// "+r": the asm may read and rewrite `value`, so nothing known about it
// before this point may be used after it.
template <typename T>
inline void assumeChanged(T& value) {
    asm volatile("" : "+r"(value));
}

// "r" plus a "memory" clobber: the asm reads `value` and may read or write
// any memory, so the value must exist and pending stores must be done.
template <typename T>
inline void assumeRead(const T& value) {
    asm volatile("" : : "r"(value) : "memory");
}

static unsigned long work(unsigned long seed) {
    unsigned long x = seed;
    for (int i = 0; i < 8; ++i) x = x * 1103515245u + 12345u;
    return x;
}

int main() {
    const int reps = 5;
    unsigned long input = 42;
    unsigned long total = 0;
    for (int i = 0; i < reps; ++i) {
        assumeChanged(input);  // so work(input) cannot be computed once and reused
        unsigned long result = work(input);
        assumeRead(result);    // so the call cannot be deleted as unused
        total += result;
    }
    std::printf("reps=%d total=%lu\n", reps, total);
}

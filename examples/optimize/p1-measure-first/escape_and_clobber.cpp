// Two inline-asm "escape hatches" that stop an optimizer from doing to a
// benchmark loop what O1's as-if rule otherwise allows: hoisting a pure call
// out of a loop whose visible input never changes, or deleting work whose
// result is not otherwise read. Open this file in Compiler Explorer (the
// link on the page) and compare the assembly with the escape calls removed.
//
// Follows: google/benchmark user guide, section "Preventing Optimization"
// (the technique, reimplemented here from its description, not its code).
#include <cstdio>

template <typename T>
inline void escape(T const& value) {
    asm volatile("" : : "g"(value) : "memory");
}

inline void clobberMemory() {
    asm volatile("" : : : "memory");
}

// Pure and deterministic: legal for the compiler to compute once and reuse,
// unless the loop convinces it that the input or the result might differ
// each time around.
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
        escape(input);            // "the input might have changed": no hoisting past here
        unsigned long result = work(input);
        escape(result);           // "the result was used": no dead-code elimination
        total += result;
        clobberMemory();          // "memory might have changed": no reordering across this point
    }

    std::printf("reps=%d total=%lu\n", reps, total);
}

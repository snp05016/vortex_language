#include <cstdio>

// Two threads each add 1 to a shared counter three times. A real GPU or CPU
// schedules the two threads' instructions in some interleaving the program
// does not control; this fixes one interleaving by hand, so the trace is
// exactly reproducible, and shows the interleaving a barrier-free,
// non-atomic increment can hit.
int racing_increments() {
    int counter = 0;
    for (int round = 0; round < 3; ++round) {
        // Non-atomic "load, add, store" split into its steps, interleaved
        // so that both threads read before either writes: the classic
        // lost update. Thread 0's write is overwritten by thread 1's.
        int r0 = counter;       // thread 0 reads
        int r1 = counter;       // thread 1 reads the same old value
        counter = r0 + 1;       // thread 0 writes
        counter = r1 + 1;       // thread 1 writes, discarding thread 0's add
    }
    return counter;
}

int atomic_increments() {
    int counter = 0;
    for (int round = 0; round < 3; ++round) {
        // An atomic read-modify-write has no window between the read and
        // the write for another thread to step in, so this fixed trace
        // has only one legal interleaving per round.
        counter = counter + 1;  // thread 0's atomic add
        counter = counter + 1;  // thread 1's atomic add
    }
    return counter;
}

int main() {
    std::printf("expected: 6\n");
    std::printf("without synchronization: %d\n", racing_increments());
    std::printf("with atomic increments:  %d\n", atomic_increments());
}

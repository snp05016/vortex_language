#include <bit>
#include <cstdio>
#include <string>

// Two threads each add 1 to a shared counter that starts at 0. A plain
// increment is two memory steps, a load into a register and a store of the
// register plus one, and another thread's steps may fall between them. This
// program tries every interleaving that keeps each thread's own steps in
// order, instead of waiting for a real scheduler to pick one.
struct Step { int thread; bool is_load; };

int run(const Step* schedule, int steps, bool atomic_rmw) {
    int counter = 0;
    int reg[2] = {0, 0};
    for (int s = 0; s < steps; ++s) {
        int t = schedule[s].thread;
        if (atomic_rmw) {
            counter = counter + 1;  // one indivisible step: nothing can fall between
        } else if (schedule[s].is_load) {
            reg[t] = counter;
        } else {
            counter = reg[t] + 1;
        }
    }
    return counter;
}

int main() {
    // Plain increments: each thread has a load then a store, so a schedule is
    // a choice of which 2 of the 4 slots belong to thread 0 (6 choices).
    int ones = 0, twos = 0;
    for (int mask = 0; mask < 16; ++mask) {
        if (std::popcount(static_cast<unsigned>(mask)) != 2) continue;
        Step schedule[4];
        bool loaded[2] = {false, false};
        std::string trace;
        for (int s = 0; s < 4; ++s) {
            int t = (mask >> s) & 1 ? 0 : 1;
            schedule[s] = {t, !loaded[t]};
            trace += (loaded[t] ? "S" : "L") + std::to_string(t) + " ";
            loaded[t] = true;
        }
        int result = run(schedule, 4, false);
        (result == 2 ? twos : ones) += 1;
        std::printf("%s-> %d\n", trace.c_str(), result);
    }
    std::printf("plain: %d schedules give 2, %d give 1\n", twos, ones);

    // Atomic increments: one step per thread, so only two schedules exist.
    Step a[2] = {{0, true}, {1, true}};
    Step b[2] = {{1, true}, {0, true}};
    std::printf("atomic: %d and %d\n", run(a, 2, true), run(b, 2, true));
}

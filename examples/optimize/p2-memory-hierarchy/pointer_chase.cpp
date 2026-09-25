// program: valid
// Follows: Ulrich Drepper, "What Every Programmer Should Know About Memory",
// section 3.3.2 (a circular list walked in sequential or random order).
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

// A fixed 64-bit linear congruential generator: the same shuffle everywhere.
std::uint64_t next_random(std::uint64_t& s) {
    s = s * 6364136223846793005ull + 1442695040888963407ull;
    return s >> 33;
}

// next[i] is the slot visited after slot i. Linking a shuffled visiting order
// end to end, last back to first, always makes one cycle through every slot.
std::vector<std::size_t> make_chain(std::size_t slots, bool shuffled) {
    std::vector<std::size_t> order(slots), next(slots);
    for (std::size_t i = 0; i < slots; ++i) order[i] = i;
    std::uint64_t state = 42;
    if (shuffled)
        for (std::size_t i = slots - 1; i > 0; --i)
            std::swap(order[i], order[next_random(state) % (i + 1)]);
    for (std::size_t i = 0; i < slots; ++i) next[order[i]] = order[(i + 1) % slots];
    return next;
}

// Each load needs the previous load's result, so no two loads can overlap.
std::size_t chase(const std::vector<std::size_t>& next, std::size_t steps) {
    std::size_t p = 0;
    for (std::size_t i = 0; i < steps; ++i) p = next[p];
    return p;
}

// Counts hops that stay inside one 128-byte line (16 slots of 8 bytes).
std::size_t same_line_hops(const std::vector<std::size_t>& next) {
    std::size_t same = 0;
    for (std::size_t i = 0; i < next.size(); ++i) same += (i / 16 == next[i] / 16);
    return same;
}

int main(int argc, char** argv) {
    if (argc > 1 && std::strcmp(argv[1], "--time") == 0) {  // run this yourself
        for (std::size_t bytes = 16 << 10; bytes <= std::size_t{256} << 20; bytes *= 2) {
            auto next = make_chain(bytes / sizeof(std::size_t), true);
            chase(next, next.size());  // touch every slot once first
            std::vector<double> ns;
            for (int run = 0; run < 5; ++run) {
                auto t0 = std::chrono::steady_clock::now();
                volatile std::size_t sink = chase(next, std::size_t{1} << 24);
                auto t1 = std::chrono::steady_clock::now();
                (void)sink;  // the volatile store keeps the chase from being deleted
                ns.push_back(std::chrono::duration<double, std::nano>(t1 - t0).count() / (1 << 24));
            }
            std::sort(ns.begin(), ns.end());
            std::printf("%9zu KiB  median %.2f ns per load\n", bytes >> 10, ns[2]);
        }
        return 0;
    }
    std::printf("%8s %14s %22s\n", "slots", "cycle length", "same-line hops");
    for (std::size_t slots : {4096u, 1048576u})
        for (bool shuffled : {false, true}) {
            auto next = make_chain(slots, shuffled);
            std::size_t length = 1;
            for (std::size_t p = next[0]; p != 0; p = next[p]) ++length;
            std::printf("%8zu %14zu %12zu (%s)\n", slots, length, same_line_hops(next),
                        shuffled ? "random" : "sequential");
        }
    return 0;
}

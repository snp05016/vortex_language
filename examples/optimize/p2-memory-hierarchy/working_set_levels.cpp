// program: valid
// Follows: Ulrich Drepper, "What Every Programmer Should Know About Memory",
// section 3.3.2 (working sets that do or do not fit a cache level).
#include <cstdio>
#include <vector>

// A toy two-level hierarchy, counted in lines: L1 holds 8, L2 holds 32. Each
// level is fully associative and evicts its least recently used line.
class Level {
public:
    explicit Level(unsigned lines) : capacity_(lines) {}
    bool access(unsigned line) {  // true on a hit; either way `line` becomes most recent
        bool hit = false;
        for (unsigned i = 0; i < lru_.size(); ++i)
            if (lru_[i] == line) { lru_.erase(lru_.begin() + i); hit = true; break; }
        if (!hit && lru_.size() == capacity_) lru_.erase(lru_.begin());
        lru_.push_back(line);
        return hit;
    }

private:
    unsigned capacity_;
    std::vector<unsigned> lru_;  // least recent first
};

// A fixed linear congruential generator, so the output is the same everywhere.
unsigned next_random(unsigned& state) { return state = state * 1103515245u + 12345u; }

void sweep(unsigned lines, bool random_order) {
    Level l1(8), l2(32);
    unsigned state = 1, served[3] = {0, 0, 0}, order[64];
    for (unsigned i = 0; i < lines; ++i) order[i] = i;
    const unsigned passes = 17;  // the first pass only fills the caches
    for (unsigned pass = 0; pass < passes; ++pass) {
        if (random_order)  // a fresh shuffle each pass (Fisher-Yates)
            for (unsigned i = lines - 1; i > 0; --i) {
                unsigned j = (next_random(state) >> 16) % (i + 1), t = order[i];
                order[i] = order[j]; order[j] = t;
            }
        for (unsigned i = 0; i < lines; ++i) {
            // L2 is asked only when L1 misses, as in a real hierarchy.
            unsigned where = l1.access(order[i]) ? 0 : l2.access(order[i]) ? 1 : 2;
            if (pass > 0) ++served[where];
        }
    }
    unsigned total = lines * (passes - 1);  // percentages below are rounded down
    std::printf("%6u %9s %5u%% %5u%% %6u%%\n", lines, random_order ? "random" : "cyclic",
                100 * served[0] / total, 100 * served[1] / total, 100 * served[2] / total);
}

int main() {
    std::printf("%6s %9s %6s %6s %7s\n", "lines", "order", "L1", "L2", "memory");
    for (unsigned lines : {4u, 8u, 9u, 16u, 32u, 33u, 64u}) sweep(lines, false);
    for (unsigned lines : {4u, 8u, 9u, 16u, 32u, 33u, 64u}) sweep(lines, true);
    return 0;
}

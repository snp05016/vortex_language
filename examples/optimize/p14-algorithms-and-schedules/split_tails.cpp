// Splitting a loop of 7 iterations by a factor of 3 leaves a tail of 1.
// Three ways to handle the tail, each written out as the loops it produces.
// The body records which x it ran for, so the printout shows the order and
// how many times each point was evaluated.
#include <array>
#include <cstdio>
#include <vector>

constexpr int extent = 7;
constexpr int factor = 3;

struct Trace {
    std::vector<int> order;
    std::array<int, extent> count{};
    void body(int x) { order.push_back(x); ++count[x]; }
};

// Guard: round the outer loop up and test every inner iteration.
Trace guard() {
    Trace t;
    for (int xo = 0; xo < (extent + factor - 1) / factor; ++xo)
        for (int xi = 0; xi < factor; ++xi) {
            int x = xo * factor + xi;
            if (x < extent) t.body(x);
        }
    return t;
}

// Shift inward: the last block slides left so that it ends at the extent.
// Every inner loop runs exactly `factor` times, with no test inside it.
Trace shift_inward() {
    Trace t;
    for (int xo = 0; xo < (extent + factor - 1) / factor; ++xo) {
        int base = xo * factor;
        if (base > extent - factor) base = extent - factor;
        for (int xi = 0; xi < factor; ++xi) t.body(base + xi);
    }
    return t;
}

// Cut: full blocks first, then a separate loop for what is left over.
Trace cut() {
    Trace t;
    for (int xo = 0; xo < extent / factor; ++xo)
        for (int xi = 0; xi < factor; ++xi) t.body(xo * factor + xi);
    for (int xi = 0; xi < extent % factor; ++xi) t.body(extent / factor * factor + xi);
    return t;
}

void show(const char* name, const Trace& t) {
    std::printf("%-12s order:", name);
    for (int x : t.order) std::printf(" %d", x);
    std::printf("\n%-12s count:", "");
    for (int c : t.count) std::printf(" %d", c);
    std::printf("   (%zu evaluations)\n", t.order.size());
}

int main() {
    show("guard", guard());
    show("shift_inward", shift_inward());
    show("cut", cut());
}

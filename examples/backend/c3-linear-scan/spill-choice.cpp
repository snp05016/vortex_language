// Linear scan with the spill decision made pluggable, run three times on
// the same five intervals and two registers. Each interval carries a use
// count, standing in for how often the program reads or writes it: a
// spilled interval pays one memory access per use. The point: "furthest
// end" minimizes how many intervals are spilled, but not how many memory
// accesses the spills cost. Which register each survivor gets is left out:
// only the spill decision differs between the three runs.
//
// Follows: Poletto and Sarkar, "Linear Scan Register Allocation", ACM
// TOPLAS 21(5), 1999: section 4.1 (spill the interval that ends last) and
// section 6.3 (the alternative that spills the least-used interval).

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

struct Interval {
    std::string name;
    int start;
    int end;
    int uses;
    bool spilled = false;
};

// A rule looks at the intervals competing for the registers (everything
// in active plus the newcomer) and names the one to send to memory.
using Rule = Interval* (*)(const std::vector<Interval*>& candidates);

Interval* newcomer(const std::vector<Interval*>& c) { return c.back(); }

Interval* furthest_end(const std::vector<Interval*>& c) {
    return *std::max_element(c.begin(), c.end(),
        [](Interval* x, Interval* y) { return x->end < y->end; });
}

Interval* fewest_uses(const std::vector<Interval*>& c) {
    // Ties go to the interval that ends later, as in furthest_end.
    return *std::min_element(c.begin(), c.end(), [](Interval* x, Interval* y) {
        return x->uses != y->uses ? x->uses < y->uses : x->end > y->end;
    });
}

void run(const char* label, Rule choose, int registers) {
    std::vector<Interval> intervals = {   // already sorted by start
        {"p", 1, 10, 8}, {"q", 2, 4, 1}, {"r", 3, 5, 2},
        {"s", 5, 7, 2}, {"t", 6, 8, 2},
    };
    std::vector<Interval*> active;
    for (Interval& current : intervals) {
        std::erase_if(active, [&](Interval* it) { return it->end < current.start; });
        active.push_back(&current);   // newcomer is last among the candidates
        if (static_cast<int>(active.size()) > registers) {
            Interval* victim = choose(active);
            victim->spilled = true;
            std::erase(active, victim);
        }
    }
    int count = 0, accesses = 0;
    std::printf("%-13s spilled:", label);
    for (const Interval& it : intervals) {
        if (!it.spilled) continue;
        std::printf(" %s", it.name.c_str());
        ++count;
        accesses += it.uses;
    }
    std::printf("  (%d interval%s, %d memory accesses)\n",
                count, count == 1 ? "" : "s", accesses);
}

int main() {
    run("newcomer", newcomer, 2);
    run("furthest end", furthest_end, 2);
    run("fewest uses", fewest_uses, 2);
}

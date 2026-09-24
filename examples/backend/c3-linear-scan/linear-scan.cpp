// A minimal implementation of Poletto and Sarkar's linear-scan register
// allocator, run on eight made-up live intervals over 3 registers. The
// intervals are invented for this example, not taken from any real
// program; the point is the algorithm's three moves: expire an interval
// whose register is free again, allocate a free register to a new one, or
// spill when none is free, always giving up the interval whose live range
// ends furthest in the future.
//
// Follows: Poletto and Sarkar, "Linear Scan Register Allocation", ACM
// TOPLAS 21(5), 1999, section 3, "LinearScanRegisterAllocation".

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

struct Interval {
    std::string name;
    int start;
    int end;
    int reg = -1;       // assigned register, or -1 once spilled
    bool spilled = false;
};

// Intervals currently holding a register, kept sorted by increasing end
// point: the one expiring soonest is always at the front.
using Active = std::vector<Interval*>;

void expire_old_intervals(Active& active, int current_start,
                           std::vector<int>& free_regs) {
    while (!active.empty() && active.front()->end < current_start) {
        Interval* it = active.front();
        active.erase(active.begin());
        free_regs.push_back(it->reg);
        std::printf("  expire %-2s (frees r%d)\n", it->name.c_str(), it->reg);
    }
    std::sort(free_regs.begin(), free_regs.end());
}

void spill_at_interval(Interval& current, Active& active) {
    // active is sorted by increasing end, so its last element has the
    // furthest endpoint: giving up its register costs the least, because
    // it has the most instructions left to run without one anyway.
    Interval* candidate = active.back();
    if (candidate->end > current.end) {
        current.reg = candidate->reg;
        candidate->reg = -1;
        candidate->spilled = true;
        active.back() = &current;
        std::sort(active.begin(), active.end(),
                  [](Interval* a, Interval* b) { return a->end < b->end; });
        std::printf("  spill  %-2s instead: it ends later, %s keeps r%d\n",
                    candidate->name.c_str(), current.name.c_str(), current.reg);
    } else {
        current.spilled = true;
        std::printf("  spill  %-2s: nothing active ends after it\n",
                    current.name.c_str());
    }
}

void linear_scan(std::vector<Interval>& intervals, int num_registers) {
    std::sort(intervals.begin(), intervals.end(),
              [](const Interval& a, const Interval& b) { return a.start < b.start; });

    Active active;
    std::vector<int> free_regs;
    for (int r = 0; r < num_registers; ++r) free_regs.push_back(r);

    for (Interval& current : intervals) {
        std::printf("%-2s [%2d,%2d]:\n", current.name.c_str(), current.start, current.end);
        expire_old_intervals(active, current.start, free_regs);

        if (static_cast<int>(active.size()) == num_registers) {
            spill_at_interval(current, active);
        } else {
            current.reg = free_regs.front();
            free_regs.erase(free_regs.begin());
            active.push_back(&current);
            std::sort(active.begin(), active.end(),
                      [](Interval* a, Interval* b) { return a->end < b->end; });
            std::printf("  allocate r%d\n", current.reg);
        }
    }
}

int main() {
    std::vector<Interval> intervals = {
        {"a", 1, 8}, {"b", 2, 4}, {"c", 3, 9}, {"d", 5, 6},
        {"e", 6, 10}, {"f", 7, 7}, {"g", 8, 12}, {"h", 10, 11},
    };

    linear_scan(intervals, 3);

    std::printf("\nfinal assignment:\n");
    std::sort(intervals.begin(), intervals.end(),
              [](const Interval& a, const Interval& b) { return a.name < b.name; });
    for (const Interval& it : intervals) {
        if (it.spilled) {
            std::printf("  %-2s [%2d,%2d] spilled\n", it.name.c_str(), it.start, it.end);
        } else {
            std::printf("  %-2s [%2d,%2d] r%d\n", it.name.c_str(), it.start, it.end, it.reg);
        }
    }
}

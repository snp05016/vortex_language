// A checker for a register assignment: given intervals with an assigned
// register (or "spilled"), it confirms that no two intervals sharing a
// register overlap. This is the independent test an allocator's output
// should be run against, built on data separate from the code that
// produced it. The first table is the assignment linear-scan.cpp computed;
// the second is the same table with one register changed by hand, to show
// the checker catching the conflict that change creates.
//
// Follows: Poletto and Sarkar, "Linear Scan Register Allocation", ACM
// TOPLAS 21(5), 1999, section 4: interference among live intervals is
// captured by whether they overlap, so this checker compares ranges.

#include <cstdio>
#include <string>
#include <vector>

struct Assignment {
    std::string name;
    int start;
    int end;
    int reg;   // -1 means "spilled": no register, so never conflicts
};

bool overlaps(const Assignment& a, const Assignment& b) {
    return a.start <= b.end && b.start <= a.end;
}

int check(const std::vector<Assignment>& assignments) {
    int violations = 0;
    for (std::size_t i = 0; i < assignments.size(); ++i) {
        for (std::size_t j = i + 1; j < assignments.size(); ++j) {
            const Assignment& a = assignments[i];
            const Assignment& b = assignments[j];
            if (a.reg == -1 || b.reg == -1) continue;
            if (a.reg == b.reg && overlaps(a, b)) {
                std::printf("  conflict: %s and %s both in r%d, ranges [%d,%d] and [%d,%d]\n",
                            a.name.c_str(), b.name.c_str(), a.reg,
                            a.start, a.end, b.start, b.end);
                ++violations;
            }
        }
    }
    return violations;
}

int main() {
    // The assignment linear-scan.cpp printed for the same eight intervals.
    std::vector<Assignment> correct = {
        {"a", 1, 8, 0}, {"b", 2, 4, 1}, {"c", 3, 9, 2}, {"d", 5, 6, 1},
        {"e", 6, 10, -1}, {"f", 7, 7, 1}, {"g", 8, 12, 1}, {"h", 10, 11, 0},
    };
    std::printf("checking the linear-scan assignment:\n");
    int found = check(correct);
    std::printf("  %d conflict(s)\n", found);

    // The same table, with g's register changed from r1 to r2 by hand.
    // g spans [8,12] and c spans [3,9]: they overlap at 8 and 9, so
    // giving them the same register is wrong, and the checker must say so.
    std::vector<Assignment> broken = correct;
    for (Assignment& entry : broken) {
        if (entry.name == "g") entry.reg = 2;
    }
    std::printf("checking a broken assignment (g moved to r2):\n");
    found = check(broken);
    std::printf("  %d conflict(s)\n", found);
}

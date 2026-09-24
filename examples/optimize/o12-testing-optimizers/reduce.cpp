// Test-case reduction: given a failing test with irrelevant lines mixed in,
// delete what you can while the failure keeps happening. This is a small,
// deterministic 1-minimizing pass in the spirit of ddmin, run to a fixed
// point (no further single line can be removed).
// Follows: Regehr, Chen, Cuoq, Eide, Ellison, Yang, PLDI 2012 (see .toml).
#include <iostream>
#include <string>
#include <vector>

using Program = std::vector<std::string>;

// Stands in for "still reproduces the bug from differential_test.cpp": that
// harness needs a negative left-hand side reaching a divide-by-two, so the
// interesting property is "both lines are present, in this order".
bool isInteresting(const Program& lines) {
    int negativeAssignment = -1;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (lines[i] == "x = -3") negativeAssignment = static_cast<int>(i);
        if (lines[i] == "divide_by_two(x)" && negativeAssignment >= 0
            && negativeAssignment < static_cast<int>(i))
            return true;
    }
    return false;
}

// One pass over the current lines: try deleting each one; keep the deletion
// only when the remainder is still interesting. Returns the smaller program.
Program reducePass(const Program& lines) {
    Program current = lines;
    for (std::size_t i = 0; i < current.size();) {
        Program candidate = current;
        candidate.erase(candidate.begin() + static_cast<long>(i));
        if (isInteresting(candidate)) {
            std::cout << "removed line: " << current[i] << "\n";
            current = candidate; // do not advance: the next line shifted down
        } else {
            ++i;
        }
    }
    return current;
}

int main() {
    Program original = {
        "x = 5", "y = x + 1", "print(y)", "x = -3", "z = 9",
        "divide_by_two(x)", "w = z * 2", "print(w)",
    };

    Program minimized = original;
    while (true) {
        Program next = reducePass(minimized);
        if (next.size() == minimized.size()) break; // fixed point: no pass helped
        minimized = next;
    }

    std::cout << "minimized from " << original.size() << " to "
              << minimized.size() << " lines:\n";
    for (const auto& line : minimized) std::cout << "  " << line << "\n";
    return 0;
}

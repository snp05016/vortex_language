// Equivalence modulo inputs, in miniature: for one fixed input, a profile
// says which guarded statements never run; pruning them must not change that
// input's output. The "profile" here is a reachability check with a planted
// bug (it ignores the guard's magnitude, only its sign), which is exactly the
// kind of static-analysis mistake EMI is good at catching.
// Follows: Le, Afshari, Su, PLDI 2014 (see .toml).
#include <iostream>
#include <vector>

struct Statement {
    int threshold;  // this line runs only when input > threshold
    int delta;      // its effect on the running total when it runs
};

// The real, unambiguous rule: run every statement whose guard holds.
int runOriginal(const std::vector<Statement>& program, int input) {
    int total = 0;
    for (const auto& s : program)
        if (input > s.threshold) total += s.delta;
    return total;
}

// The profiler's rule: prune a statement when it *looks* unreachable for
// this input. The bug: it compares signs instead of magnitudes, so it wrongly
// keeps some statements whose threshold the input does not actually clear.
bool profilerThinksReachable(const Statement& s, int input) {
    bool inputPositive = input > 0;
    bool thresholdPositive = s.threshold > 0;
    return inputPositive != thresholdPositive || input > s.threshold;
}

int runPruned(const std::vector<Statement>& program, int input) {
    int total = 0;
    for (const auto& s : program)
        if (profilerThinksReachable(s, input)) total += s.delta;
    return total;
}

int main() {
    // Ten statements with a mix of thresholds; ten fixed inputs, no
    // randomness needed because the bug does not depend on a rare draw.
    std::vector<Statement> program = {
        {-5, 3}, {2, -4}, {10, 7}, {-1, 2}, {0, -6},
        {7, 1}, {-8, 5}, {3, -2}, {1, 4}, {-3, -1},
    };
    int inputs[] = {-9, -4, -1, 0, 1, 2, 5, 6, 8, 12};

    for (int input : inputs) {
        int original = runOriginal(program, input);
        int pruned = runPruned(program, input);
        if (original != pruned) {
            std::cout << "divergence at input=" << input
                      << ": original=" << original
                      << " pruned=" << pruned << "\n";
            return 0;
        }
    }
    std::cout << "all " << (sizeof(inputs) / sizeof(inputs[0]))
              << " inputs agreed\n";
    return 0;
}

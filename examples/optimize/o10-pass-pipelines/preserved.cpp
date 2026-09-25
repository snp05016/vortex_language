// A pass manager in miniature. It caches analysis results, runs passes in
// order, and after each pass discards only the results that pass did not
// promise to keep. The program is a list of numbers, and its two analyses
// answer "is it sorted?" and "what is its sum?". The log uses the words of
// LLVM's -debug-pass-manager, and, as there, a cached answer prints nothing.
//
// Follows: LLVM's New Pass Manager guide (Using Analyses) and PassManager::run
// in LLVM 18's PassManager.h, which invalidates after every pass.
#include <algorithm>
#include <functional>
#include <numeric>
#include <optional>
#include <print>
#include <string>
#include <vector>

using Program = std::vector<int>;

// What a pass says is still valid when it returns: LLVM's PreservedAnalyses.
struct Preserved {
    bool sorted;
    bool sum;
};

// The analysis manager: a result is computed on first request, then kept.
struct Analyses {
    std::optional<bool> sorted_result;
    std::optional<long> sum_result;
    int runs = 0;
    int queries = 0;

    template <typename T, typename Compute>
    T get(std::optional<T>& slot, const char* name, Compute compute) {
        ++queries;
        if (!slot) {
            std::println("Running analysis: {}", name);
            ++runs;
            slot = compute();
        }
        return *slot;
    }
    bool sorted(const Program& p) {
        return get(sorted_result, "sorted", [&] { return std::ranges::is_sorted(p); });
    }
    long sum(const Program& p) {
        return get(sum_result, "sum", [&] { return std::accumulate(p.begin(), p.end(), 0L); });
    }
    void invalidate(Preserved kept) {
        if (!kept.sorted && sorted_result) {
            std::println("Invalidating analysis: sorted");
            sorted_result.reset();
        }
        if (!kept.sum && sum_result) {
            std::println("Invalidating analysis: sum");
            sum_result.reset();
        }
    }
};

struct Pass {
    std::string name;
    std::function<Preserved(Program&, Analyses&)> run;
};

int main() {
    // Reads both analyses and changes nothing, so it keeps everything.
    Pass check{"check", [](Program& p, Analyses& a) {
        bool is_sorted = a.sorted(p);  // two statements, because the order
        long total = a.sum(p);         // of the queries shows in the log
        std::println("  sorted={} sum={}", is_sorted, total);
        return Preserved{.sorted = true, .sum = true};
    }};
    // New values in the same order: "sorted" survives, "sum" does not.
    Pass add_one{"add_one", [](Program& p, Analyses&) {
        for (int& x : p) ++x;
        return Preserved{.sorted = true, .sum = false};
    }};
    // The same values in a new order: "sum" survives, "sorted" does not.
    Pass reverse{"reverse", [](Program& p, Analyses&) {
        std::ranges::reverse(p);
        return Preserved{.sorted = false, .sum = true};
    }};

    Program program{1, 2, 3, 4};
    Analyses analyses;
    for (const Pass& pass : {check, add_one, check, reverse, check}) {
        std::println("Running pass: {}", pass.name);
        analyses.invalidate(pass.run(program, analyses));
    }
    std::println("{} analysis runs for {} queries", analyses.runs, analyses.queries);
}

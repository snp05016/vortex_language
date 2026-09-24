// A minimal stand-in for one thing a pass manager does: cache an analysis
// result and only throw it away when a transform admits it might be wrong.
// LLVM calls the set of results a transform leaves valid its
// PreservedAnalyses; see the Sources section for the real mechanism.
#include <iostream>
#include <optional>
#include <utility>
#include <vector>

struct Program {
    std::vector<int> values;
};

struct Cache {
    std::optional<bool> sorted;
    std::optional<long> sum;
};

bool compute_sorted(const Program& program) {
    std::cout << "  (recomputing sorted)\n";
    for (std::size_t i = 1; i < program.values.size(); ++i) {
        if (program.values[i] < program.values[i - 1]) return false;
    }
    return true;
}

long compute_sum(const Program& program) {
    std::cout << "  (recomputing sum)\n";
    long total = 0;
    for (int value : program.values) total += value;
    return total;
}

// Fills in only the cache entries a previous transform invalidated.
void query(const Program& program, Cache& cache, const char* label) {
    if (!cache.sorted.has_value()) cache.sorted = compute_sorted(program);
    if (!cache.sum.has_value()) cache.sum = compute_sum(program);
    std::cout << label << ": sorted=" << *cache.sorted << " sum=" << *cache.sum << '\n';
}

// Adds one to every value: the order of elements is untouched, so "sorted"
// survives, but every element changed, so "sum" does not.
void add_one(Program& program, Cache& cache) {
    for (int& value : program.values) value += 1;
    cache.sum.reset();
}

// Reverses the values: their sum survives, but their order does not.
void reverse(Program& program, Cache& cache) {
    auto& v = program.values;
    for (std::size_t i = 0, j = v.size(); i < j / 2; ++i) std::swap(v[i], v[j - 1 - i]);
    cache.sorted.reset();
}

int main() {
    Program program{{1, 2, 3, 4}};
    Cache cache;

    query(program, cache, "before");
    std::cout << "running add_one (preserves: sorted)\n";
    add_one(program, cache);
    query(program, cache, "after add_one");
    std::cout << "running reverse (preserves: sum)\n";
    reverse(program, cache);
    query(program, cache, "after reverse");
}

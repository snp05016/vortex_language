// A toy bottom-up inliner over the call graph of a small calculator program.
// Each function has a size (its instructions other than calls) and a list of
// calls. Tarjan's algorithm finishes each strongly connected component of the
// call graph after every component it calls, so walking the components in
// that order shows the inliner each callee before its callers. A call is
// inlined when its callee is not recursive and is no bigger than a threshold:
// the callee's size and its remaining calls are copied into the caller. A
// function that is not exported and has no callers left is deleted.
//
// Follows: LLVM 18 Inliner.cpp and CGSCCPassManager.h (bottom-up, one
// component at a time); SCCIterator.h (Tarjan's order).
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

struct Function {
    std::string name;
    int size;
    std::vector<int> calls;  // callees, as indices into `program`
    bool exported = false;
    bool deleted = false;
};

std::vector<Function> program = {
    {"main", 5, {1, 5, 3}, true}, {"eval", 10, {2, 3}}, {"apply", 8, {1}},
    {"norm", 3, {4, 4}},          {"sq", 2, {}},        {"report", 30, {}},
};
const int n = static_cast<int>(program.size());
constexpr int threshold = 12;
std::vector<int> order(n, -1), low(n), component(n, -1), path;
int counter = 0, components = 0;

void visit(int v) {  // Tarjan: numbers components in the order they finish
    order[v] = low[v] = counter++;
    path.push_back(v);
    for (int w : program[v].calls) {
        if (order[w] < 0) visit(w);
        if (component[w] < 0) low[v] = std::min(low[v], low[w]);  // w on path
    }
    if (low[v] != order[v]) return;
    for (int w = -1; w != v; path.pop_back()) component[w = path.back()] = components;
    ++components;
}

bool recursive(int g) {
    return std::ranges::count(component, component[g]) > 1 ||
           std::ranges::count(program[g].calls, g) > 0;
}

int total() {
    int sum = 0;
    for (const Function& f : program) sum += f.deleted ? 0 : f.size;
    return sum;
}

int main() {
    for (int v = 0; v < n; ++v)
        if (order[v] < 0) visit(v);
    std::printf("size before: %d\n", total());
    for (int c = 0; c < components; ++c) {
        std::printf("component:");
        for (int f = 0; f < n; ++f)
            if (component[f] == c) std::printf(" %s", program[f].name.c_str());
        std::printf("\n");
        for (int f = 0; f < n; ++f) {
            if (component[f] != c) continue;
            Function& caller = program[f];
            std::vector<int> kept;
            for (std::size_t i = 0; i < caller.calls.size(); ++i) {  // may grow
                const int g = caller.calls[i];
                const Function& callee = program[g];
                if (recursive(g) || callee.size > threshold) {
                    std::printf("  keep %s -> %s: %s\n", caller.name.c_str(),
                                callee.name.c_str(), recursive(g) ? "recursive" : "too big");
                    kept.push_back(g);
                    continue;
                }
                caller.size += callee.size;
                for (int c : callee.calls) caller.calls.push_back(c);
                std::printf("  inline %s into %s: %s grows to %d\n", callee.name.c_str(),
                            caller.name.c_str(), caller.name.c_str(), caller.size);
            }
            caller.calls = kept;
        }
        for (int g = 0; g < n; ++g) {
            bool called = false;
            for (const Function& f : program)
                called = called || (!f.deleted && std::ranges::count(f.calls, g) > 0);
            if (!called && !program[g].exported && !program[g].deleted) {
                program[g].deleted = true;
                std::printf("  delete %s: no callers left\n", program[g].name.c_str());
            }
        }
    }
    std::printf("size after: %d\n", total());
}

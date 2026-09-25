// Live intervals for five lines of straight-line code, numbered two ways.
// With one number per instruction, a value read for the last time by an
// instruction and the value that instruction writes both claim the same
// number, so their intervals overlap. With two numbers per instruction
// (read operands at 2i, write the result at 2i + 1), they do not. The
// program prints both sets of intervals and the most intervals that cover
// any one number: the register count the interval view says the code needs.
//
// Follows: Poletto and Sarkar, "Linear Scan Register Allocation", ACM
// TOPLAS 21(5), 1999, section 3 (live intervals over a numbering); and
// Wimmer and Mossenbock, "Optimized Interval Splitting in a Linear Scan
// Register Allocator", VEE 2005, section 2.5 (numbering instructions by two).

#include <algorithm>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

struct Instruction {
    std::string def;                 // empty for "return"
    std::vector<std::string> uses;
};

struct Range { int start = -1; int end = -1; };

// read_at(i) and write_at(i) turn instruction i (counting from 1) into
// the numbers where it reads its operands and writes its result.
template <typename Read, typename Write>
void report(const char* title, const std::vector<Instruction>& code,
            Read read_at, Write write_at) {
    std::map<std::string, Range> interval;
    for (int i = 1; i <= static_cast<int>(code.size()); ++i) {
        for (const std::string& u : code[i - 1].uses) interval[u].end = read_at(i);
        if (!code[i - 1].def.empty()) interval[code[i - 1].def].start = write_at(i);
    }
    std::printf("%s\n", title);
    int last = 0;
    for (const auto& [name, r] : interval) {
        std::printf("  %s [%2d,%2d]\n", name.c_str(), r.start, r.end);
        last = std::max(last, r.end);
    }
    int most = 0, where = 0;
    for (int point = 1; point <= last; ++point) {
        int covering = 0;
        for (const auto& [name, r] : interval)
            if (r.start <= point && point <= r.end) ++covering;
        if (covering > most) { most = covering; where = point; }
    }
    std::printf("  most intervals covering one number: %d (at %d)\n", most, where);
}

int main() {
    std::vector<Instruction> code = {
        {"a", {}},            // 1  a = 2
        {"b", {}},            // 2  b = 3
        {"c", {"a", "b"}},    // 3  c = a + b
        {"d", {"a", "b"}},    // 4  d = a * b
        {"e", {"c", "d"}},    // 5  e = c + d
        {"", {"e"}},          // 6  return e
    };
    report("one number per instruction:", code,
           [](int i) { return i; }, [](int i) { return i; });
    report("two numbers per instruction:", code,
           [](int i) { return 2 * i; }, [](int i) { return 2 * i + 1; });
}

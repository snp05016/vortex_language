// program: valid
// Follows: Ulrich Drepper, "What Every Programmer Should Know About Memory",
// sections 3.2 (tag, set and offset) and 3.3.1 (set associativity).
#include <array>
#include <cstdio>

// A toy cache, not a model of any real one: 16 sets of 2 ways, 64-byte lines.
// One way spans 16 * 64 = 1024 bytes, so addresses 1024 bytes apart share a set.
constexpr unsigned kLine = 64, kSets = 16, kWays = 2;

struct Split { unsigned tag, set, offset; };

Split split(unsigned address) {
    unsigned line = address / kLine;  // which line of memory
    return {line / kSets, line % kSets, address % kLine};
}

class ToyCache {
public:
    // Returns true on a hit. Each set keeps its ways in least-recently-used
    // order, most recent first; a miss evicts the last way.
    bool access(unsigned address) {
        Split s = split(address);
        auto& ways = tags_[s.set];
        unsigned found = kWays;
        for (unsigned w = 0; w < kWays; ++w)
            if (ways[w] == s.tag + 1) found = w;  // +1 so that 0 means empty
        unsigned last = (found == kWays) ? kWays - 1 : found;
        for (unsigned w = last; w > 0; --w) ways[w] = ways[w - 1];
        ways[0] = s.tag + 1;
        return found != kWays;
    }

private:
    std::array<std::array<unsigned, kWays>, kSets> tags_{};
};

// Reads element [row, 0] of an 8-row f32 matrix, twice down the column.
void column_walk(unsigned row_bytes) {
    ToyCache cache;
    std::printf("row length %u bytes, sets of rows 0..7:", row_bytes);
    for (unsigned row = 0; row < 8; ++row) std::printf(" %u", split(row * row_bytes).set);
    int misses[2] = {0, 0};
    for (int pass = 0; pass < 2; ++pass)
        for (unsigned row = 0; row < 8; ++row)
            if (!cache.access(row * row_bytes)) ++misses[pass];
    std::printf("\n  misses: first pass %d, second pass %d\n", misses[0], misses[1]);
}

int main() {
    Split s = split(0x1234);
    std::printf("address 0x1234: tag %u, set %u, offset %u\n", s.tag, s.set, s.offset);
    // Eight lines are a quarter of the cache's 32. Any misses on the second
    // pass are conflict misses: the lines would fit, but not in one set.
    column_walk(1024);  // 256 f32 per row: a multiple of the way size
    column_walk(1088);  // 272 f32 per row: one line longer
    return 0;
}

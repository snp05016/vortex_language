// program: valid
// Follows: Ulrich Drepper, "What Every Programmer Should Know About Memory,"
// section 3.3 (associativity and conflict misses).
#include <cstdio>
#include <array>

// A toy cache: `Sets` sets of `Ways` tags each, no data, no timing. Its sizes
// are chosen to make the example readable, not to model any real cache.
template <int Sets, int Ways>
class ToyCache {
public:
    // Returns true when `tag` is already resident in `set_index` (a hit).
    // A miss inserts `tag`, evicting the oldest tag in that set if it is full.
    bool access(int set_index, int tag) {
        auto& set = ways_[set_index];
        for (int way = 0; way < Ways; ++way) {
            if (set[way] == tag) return true;
        }
        for (int way = Ways - 1; way > 0; --way) set[way] = set[way - 1];
        set[0] = tag;
        return false;
    }

private:
    std::array<std::array<int, Ways>, Sets> ways_{};  // 0 means empty
};

int main() {
    // Two addresses, tags 100 and 200, that map to the same set (index 0).
    // Accessing them alternately thrashes a direct-mapped cache: each access
    // evicts the other tag, so nothing after the first two is ever a hit. A
    // 2-way set-associative cache of the same set count holds both at once.
    ToyCache<4, 1> direct_mapped;
    ToyCache<4, 2> two_way;

    int direct_hits = 0, two_way_hits = 0;
    const int repeats = 6;
    for (int i = 0; i < repeats; ++i) {
        if (direct_mapped.access(0, 100)) ++direct_hits;
        if (direct_mapped.access(0, 200)) ++direct_hits;
        if (two_way.access(0, 100)) ++two_way_hits;
        if (two_way.access(0, 200)) ++two_way_hits;
    }
    std::printf("direct-mapped hits out of %d: %d\n", repeats * 2, direct_hits);
    std::printf("2-way associative hits out of %d: %d\n", repeats * 2, two_way_hits);
    return 0;
}

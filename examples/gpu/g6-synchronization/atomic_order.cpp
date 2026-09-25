#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <map>

// Four threads each fold one partial sum into a total that starts at 0, with
// an atomic add. Each add is indivisible, but the order in which the four
// arrive is up to the hardware. This program applies all 24 arrival orders on
// one thread and counts the distinct totals, first for float, then for int.
int main() {
    const float partial[4] = {16777216.0f, 1.0f, 1.0f, -16777216.0f};  // 2^24
    const std::int32_t whole[4] = {16777216, 1, 1, -16777216};

    int order[4] = {0, 1, 2, 3};
    std::map<std::uint32_t, int> float_totals;  // bit pattern -> how many orders
    std::map<std::int32_t, int> int_totals;
    do {
        float f = 0.0f;
        std::int32_t n = 0;
        for (int k = 0; k < 4; ++k) {
            f = f + partial[order[k]];  // one correctly rounded add per arrival
            n = n + whole[order[k]];    // exact: no rounding, no overflow here
        }
        ++float_totals[std::bit_cast<std::uint32_t>(f)];
        ++int_totals[n];
    } while (std::next_permutation(order, order + 4));

    std::printf("float: %zu distinct totals\n", float_totals.size());
    for (auto [bits, count] : float_totals)
        std::printf("  0x%08x = %g in %d orders\n", bits, std::bit_cast<float>(bits), count);
    std::printf("int32: %zu distinct total\n", int_totals.size());
    for (auto [value, count] : int_totals)
        std::printf("  %d in %d orders\n", value, count);
}

#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <set>

// Four partial sums, as if four threads each reduced part of a large array
// and are about to fold their results into one total with an atomic add.
// The C++ standard library has no portable simulated atomic order to run;
// instead this enumerates, on one thread, every order in which four fixed
// values could be added, the way a real atomic's hardware arbiter might
// choose one such order at run time.
int main() {
    float values[4] = {16777216.0f, 1.0f, 1.0f, -16777216.0f};
    int order[4] = {0, 1, 2, 3};
    std::set<std::uint32_t> distinct;
    do {
        float acc = 0.0f;
        for (int i = 0; i < 4; ++i) acc = acc + values[order[i]];
        distinct.insert(std::bit_cast<std::uint32_t>(acc));
    } while (std::next_permutation(order, order + 4));
    std::printf("orders: 24, distinct results: %zu\n", distinct.size());
    for (std::uint32_t bits : distinct) {
        std::printf("0x%08x = %g\n", bits, std::bit_cast<float>(bits));
    }
}

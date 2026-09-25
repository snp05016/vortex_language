#include <cstddef>
#include <cstdio>
#include <new>

namespace {

// One counter per thread, laid out back to back: nothing stops two of them
// from landing on the same cache line.
struct Packed {
    long count = 0;
};

// The same counter, padded out to a full line so that no other object's
// writes can ever fall on it.
struct alignas(std::hardware_destructive_interference_size) Padded {
    long count = 0;
};

// The distance between two objects, in bytes. Two adjacent array elements
// are always this far apart, on every run: it depends only on the type's
// layout, never on where the array happens to sit in memory.
std::ptrdiff_t byte_gap(const void* a, const void* b) {
    return static_cast<const std::byte*>(b) - static_cast<const std::byte*>(a);
}

}  // namespace

int main() {
    Packed packed[2]{};
    Padded padded[2]{};
    const auto line =
        static_cast<std::ptrdiff_t>(std::hardware_destructive_interference_size);

    const std::ptrdiff_t packed_gap = byte_gap(&packed[0], &packed[1]);
    const std::ptrdiff_t padded_gap = byte_gap(&padded[0], &padded[1]);

    std::printf("packed[0] and packed[1] can share a cache line: %s\n",
                packed_gap < line ? "yes" : "no");
    std::printf("padded[0] and padded[1] can share a cache line: %s\n",
                padded_gap < line ? "yes" : "no");
    return 0;
}

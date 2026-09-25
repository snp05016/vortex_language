// How many cache lines and pages the micro-kernel's operands touch, before
// and after packing. Only addresses are computed here, never loaded: each
// operand is a row-major f32 matrix starting at address 0, and the line and
// page sizes are parameters (128 and 16384 bytes are what sysctl reports for
// hw.cachelinesize and hw.pagesize on an Apple M4 Pro).
//
// Follows: Goto & van de Geijn, TOMS 34(3) 2008, sections 4.2.2 and 4.2.3
// (a submatrix needs many TLB entries; packing it into a contiguous buffer
// fixes that).
#include <cstdio>
#include <set>

constexpr long line = 128, page = 16384, elem = 4;

struct Footprint {
    std::set<long> lines, pages;
    void touch(long address) {
        lines.insert(address / line);
        pages.insert(address / page);
    }
};

// rows x cols elements read from a row-major matrix whose rows are `width`
// elements apart, then the same elements read from a packed, contiguous copy.
void report(const char* name, int rows, int cols, int width) {
    Footprint strided, packed;
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c) {
            strided.touch((long(r) * width + c) * elem);
            packed.touch((long(r) * cols + c) * elem);
        }
    long bytes = long(rows) * cols * elem;
    std::printf("%s: %ld bytes of data\n", name, bytes);
    std::printf("  in place: lines=%zu pages=%zu useful bytes per line=%ld\n",
                strided.lines.size(), strided.pages.size(),
                bytes / long(strided.lines.size()));
    std::printf("  packed:   lines=%zu pages=%zu useful bytes per line=%ld\n",
                packed.lines.size(), packed.pages.size(),
                bytes / long(packed.lines.size()));
}

int main() {
    // One kc x nr sliver of b, as one micro-kernel call reads it.
    report("b sliver, kc=256 x nr=4, rows 1024 wide", 256, 4, 1024);
    // One mc x kc block of a, as the ic loop reuses it.
    report("a block, mc=64 x kc=256, rows 1024 wide", 64, 256, 1024);
    return 0;
}

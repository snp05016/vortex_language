// A closed-form operational-intensity model for an N x N x N matrix
// product, ignoring everything a real cache does except one number: how
// many times each loaded byte is reused before it is evicted. This is
// the algebra behind "tiling moves a rung up and to the right" on the
// roofline model: it says nothing about any particular chip's peak
// bandwidth or peak flops, only how the algorithm's own traffic changes.
#include <iomanip>
#include <iostream>

// 2*N^3 multiply-adds; ignoring the write of C, which is O(N^2) and
// negligible next to the O(N^3) reads for any N worth tiling.
double flops(double n) { return 2.0 * n * n * n; }

// Naive model: every one of the 2*N^3 operand reads is a fresh four-byte
// load, because nothing is still resident when it is needed again.
double naive_bytes(double n) { return 8.0 * n * n * n; }

// Blocked model: a block_side x block_side tile of operands stays
// resident for block_side reuses before it is reloaded, so the naive
// traffic is divided by block_side. block_side is a model parameter here,
// not a measured cache size; P8 fits it to a real cache.
double blocked_bytes(double n, double block_side) { return naive_bytes(n) / block_side; }

int main() {
    const double block_side = 16.0;
    std::cout << std::fixed << std::setprecision(4);
    std::cout << "block_side = " << block_side << " (a model parameter, not a cache size)\n";
    std::cout << "N       naive flops/byte   blocked flops/byte\n";
    for (double n : {64.0, 256.0, 1024.0}) {
        const double f = flops(n);
        const double naive_intensity = f / naive_bytes(n);
        const double blocked_intensity = f / blocked_bytes(n, block_side);
        std::cout << std::setw(5) << static_cast<long>(n) << std::setw(20) << naive_intensity
                  << std::setw(22) << blocked_intensity << "\n";
    }
    return 0;
}

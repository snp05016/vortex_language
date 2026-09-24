// A reusable "bits gate": before any rung of the ladder is timed, its
// output must be compared, byte for byte, against the naive kernel. This
// checks two rungs from the ladder's own table: interchanging i and j
// (bits stay identical, because it never touches how one C element's sum
// is built) and reducing across k with a tree instead of left to right
// (bits differ, because addition is not associative).
#include <array>
#include <iomanip>
#include <iostream>
#include <span>

bool same_bits(std::span<const float> lhs, std::span<const float> rhs) {
    if (lhs.size() != rhs.size()) return false;
    for (std::size_t i = 0; i < lhs.size(); ++i) {
        if (lhs[i] != rhs[i]) return false;
    }
    return true;
}

// A 2x2 matrix product, once with rows outer and once with columns outer.
// Only the order across C elements changes; each element's own sum still
// runs k = 0, 1 in order.
std::array<float, 4> matmul_ij(const std::array<float, 4>& a,
                                const std::array<float, 4>& b, bool rows_outer) {
    std::array<float, 4> c{};
    auto at = [](const std::array<float, 4>& m, int r, int col) { return m[r * 2 + col]; };
    auto run = [&](int i, int j) {
        float sum = 0.0f;
        for (int k = 0; k < 2; ++k) sum += at(a, i, k) * at(b, k, j);
        c[i * 2 + j] = sum;
    };
    if (rows_outer) {
        for (int i = 0; i < 2; ++i)
            for (int j = 0; j < 2; ++j) run(i, j);
    } else {
        for (int j = 0; j < 2; ++j)
            for (int i = 0; i < 2; ++i) run(i, j);
    }
    return c;
}

// One C element's dot product, summed left to right (what every rung up
// to the vectorizer does) and summed as a pairwise tree (what a
// dot-product-shaped, k-vectorized reduction would do instead).
float dot_sequential(const std::array<float, 4>& a, const std::array<float, 4>& b) {
    float sum = 0.0f;
    for (int k = 0; k < 4; ++k) sum += a[k] * b[k];
    return sum;
}

float dot_tree(const std::array<float, 4>& a, const std::array<float, 4>& b) {
    float left = a[0] * b[0] + a[1] * b[1];
    float right = a[2] * b[2] + a[3] * b[3];
    return left + right;
}

int main() {
    const std::array<float, 4> a{1.0f, 2.0f, 3.0f, 4.0f};
    const std::array<float, 4> b{5.0f, 6.0f, 7.0f, 8.0f};
    auto c_rows_outer = matmul_ij(a, b, true);
    auto c_cols_outer = matmul_ij(a, b, false);
    bool interchange_ok = same_bits(c_rows_outer, c_cols_outer);
    std::cout << "gate: interchange (i,j swapped): "
              << (interchange_ok ? "PASS" : "FAIL") << " (bits "
              << (interchange_ok ? "identical" : "differ") << ")\n";

    // A classic case where left-to-right and tree summation disagree: a
    // large term all but swallows a small one, and which small term
    // survives depends on grouping.
    const std::array<float, 4> d{1.0e8f, 1.0f, -1.0e8f, 1.0f};
    const std::array<float, 4> ones{1.0f, 1.0f, 1.0f, 1.0f};
    float seq = dot_sequential(d, ones);
    float tree = dot_tree(d, ones);
    bool reduction_ok = (seq == tree);
    std::cout << "gate: k-reduction (tree vs sequential sum): "
              << (reduction_ok ? "PASS" : "FAIL") << " (bits "
              << (reduction_ok ? "identical" : "differ") << ")\n";
    std::cout << std::fixed << std::setprecision(1);
    std::cout << "  sequential = " << seq << "\n";
    std::cout << "  tree       = " << tree << "\n";
    return 0;
}

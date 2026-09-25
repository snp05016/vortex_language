// A two-stage blur: blurx sums three neighbours along x, out sums three rows
// of blurx. Four schedules decide when blurx is computed and where it is
// kept. All four must produce the same out; they differ in how many times
// blurx is evaluated and how much storage it needs.
#include <algorithm>
#include <cstdio>
#include <vector>

constexpr int W = 32, H = 32;  // out covers x in [0, W), y in [0, H)
constexpr int TILE = 8;

// --- The algorithm ---
int in(int x, int y) { return ((x * 7 + y * 13) % 256 + 256) % 256; }
long evaluations = 0;  // counts calls of the producer, for the report only
int blurx(int x, int y) { ++evaluations; return in(x - 1, y) + in(x, y) + in(x + 1, y); }

using Image = std::vector<int>;  // out, row-major W x H

// Root: compute every needed row of blurx (y from -1 to H) before any of out.
Image root(long& storage) {
    std::vector<int> bx((H + 2) * W);
    storage = long(bx.size());
    for (int y = -1; y <= H; ++y)
        for (int x = 0; x < W; ++x) bx[(y + 1) * W + x] = blurx(x, y);
    Image out(W * H);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            out[y * W + x] = bx[y * W + x] + bx[(y + 1) * W + x] + bx[(y + 2) * W + x];
    return out;
}

// Inline: every point of out computes the three blurx values it needs.
Image inline_all(long& storage) {
    storage = 0;
    Image out(W * H);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) out[y * W + x] = blurx(x, y - 1) + blurx(x, y) + blurx(x, y + 1);
    return out;
}

// Sliding window: keep the last three rows of blurx; each new row of out
// computes only the one row of blurx it has not seen yet. Rows run in order.
Image sliding(long& storage) {
    std::vector<int> ring(3 * W);
    storage = long(ring.size());
    auto slot = [](int y) { return ((y % 3) + 3) % 3; };
    Image out(W * H);
    for (int y = -1; y <= H; ++y) {
        for (int x = 0; x < W; ++x) ring[slot(y) * W + x] = blurx(x, y);
        if (y < 1) continue;  // out row y - 1 needs blurx rows y - 2, y - 1, y
        for (int x = 0; x < W; ++x)
            out[(y - 1) * W + x] = ring[slot(y - 2) * W + x] + ring[slot(y - 1) * W + x] + ring[slot(y) * W + x];
    }
    return out;
}

// Tiles: for each TILE x TILE block of out, compute the blurx rows it needs,
// one row above and one below included. Neighbouring tiles recompute those rows.
Image tiles(long& storage) {
    std::vector<int> bx((TILE + 2) * TILE);
    storage = long(bx.size());
    Image out(W * H);
    for (int ty = 0; ty < H; ty += TILE)
        for (int tx = 0; tx < W; tx += TILE) {
            for (int y = -1; y <= TILE; ++y)
                for (int x = 0; x < TILE; ++x) bx[(y + 1) * TILE + x] = blurx(tx + x, ty + y);
            for (int y = 0; y < TILE; ++y)
                for (int x = 0; x < TILE; ++x)
                    out[(ty + y) * W + tx + x] =
                        bx[y * TILE + x] + bx[(y + 1) * TILE + x] + bx[(y + 2) * TILE + x];
        }
    return out;
}

int main() {
    struct Schedule { const char* name; Image (*run)(long&); };
    const Schedule schedules[] = {{"root", root}, {"inline", inline_all}, {"sliding", sliding}, {"tiles", tiles}};
    Image reference;
    std::printf("%-8s %12s %16s %10s\n", "schedule", "blurx evals", "blurx storage", "same out");
    for (const Schedule& s : schedules) {
        evaluations = 0;
        long storage = 0;
        Image out = s.run(storage);
        if (reference.empty()) reference = out;
        std::printf("%-8s %12ld %16ld %10s\n", s.name, evaluations, storage,
                    std::equal(out.begin(), out.end(), reference.begin()) ? "yes" : "no");
    }
    std::printf("out has %d points\n", W * H);
}

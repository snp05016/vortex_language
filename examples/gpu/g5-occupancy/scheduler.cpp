// Follows: Nsight Compute Profiling Guide, "Scheduler Statistics" (active,
// eligible and issued warps), and Vasily Volkov, "Better Performance at
// Lower Occupancy", GTC 2010, slides 10 to 14 (Little's law, TLP and ILP).
//
// A toy warp scheduler: one issue slot per cycle and a result latency of
// 8 cycles, invented for the illustration and not any real GPU's numbers.
// Each warp runs 64 multiply-adds spread round-robin over `ilp` independent
// accumulators, so an instruction must wait for the one `ilp` places before
// it. Each cycle the scheduler issues from the first eligible warp after the
// one it issued from last; a cycle with no eligible warp is an empty slot.
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

constexpr int kLatency = 8;
constexpr int kOpsPerWarp = 64;

struct Run {
    int cycles;
    std::string timeline;  // first 16 cycles: warp letter, or '.' if empty
};

Run simulate(int warps, int ilp) {
    std::vector<int> done(warps, 0);                    // ops issued so far
    std::vector<std::vector<int>> ready(warps, std::vector<int>(ilp, 0));
    Run run{0, ""};
    int last = warps - 1, remaining = warps * kOpsPerWarp;
    for (int cycle = 0; remaining > 0; ++cycle) {
        char slot = '.';
        for (int step = 1; step <= warps; ++step) {
            int w = (last + step) % warps;
            if (done[w] == kOpsPerWarp) continue;
            int& acc = ready[w][done[w] % ilp];  // accumulator this op uses
            if (acc > cycle) continue;           // stalled: result not back
            acc = cycle + kLatency;
            ++done[w];
            --remaining;
            last = w;
            slot = static_cast<char>('A' + w);
            break;
        }
        if (cycle < 16) run.timeline += slot;
        run.cycles = cycle + 1;
    }
    return run;
}

int main() {
    const int configs[][2] = {{1, 1}, {2, 1}, {4, 1}, {8, 1}, {16, 1},
                              {1, 2}, {1, 4}, {1, 8}, {2, 4}, {4, 2}};
    std::printf("warps ilp  cycles  busy  predicted  first 16 cycles\n");
    for (const auto& c : configs) {
        Run r = simulate(c[0], c[1]);
        int ops = c[0] * kOpsPerWarp;
        int busy = ops * 100 / r.cycles;
        int predicted = std::min(100, c[0] * c[1] * 100 / kLatency);
        std::printf("%5d %3d %7d %4d%% %9d%%  %s\n", c[0], c[1], r.cycles,
                    busy, predicted, r.timeline.c_str());
    }
}

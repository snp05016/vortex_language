// The price of a fast schedule: registers. The block squares eight loaded
// values and stores each square, like the body of a loop unrolled eight
// times. The machine is the made-up one from list_schedule.cpp: one
// instruction per cycle, in order; a load or a multiply is ready 3 cycles
// after it starts, a store 1. For each schedule the program prints the
// cycles taken and the most values live at once, which is how many
// registers the block needs without spilling.
//
// The capped schedules are list scheduling that refuses to start an
// instruction that would push the live count past a limit while another
// instruction, ready now or soon, would not. That is the idea of switching
// between a latency-first choice and a register-saving one, as in Goodman
// and Hsu's integrated prepass scheduling.
//
// Follows: Goodman and Hsu, "Code scheduling and register allocation in large
// basic blocks", ICS 1988 (the abstract).

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

struct Instr {
    std::string text;
    int latency;
    std::vector<int> reads;
};

std::vector<Instr> block;
std::vector<int> readers;  // how many instructions read each result

void build(int n) {
    for (int k = 0; k < n; ++k) {
        std::string i = std::to_string(k);
        int load = int(block.size());
        block.push_back({"x" + i + " = load v[" + i + "]", 3, {}});
        block.push_back({"q" + i + " = x" + i + " * x" + i, 3, {load}});
        block.push_back({"store w[" + i + "], q" + i, 1, {load + 1}});
    }
    readers.assign(block.size(), 0);
    for (const Instr& in : block)
        for (int p : in.reads) ++readers[p];
}

std::vector<int> heights() {
    std::vector<int> h(block.size(), 0);
    for (int i = int(block.size()) - 1; i >= 0; --i) {
        h[i] += block[i].latency;
        for (int p : block[i].reads) h[p] = std::max(h[p], h[i]);
    }
    return h;
}

// Change in live values if i starts now: its result becomes live if anyone
// reads it; an input dies if i is its last remaining reader.
int delta(int i, const std::vector<int>& left) {
    int d = readers[i] > 0 ? 1 : 0;
    for (int p : block[i].reads)
        if (left[p] == 1) --d;
    return d;
}

struct Result { int cycles, max_live; };

// limit < 0: plain list scheduling. limit == 0: keep the source order.
Result schedule(int limit) {
    std::vector<int> h = heights(), start(block.size(), -1);
    std::vector<int> left = readers;
    int live = 0, max_live = 0, placed = 0, done = 0;
    for (int cycle = 0; placed < int(block.size()); ++cycle) {
        int best = -1;
        bool fit_soon = false;  // something that fits is waiting on latency
        for (int i = 0; i < int(block.size()); ++i) {
            if (start[i] >= 0) continue;
            if (limit == 0 && i != placed) continue;  // source order only
            bool started = true, ready = true;
            for (int p : block[i].reads) {
                if (start[p] < 0) started = false;
                else if (start[p] + block[p].latency > cycle) ready = false;
            }
            bool fits = limit <= 0 || live + delta(i, left) <= limit;
            if (started && !ready && fits) fit_soon = true;
            if (!started || !ready) continue;
            bool best_fits = best >= 0 &&
                             (limit <= 0 || live + delta(best, left) <= limit);
            if (best < 0 || (fits && !best_fits) ||
                (fits == best_fits && h[i] > h[best]))
                best = i;
        }
        // Over the limit with a better choice on its way: wait for it.
        if (best >= 0 && limit > 0 && live + delta(best, left) > limit &&
            fit_soon)
            best = -1;
        if (best < 0) continue;  // a stall
        live += delta(best, left);
        for (int p : block[best].reads) --left[p];
        max_live = std::max(max_live, live);
        start[best] = cycle;
        done = std::max(done, cycle + block[best].latency);
        ++placed;
    }
    return {done, max_live};
}

int main() {
    build(8);
    std::printf("%zu instructions\n", block.size());
    std::printf("%-30s %7s %9s\n", "schedule", "cycles", "max live");
    Result in_order = schedule(0), fast = schedule(-1);
    std::printf("%-30s %7d %9d\n", "source order", in_order.cycles,
                in_order.max_live);
    std::printf("%-30s %7d %9d\n", "list schedule, latency first",
                fast.cycles, fast.max_live);
    for (int cap : {4, 2}) {
        Result r = schedule(cap);
        std::printf("list schedule, at most %d live  %7d %9d\n", cap,
                    r.cycles, r.max_live);
    }
}

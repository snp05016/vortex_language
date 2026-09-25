// List scheduling on one basic block: the squared length of a 3-vector,
// x*x + y*y + z*z, summed left to right and stored.
//
// The machine is a made-up teaching machine, not a real core: it starts at
// most kIssueWidth instructions per cycle, in the order it is given, and an
// instruction cannot start until every value it reads is ready. A load, a
// multiply and an add each take 3 cycles to produce their result; a store
// takes 1. Changing the order changes only when things happen: every
// instruction reads the same inputs in both orders, so the result is the
// same bits.
//
// Follows: Gibbons and Muchnick, "Efficient instruction scheduling for a
// pipelined architecture", SIGPLAN '86 (a DAG per basic block, scheduled
// heuristically after code generation).

#include <algorithm>
#include <cstdio>
#include <vector>

constexpr int kIssueWidth = 1;  // try 2, then compare with the page

struct Instr {
    const char* text;
    int latency;            // cycles until the result can be read
    std::vector<int> reads; // instructions whose results this one reads
};

// Source order: each square is used right after it is computed.
const std::vector<Instr> block = {
    {"x  = load v[0]", 3, {}},
    {"xx = x * x", 3, {0}},
    {"y  = load v[1]", 3, {}},
    {"yy = y * y", 3, {2}},
    {"s  = xx + yy", 3, {1, 3}},
    {"z  = load v[2]", 3, {}},
    {"zz = z * z", 3, {5}},
    {"t  = s + zz", 3, {4, 6}},
    {"store out, t", 1, {7}},
};

// Priority: the longest path, in cycles, from the start of an instruction
// to the end of the block. Reads always point backwards, so one reverse
// sweep sees every successor before its predecessors.
std::vector<int> heights() {
    std::vector<int> h(block.size(), 0);
    for (int i = int(block.size()) - 1; i >= 0; --i) {
        h[i] += block[i].latency;
        for (int p : block[i].reads) h[p] = std::max(h[p], h[i]);
    }
    return h;
}

bool ready_at(int i, int cycle, const std::vector<int>& start) {
    for (int p : block[i].reads)
        if (start[p] < 0 || start[p] + block[p].latency > cycle) return false;
    return true;
}

// In-order issue of a fixed order: the next instruction waits for its inputs,
// and nothing behind it may overtake it.
std::vector<int> issue_in_order(const std::vector<int>& order) {
    std::vector<int> start(block.size(), -1);
    int cycle = 0, used = 0;
    for (int i : order) {
        while (!ready_at(i, cycle, start) || used == kIssueWidth) {
            ++cycle;
            used = 0;
        }
        start[i] = cycle;
        ++used;
    }
    return start;
}

// List scheduling: each cycle, among the instructions whose inputs are
// ready, start the ones with the longest path to the end first.
std::vector<int> list_schedule(const std::vector<int>& h) {
    std::vector<int> start(block.size(), -1), order;
    for (int cycle = 0; order.size() < block.size(); ++cycle) {
        for (int slot = 0; slot < kIssueWidth; ++slot) {
            int best = -1;
            for (int i = 0; i < int(block.size()); ++i)
                if (start[i] < 0 && ready_at(i, cycle, start) &&
                    (best < 0 || h[i] > h[best]))  // ties: source order
                    best = i;
            if (best < 0) break;
            start[best] = cycle;
            order.push_back(best);
        }
    }
    return start;
}

// An independent check: every value is ready before anything reads it.
bool respects_dependences(const std::vector<int>& start) {
    for (int i = 0; i < int(block.size()); ++i)
        for (int p : block[i].reads)
            if (start[i] < start[p] + block[p].latency) return false;
    return true;
}

void print(const char* title, const std::vector<int>& start) {
    std::vector<int> by_cycle(block.size());
    for (int i = 0; i < int(block.size()); ++i) by_cycle[i] = i;
    std::stable_sort(by_cycle.begin(), by_cycle.end(),
                     [&](int a, int b) { return start[a] < start[b]; });
    int done = 0, busy = 0, last = -1;
    std::printf("%s\n", title);
    for (int i : by_cycle) {
        std::printf("  cycle %2d  %s\n", start[i], block[i].text);
        done = std::max(done, start[i] + block[i].latency);
        if (start[i] != last) ++busy;
        last = start[i];
    }
    std::printf("  done after %d cycles; %d of them start nothing\n", done,
                done - busy);
}

int main() {
    std::vector<int> h = heights();
    std::printf("priority = longest path to the end, in cycles\n");
    for (int i = 0; i < int(block.size()); ++i)
        std::printf("  %-14s %2d\n", block[i].text, h[i]);

    std::vector<int> source(block.size());
    for (int i = 0; i < int(block.size()); ++i) source[i] = i;
    std::vector<int> naive = issue_in_order(source);
    std::vector<int> listed = list_schedule(h);

    std::printf("issue width %d\n", kIssueWidth);
    print("source order:", naive);
    print("list schedule:", listed);
    std::printf("every dependence respected: %s, %s\n",
                respects_dependences(naive) ? "yes" : "no",
                respects_dependences(listed) ? "yes" : "no");
}

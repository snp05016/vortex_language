// Follows: the perf wiki tutorial, "multiplexing and scaling events"
// (final_count = raw_count * time_enabled / time_running), and the
// perf_event_open(2) manual page, "time_enabled, time_running".
//
// A model, not a measurement: a run is 8 equal time slices, and the core has
// 2 counters. When more events are asked for than there are counters, each
// slice programs the first 2 events of a list and then rotates the list by
// one, a simple form of the round-robin the perf wiki describes.
#include <cstdio>
#include <string>
#include <vector>

constexpr int slices = 8;
constexpr int counters = 2;

// The program alternates two phases of two slices each: a memory-bound
// phase (few instructions per cycle, many misses) and a compute phase.
struct Slice {
    long cycles, instructions, l1_misses, branch_misses;
};
Slice at(int s) {
    bool memory_phase = (s / 2) % 2 == 0;
    return memory_phase ? Slice{1000, 500, 40, 2} : Slice{1000, 2500, 1, 2};
}
long value(const Slice& x, const std::string& event) {
    if (event == "cycles") return x.cycles;
    if (event == "instructions") return x.instructions;
    if (event == "l1-misses") return x.l1_misses;
    return x.branch_misses;
}

struct Estimate {
    long truth = 0, raw = 0;
    int running = 0;  // slices in which the event had a counter
    double scaled() const { return double(raw) * slices / running; }
};

std::vector<Estimate> measure(std::vector<std::string> events) {
    std::vector<std::string> order = events;
    std::vector<Estimate> out(events.size());
    for (int s = 0; s < slices; ++s) {
        for (std::size_t e = 0; e < events.size(); ++e) {
            out[e].truth += value(at(s), events[e]);
            for (int k = 0; k < counters && k < int(order.size()); ++k)
                if (order[k] == events[e]) {
                    out[e].raw += value(at(s), events[e]);
                    ++out[e].running;
                }
        }
        if (int(order.size()) > counters) {  // multiplexing: rotate
            order.push_back(order.front());
            order.erase(order.begin());
        }
    }
    return out;
}

void report(const std::vector<std::string>& events) {
    std::vector<Estimate> r = measure(events);
    std::printf("%zu events on %d counters\n", events.size(), counters);
    for (std::size_t e = 0; e < events.size(); ++e)
        std::printf("  %-13s true %6ld  counted in %d/%d slices  scaled %8.0f\n",
                    events[e].c_str(), r[e].truth, r[e].running, slices, r[e].scaled());
    std::printf("  IPC: true %.2f, from the scaled counts %.2f\n\n",
                double(r[1].truth) / r[0].truth, r[1].scaled() / r[0].scaled());
}

int main() {
    report({"cycles", "instructions", "l1-misses", "branch-misses"});
    report({"cycles", "instructions"});  // no more events than counters
}

// Follows: the perf wiki tutorial, "Event-based sampling overview" and
// "Period and rate"; Brendan Gregg, "Linux perf Examples" (why 99 Hertz
// rather than 100).
//
// A model, not a profiler: a program is a fixed list of phases, each lasting
// a known number of cycles, and a "sample" is taken every `period` cycles by
// looking up which phase is running. Counting reads the totals directly.
#include <cstdio>
#include <string>
#include <vector>

struct Phase {
    std::string name;
    long cycles;
};

// One trip of the program's main loop: a short bookkeeping step, then the
// real work. The loop runs `trips` times.
const std::vector<Phase> trip = {{"tick", 10}, {"work", 90}};
constexpr long trips = 10'000;

long trip_cycles() {
    long total = 0;
    for (const Phase& p : trip) total += p.cycles;
    return total;
}

// Which phase is running at a given cycle of the run.
const std::string& running_at(long cycle) {
    long t = cycle % trip_cycles();
    for (const Phase& p : trip) {
        if (t < p.cycles) return p.name;
        t -= p.cycles;
    }
    return trip.back().name;  // not reached
}

// Counting: exact cycles per phase, as a counter read at the end would give.
void count() {
    std::printf("%-27s", "counting");
    for (const Phase& p : trip)
        std::printf("  %s %5.1f%%", p.name.c_str(), 100.0 * p.cycles / trip_cycles());
    std::printf("\n");
}

// Sampling: every `period` cycles, record which phase is running.
void sample(long period) {
    const long run = trips * trip_cycles();
    std::vector<long> hits(trip.size(), 0);
    long samples = 0;
    for (long c = period; c < run; c += period, ++samples)
        for (std::size_t i = 0; i < trip.size(); ++i)
            if (trip[i].name == running_at(c)) ++hits[i];
    std::printf("period %5ld (%4ld samples)", period, samples);
    for (std::size_t i = 0; i < trip.size(); ++i)
        std::printf("  %s %5.1f%%", trip[i].name.c_str(), 100.0 * hits[i] / samples);
    std::printf("\n");
}

int main() {
    std::printf("one trip = %ld cycles, %ld trips\n", trip_cycles(), trips);
    count();
    sample(1000);  // a multiple of the trip: every sample lands at one point
    sample(997);   // not a multiple: samples drift across the trip
    sample(1009);
    sample(10'007); // fewer samples: the same drift, a coarser estimate
}

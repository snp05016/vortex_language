// Follows: Vasily Volkov, "Better Performance at Lower Occupancy", GTC 2010,
// slides 7, 8, 28 and 29 (latency and throughput of a GTX480's arithmetic
// and memory pipes), and John D. C. Little, "Little's Law as Viewed on Its
// 50th Anniversary", Operations Research 59(3), 2011, section 2.1.
//
// Little's law: the average work in a system equals its throughput times the
// time each piece spends inside. So a pipe that is to deliver its full
// throughput must hold latency x throughput pieces of independent work at
// every moment. The same product sizes an arithmetic pipe, counted in
// multiply-adds, and a memory system, counted in bytes.
#include <cstdio>

struct Pipe {
    const char* name;
    long latency;    // cycles from issue to result
    long per_cycle;  // throughput when the pipe is busy
    const char* unit;
};

int main() {
    // Volkov gives memory latency as "400+" cycles (slide 7) and as under
    // 800 cycles, with a question mark (slide 28), so both rows are shown.
    // 128 bytes per cycle is his 32 four-byte loads per cycle (slide 8).
    const Pipe pipes[] = {
        {"arithmetic, one SM", 18, 32, "multiply-adds"},
        {"arithmetic, 15 SMs", 18, 480, "multiply-adds"},
        {"memory, 400 cycles", 400, 128, "bytes"},
        {"memory, 800 cycles", 800, 128, "bytes"},
    };
    std::printf("%-19s %8s %10s %10s\n", "pipe", "latency", "per cycle",
                "in flight");
    for (const Pipe& p : pipes)
        std::printf("%-19s %8ld %10ld %10ld %s\n", p.name, p.latency,
                    p.per_cycle, p.latency * p.per_cycle, p.unit);

    // Only the total in flight matters: many threads with a few bytes each,
    // or fewer threads that each keep more loads outstanding.
    const long needed = 800 * 128;
    const long per_thread[] = {4, 8, 16, 100};
    std::printf("\nbytes in flight per thread   threads to hold %ld bytes\n",
                needed);
    for (long bytes : per_thread)
        std::printf("%26ld %27ld\n", bytes, (needed + bytes - 1) / bytes);
}

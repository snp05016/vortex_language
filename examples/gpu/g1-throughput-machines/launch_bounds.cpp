// Follows: Vasily Volkov, "Better Performance at Lower Occupancy", GTC 2010:
// slide 8 (a GTX480 completes 480 multiply-adds and 32 four-byte loads per
// cycle), slide 28 (latencies of about 18 and under 800 cycles), slide 32
// (a thread stalls when it uses a loaded value, not when it issues the load),
// slide 15 (15 SMs) and slide 58 (at most 1,536 threads per SM).
//
// The stage 10 kernel run as one thread per output element c[row, column].
// Each thread's k loop is one chain of dependent multiply-adds, and each step
// needs a[row, k] and b[k, column]: two 4-byte loads that can be in flight
// together. If no cache catches a repeat, every multiply-add costs 8 bytes of
// memory traffic. Four ceilings bound the rate, and the smallest one binds.
// They are bounds computed from published figures, not measured times.
#include <algorithm>
#include <cstdio>
#include <iterator>

int main() {
    const double peak = 480;            // multiply-adds per cycle, 15 SMs
    const double arith_latency = 18;    // cycles
    const double bytes_per_cycle = 128; // memory bandwidth
    const double mem_latency = 800;     // cycles, Volkov's upper estimate
    const double bytes_per_fma = 8;     // a[row, k] and b[k, column]
    const long threads_at_once = 15 * 1536;

    struct Ceiling {
        const char* name;
        double value;  // multiply-adds per cycle
    };
    const long sizes[] = {64, 1024};  // [f32; n, n] matrices
    for (long n : sizes) {
        const long threads = n * n;
        const long resident = std::min(threads, threads_at_once);
        const double bytes_in_flight = resident * bytes_per_fma;
        const Ceiling ceilings[] = {
            {"arithmetic peak", peak},
            {"arithmetic latency", resident / arith_latency},
            {"memory bandwidth", bytes_per_cycle / bytes_per_fma},
            {"memory latency", bytes_in_flight / mem_latency / bytes_per_fma},
        };
        const Ceiling* low = std::min_element(
            std::begin(ceilings), std::end(ceilings),
            [](const Ceiling& a, const Ceiling& b) { return a.value < b.value; });

        std::printf("n = %ld: %ld threads, %ld at once, %.0f bytes in flight\n",
                    n, threads, resident, bytes_in_flight);
        for (const Ceiling& c : ceilings)
            std::printf("  %-20s %8.2f%s\n", c.name, c.value,
                        &c == low ? "  <- binds" : "");
        std::printf("  bound: %.2f multiply-adds per cycle, %.1f%% of peak\n\n",
                    low->value, 100 * low->value / peak);
    }
}

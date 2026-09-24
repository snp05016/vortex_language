// Little's law for arithmetic latency hiding: how many independent
// operations must be in flight, across threads, independent instructions
// per thread, or a mix, before a processor's arithmetic units retire
// results at their full rate instead of sitting idle between dependent ones.
//
// The three rows are one NVIDIA streaming-multiprocessor generation each:
// how many cycles one dependent instruction takes to retire, and how many
// independent instructions per cycle that generation's cores can issue once
// enough are ready. Their product is the parallelism Little's law requires.

#include <print>
#include <string_view>
#include <vector>

struct Generation {
  std::string_view name;
  int latency_cycles;       // cycles for one dependent instruction to retire
  int throughput_per_cycle; // independent instructions/cycle at full rate
};

int main() {
  // Volkov, "Better Performance at Lower Occupancy", GTC 2010, slide 11,
  // "Arithmetic parallelism in numbers".
  const std::vector<Generation> gens = {
      {"G80-GT200", 24, 8},
      {"GF100", 18, 32},
      {"GF104", 18, 48},
  };

  std::println("{:<10} {:>8} {:>11} {:>12}", "SM", "latency", "throughput", "parallelism");
  for (const auto &g : gens) {
    const int parallelism = g.latency_cycles * g.throughput_per_cycle; // Little's law
    std::println("{:<10} {:>8} {:>11} {:>12}", g.name, g.latency_cycles,
                  g.throughput_per_cycle, parallelism);
  }

  // That parallelism can come from threads (one independent instruction
  // each), from independent instructions per thread (ILP), or from a mix.
  // Fix a thread count and ask how much ILP closes the remaining gap.
  const int threads = 192;
  std::println("");
  std::println("{:<10} {:>8} {:>11}", "SM", "threads", "ILP needed");
  for (const auto &g : gens) {
    const int parallelism = g.latency_cycles * g.throughput_per_cycle;
    const int ilp_needed = (parallelism + threads - 1) / threads; // round up
    std::println("{:<10} {:>8} {:>11}", g.name, threads, ilp_needed);
  }
}

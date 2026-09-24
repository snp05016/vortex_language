// How many independent accumulators does a dependency chain need to hide an
// instruction's latency behind its throughput? If an instruction has a
// latency of L cycles (issue to result ready) and a reciprocal throughput of
// T cycles (the smallest gap between two independent issues of it, set by
// how many ports can execute it), one chain of dependent instructions can
// issue only once every L cycles: the next one cannot start until the
// previous result exists. Splitting the work across ceil(L / T) independent
// chains lets the processor issue every T cycles instead, because while one
// chain's result is still in flight, another chain has independent work
// ready to go.
//
// The three rows below are illustrative, not any real processor's numbers:
// they only exercise the formula. Follows: Agner Fog, "Optimizing subroutines
// in assembly language", section "Out-of-order execution", on hiding latency
// behind throughput with several parallel dependency chains.

#include <cstdint>
#include <print>

// Round L / T up to the next whole chain. T is a reciprocal throughput, so
// it may be a fraction (0.5 means the instruction can issue on two ports).
std::int64_t chains_needed(double latency_cycles, double reciprocal_throughput) {
  const double chains = latency_cycles / reciprocal_throughput;
  const auto whole = static_cast<std::int64_t>(chains);
  return (chains > static_cast<double>(whole)) ? whole + 1 : whole;
}

struct HypotheticalInstruction {
  const char *name;
  double latency_cycles;
  double reciprocal_throughput;
};

int main() {
  // A single-port instruction: throughput never beats latency, one chain
  // already keeps the port busy every cycle.
  // A two-port instruction with the same latency needs two chains.
  // A deeper, two-port instruction needs more chains again.
  const HypotheticalInstruction cases[] = {
      {"single-port, latency 3, 1 per port", 3.0, 1.0},
      {"two-port, latency 3, 1 per port each", 3.0, 0.5},
      {"two-port, latency 6, 1 per port each", 6.0, 0.5},
  };

  std::println("{:<38} chains needed", "instruction");
  for (const auto &instruction : cases) {
    const auto chains = chains_needed(instruction.latency_cycles, instruction.reciprocal_throughput);
    std::println("{:<38} {}", instruction.name, chains);
  }
}

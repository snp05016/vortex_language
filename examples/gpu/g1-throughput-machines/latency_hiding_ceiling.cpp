// The rate a pipelined resource achieves is bounded the same way a kernel's
// GFlop/s is bounded in the roofline model (P3): a line that rises with
// however much work is in flight, capped by a flat ceiling once enough is.
// Little's law supplies the two pieces directly: with `latency` cycles per
// operation and `in_flight` independent operations available, at most
// in_flight / latency of them can retire each cycle, and never more than
// the resource's own peak issue rate.
//
// The latency and peak throughput below are GF100's, from the same source
// as little_law.cpp: 18 cycles, 32 independent instructions/cycle, so the
// two lines meet at in_flight = 18 * 32 = 576, exactly the parallelism
// little_law.cpp computes for that row.

#include <algorithm>
#include <print>
#include <vector>

int main() {
  const int latency_cycles = 18;      // Volkov, GTC 2010, slide 11 (GF100)
  const int peak_throughput = 32;     // independent instructions/cycle
  const int ridge = latency_cycles * peak_throughput; // 576, Little's law

  const std::vector<int> in_flight_levels = {0,  72,  144, 216, 288,
                                              360, 432, 504, 576, 720};

  std::println("{:>10} {:>12} {:>9}", "in_flight", "achieved/cyc", "% of peak");
  for (int in_flight : in_flight_levels) {
    const double achieved =
        std::min(static_cast<double>(peak_throughput),
                  static_cast<double>(in_flight) / latency_cycles);
    const double percent_of_peak = 100.0 * achieved / peak_throughput;
    std::println("{:>10} {:>12.3f} {:>8.1f}%", in_flight, achieved, percent_of_peak);
  }

  std::println("");
  std::println("ridge (Little's law): {} operations in flight", ridge);
}

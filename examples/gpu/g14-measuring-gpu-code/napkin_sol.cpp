// Speed of light by hand: turn published figures into percentages of peak
// and a roofline bound, the arithmetic a profiler's first report does for
// you. Every input is quoted from Boehm's worklog (RTX A6000, 4092 x 4092
// FP32 SGEMM, December 2022): the advertised peaks, and for two kernels the
// measured GFLOP/s and the global-memory throughput he read from the
// profiler. Nothing here ran on a GPU; the outputs are derived, not measured.

#include <algorithm>
#include <print>

constexpr double n = 4092.0;
constexpr double peak_flops = 30e12;  // advertised FP32 peak, FLOP/s
constexpr double peak_bw = 768e9;     // advertised global-memory bandwidth, B/s

struct Kernel {
  const char* name;
  double flops_per_s;  // measured rate
  double bytes_per_s;  // measured global-memory throughput
};

int main() {
  const double work = 2 * n * n * n + n * n;             // FLOPs, Boehm's count
  const double least_bytes = 4 * n * n * 4;              // read A, B, C; write C
  const double no_cache_bytes = n * n * (2 * n + 1) * 4; // naive, nothing reused
  std::println("work {:.1f} GFLOP; least traffic {:.0f} MB; ridge point {:.1f} FLOP/B",
               work / 1e9, least_bytes / 1e6, peak_flops / peak_bw);
  std::println("naive with no cache reuse: {:.0f} GB, bound {:.0f} GFLOP/s\n",
               no_cache_bytes / 1e9, work / no_cache_bytes * peak_bw / 1e9);

  constexpr Kernel kernels[] = {{"1 naive", 309.0e9, 15e9},
                                {"2 coalesced", 1986.5e9, 110e9}};
  double seconds[2];
  double bytes[2];
  std::println("{:<12} {:>8} {:>8} {:>8} {:>9} {:>10} {:>12}", "kernel", "time s",
               "FP32 %", "DRAM %", "DRAM GB", "FLOP/B", "roof GFLOP/s");
  for (int k = 0; k < 2; ++k) {
    const Kernel& r = kernels[k];
    seconds[k] = work / r.flops_per_s;
    bytes[k] = r.bytes_per_s * seconds[k];  // assumes the rate held all run
    const double intensity = work / bytes[k];
    const double roof = std::min(peak_flops, intensity * peak_bw);
    std::println("{:<12} {:>8.3f} {:>8.1f} {:>8.1f} {:>9.1f} {:>10.1f} {:>12.0f}",
                 r.name, seconds[k], 100 * r.flops_per_s / peak_flops,
                 100 * r.bytes_per_s / peak_bw, bytes[k] / 1e9, intensity,
                 roof / 1e9);
  }
  // The remark cites the two facts the claim needs, and says which one was
  // derived rather than read, so no estimate passes for a measurement.
  std::println("\nremark: kernel 2 is {:.1f}x faster; derived DRAM traffic went from "
               "{:.1f} GB to {:.1f} GB, so it is not the cause; next counter to "
               "read: L1 sectors per request (G4).",
               seconds[0] / seconds[1], bytes[0] / 1e9, bytes[1] / 1e9);
}

// The "speed of light" report has two axes: achieved compute throughput and
// achieved memory throughput, each a percentage of that pipeline's own peak.
// A kernel's place on those two axes says what to look at next. This program
// applies that two-axis rule to a few illustrative readings; the readings are
// invented for teaching, not measured on any GPU, and the 60% cutoff is a
// teaching threshold, not a value a profiler defines for you.

#include <print>
#include <string_view>

constexpr double saturated = 60.0;  // a round teaching threshold, not NVIDIA's

struct Reading {
  std::string_view kernel;
  double compute_pct;
  double memory_pct;
};

std::string_view classify(Reading r) {
  const bool compute_hot = r.compute_pct >= saturated;
  const bool memory_hot = r.memory_pct >= saturated;
  if (compute_hot && memory_hot) return "near the speed of light";
  if (!compute_hot && !memory_hot)
    return "latency bound: read occupancy and stalls, not this report";
  if (r.compute_pct >= r.memory_pct) return "compute bound";
  return "memory bound";
}

int main() {
  constexpr Reading readings[] = {
      {"barely-launched kernel", 4.0, 3.0},
      {"transpose without shared memory", 12.0, 71.0},
      {"unrolled elementwise math", 88.0, 15.0},
      {"tuned tiled kernel", 91.0, 84.0},
  };
  std::println("{:<34} {:>8} {:>8}  verdict", "kernel", "compute%", "memory%");
  for (const Reading& r : readings) {
    std::println("{:<34} {:>7.0f}% {:>7.0f}%  {}", r.kernel, r.compute_pct,
                 r.memory_pct, classify(r));
  }
}

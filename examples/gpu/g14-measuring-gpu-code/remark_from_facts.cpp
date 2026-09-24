// An optimization remark names the rule that fired, not just the ratio it
// produced. This program builds a one-line remark from exactly two cited
// facts about a real pair of kernels, the way a report should cite exactly
// as many facts as it needs and no more. The two GFLOP/s figures are quoted,
// with their context, from Boehm's worklog (RTX A6000, 4092x4092 FP32, no
// tensor cores, December 2022); nothing here was run on a GPU.

#include <print>
#include <string_view>

struct Rung {
  std::string_view label;
  double gflops;
  double pct_of_cublas;
  std::string_view rule;  // the one structural fact that explains the change
};

std::string remark(Rung before, Rung after) {
  const double speedup = after.gflops / before.gflops;
  return std::format(
      "{} ({:.0f} GFLOP/s, {:.1f}% of cuBLAS) to {} ({:.0f} GFLOP/s, "
      "{:.1f}%): {:.1f}x, attributed to {}.",
      before.label, before.gflops, before.pct_of_cublas, after.label,
      after.gflops, after.pct_of_cublas, speedup, after.rule);
}

int main() {
  constexpr Rung naive{"the naive kernel", 309.0, 1.3, ""};
  constexpr Rung coalesced{"the coalesced kernel", 1987.0, 8.5,
                            "coalesced global memory access (G4)"};
  std::println("{}", remark(naive, coalesced));
}

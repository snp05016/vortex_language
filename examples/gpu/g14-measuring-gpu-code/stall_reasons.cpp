// A stall-reason report only matters once the speed-of-light report has
// already shown that neither pipeline is busy: if warps were issuing nearly
// every cycle, there is no stall to explain. This program tallies sampled
// per-cycle scheduler outcomes into a report, in the P1 style of a
// distribution rather than a single number. The samples and the reason names
// are invented for teaching; a real profiler's own reason names differ by
// tool.

#include <algorithm>
#include <array>
#include <print>
#include <string_view>

enum class Reason { Issued, WaitingOnMemory, WaitingOnBarrier, NotSelected };

std::string_view name(Reason r) {
  switch (r) {
    case Reason::Issued: return "issued an instruction";
    case Reason::WaitingOnMemory: return "waiting on a memory reply";
    case Reason::WaitingOnBarrier: return "waiting at a barrier";
    case Reason::NotSelected: return "eligible, but not selected";
  }
  return "";
}

// One sampled cycle per entry, for one warp scheduler. A real tool samples
// many more cycles across many warps; the shape of the report is the point.
constexpr std::array<Reason, 20> samples = {
    Reason::WaitingOnMemory, Reason::WaitingOnMemory, Reason::Issued,
    Reason::WaitingOnMemory, Reason::NotSelected,      Reason::WaitingOnMemory,
    Reason::Issued,          Reason::WaitingOnBarrier, Reason::WaitingOnMemory,
    Reason::WaitingOnMemory, Reason::Issued,           Reason::NotSelected,
    Reason::WaitingOnMemory, Reason::WaitingOnBarrier, Reason::WaitingOnMemory,
    Reason::Issued,          Reason::WaitingOnMemory,  Reason::NotSelected,
    Reason::WaitingOnMemory, Reason::Issued,
};

int main() {
  std::array<int, 4> counts{};
  for (Reason r : samples) ++counts[static_cast<int>(r)];

  const int issued = counts[static_cast<int>(Reason::Issued)];
  const double issue_rate = 100.0 * issued / samples.size();
  std::println("issue rate: {:.0f}% ({} of {} sampled cycles)", issue_rate,
               issued, samples.size());
  if (issue_rate >= 90.0) {
    std::println("scheduler is nearly always finding work: stall reasons "
                 "would not change the picture.");
    return 0;
  }

  std::array<std::pair<Reason, int>, 4> ranked;
  for (int i = 0; i < 4; ++i) ranked[i] = {static_cast<Reason>(i), counts[i]};
  std::sort(ranked.begin(), ranked.end(),
            [](auto a, auto b) { return a.second > b.second; });

  std::println("stall reasons, by share of sampled cycles:");
  for (auto [reason, count] : ranked) {
    if (reason == Reason::Issued) continue;
    std::println("  {:>5.0f}%  {}", 100.0 * count / samples.size(), name(reason));
  }
}

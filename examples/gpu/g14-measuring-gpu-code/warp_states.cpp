// A toy warp scheduler that records, every cycle, the state of every warp:
// "selected" (it issued), "not selected" (ready, but another warp issued) or
// "long scoreboard" (waiting for a load). It prints what Nsight Compute's
// warp-state report prints, cycles per issued instruction in each state,
// next to the issue-slot utilization that says whether the stalls matter.
// The latency and the program are made-up toy values, not any GPU's.

#include <array>
#include <print>

constexpr int load_latency = 15;  // cycles before a load's result is usable
constexpr int iterations = 64;    // each iteration: a load, then a use of it

struct Warp {
  int next = 0;         // instruction index: even = load, odd = dependent use
  int ready_at = 0;     // cycle at which the next instruction may issue
};

void run(int warps) {
  std::array<Warp, 32> w{};
  std::array<long, 3> state_cycles{};  // selected, not selected, long scoreboard
  long cycle = 0, issued = 0, last = -1;
  const int total = 2 * iterations;
  for (bool done = false; !done; ++cycle) {
    int pick = -1;
    for (int k = 1; k <= warps && pick < 0; ++k) {  // round robin from last
      const int i = static_cast<int>((last + k) % warps);
      if (w[i].next < total && w[i].ready_at <= cycle) pick = i;
    }
    for (int i = 0; i < warps; ++i) {
      if (w[i].next == total) continue;  // finished warps are not sampled
      if (i == pick) ++state_cycles[0];
      else if (w[i].ready_at <= cycle) ++state_cycles[1];
      else ++state_cycles[2];
    }
    if (pick >= 0) {
      Warp& p = w[pick];
      const bool is_load = p.next % 2 == 0;
      ++p.next;
      p.ready_at = cycle + (is_load ? load_latency : 1);
      ++issued;
      last = pick;
    }
    done = true;
    for (int i = 0; i < warps; ++i) done = done && w[i].next == total;
  }
  const double per = static_cast<double>(issued);
  std::println("{:>5} {:>7} {:>6.1f}% {:>9.2f} {:>13.2f} {:>16.2f}", warps, cycle,
               100.0 * issued / cycle, state_cycles[0] / per,
               state_cycles[1] / per, state_cycles[2] / per);
}

int main() {
  std::println("warp-cycles per issued instruction, by state (load latency {})",
               load_latency);
  std::println("{:>5} {:>7} {:>7} {:>9} {:>13} {:>16}", "warps", "cycles",
               "issue", "selected", "not selected", "long scoreboard");
  for (int warps : {1, 2, 4, 8, 16, 32}) run(warps);
}

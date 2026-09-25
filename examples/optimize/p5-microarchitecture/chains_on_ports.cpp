// How many independent dependency chains keep a core's ports busy? This
// program simulates one kind of instruction with a latency of L cycles on P
// identical, fully pipelined ports (each port can start a new instruction
// every cycle). Instruction i of the program belongs to chain i mod N, and
// may start only once the previous instruction of its own chain has produced
// its result. Each cycle, the oldest ready instructions start, at most P of
// them, as an out-of-order scheduler prefers older work. The issue rate
// climbs with N until it reaches P per cycle at N = L * P chains, and then
// stops climbing: the "knee" that a real measurement looks for. The latency
// and the port count are illustrative, not any real core's numbers.
//
// Follows: Agner Fog, "Optimizing subroutines in assembly language",
// sections 9.4 (latency and throughput) and 9.5 (break dependency chains).

#include <algorithm>
#include <print>
#include <vector>

// Returns the number of cycles needed to start all `total` instructions.
int cycles_to_issue(int total, int chains, int latency, int ports) {
  std::vector<int> next(chains);      // program index of each chain's next instruction
  std::vector<int> ready(chains, 0);  // first cycle that instruction may start
  for (int c = 0; c < chains; ++c) next[c] = c;
  int issued = 0;
  int cycle = 0;
  for (; issued < total; ++cycle) {
    std::vector<int> candidates;
    for (int c = 0; c < chains; ++c)
      if (next[c] < total && ready[c] <= cycle) candidates.push_back(c);
    std::ranges::sort(candidates, {}, [&](int c) { return next[c]; });  // oldest first
    for (int i = 0; i < std::min(ports, static_cast<int>(candidates.size())); ++i) {
      const int c = candidates[i];
      next[c] += chains;
      ready[c] = cycle + latency;  // the chain's next instruction needs this result
      ++issued;
    }
  }
  return cycle;
}

int main() {
  constexpr int total = 840, latency = 3, ports = 2;
  std::println("latency {} cycles, {} ports, {} instructions", latency, ports, total);
  std::println("the formula predicts {} chains to fill every port", latency * ports);
  std::println("chains  cycles  per cycle");
  for (int chains = 1; chains <= 8; ++chains) {
    const int cycles = cycles_to_issue(total, chains, latency, ports);
    std::println("{:>6}  {:>6}  {:>9.2f}", chains, cycles,
                 static_cast<double>(total) / cycles);
  }
}

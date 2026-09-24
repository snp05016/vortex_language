// SSA lets liveness skip the general fixed point O4 used. Because every
// definition dominates all its uses in SSA (O3), a variable's live range can
// be found by walking backward from each of its uses toward its own
// definition, one variable at a time, stopping at the definition or at a
// block already visited for that walk. No pass ever has to ask "did
// anything change", because each walk already knows where it must end.
//
// A 6-block SSA loop, condition tested at the bottom:
//   0 entry:  x0 = ...; i0 = 0; acc0 = 0
//   1 header: i1 = phi(entry: i0, latch: i2); acc1 = phi(entry: acc0, latch: acc2)
//             branch on i1, to pos or neg
//   2 pos:    acc2p = acc1 + i1              (acc1, i1 used here)
//   3 neg:    use(x0)                        (x0's only use)
//   4 latch:  acc2 = phi(pos: acc2p, neg: acc1); i2 = i1 + 1
//             branch: back to header, or out to exit
//   5 exit:   print acc2
// A phi operand's use counts at the end of its predecessor block, not inside
// the phi's own block (O3), so acc1's use in the neg-edge of acc2's phi is a
// use at block neg, and i1's use in the latch-edge of i2 is a use at latch.
// Same-block definition-then-use (i1 in its own branch, i2 feeding i1's own
// phi from latch) never makes anything live-in anywhere else, so it is left
// out below; instruction_liveness.cpp is where that finer grain belongs.
//
// Follows: Brandner, Boissinot, Darte, Dupont de Dinechin, Rastello,
// "Computing Liveness Sets for SSA-Form Programs", INRIA RR-7503, 2011.
// https://inria.hal.science/inria-00558509

#include <array>
#include <deque>
#include <print>
#include <set>
#include <string>
#include <vector>

enum Block { entry, header, pos, neg, latch, exit_, count };
const std::vector<int> succ[count] = {{header}, {pos, neg}, {latch}, {latch}, {header, exit_}, {}};
std::vector<int> pred[count];

struct Var {
  const char *name;
  int def_block;
  std::vector<int> uses;  // blocks where this value is read, other than its own def block
};
const std::vector<Var> vars = {
    {"x0", entry, {neg}},
    {"i1", header, {pos, latch}},
    {"acc1", header, {pos, neg}},
};

// Walk backward from one use toward def_block, marking every block strictly
// between them as live-in, and stopping at def_block or at a block this walk
// has already seen. Returns the number of blocks popped from the queue.
int walk_from(int start, int def_block, std::set<int> &live_in) {
  std::deque<int> work{start};
  std::set<int> seen{start};
  int visits = 0;
  while (!work.empty()) {
    const int b = work.front();
    work.pop_front();
    ++visits;
    if (b == def_block) continue;
    live_in.insert(b);
    for (int p : pred[b])
      if (seen.insert(p).second) work.push_back(p);
  }
  return visits;
}

int path_exploration(const Var &v, std::set<int> &live_in) {
  int visits = 0;
  for (int u : v.uses) visits += walk_from(u, v.def_block, live_in);
  return visits;
}

// The general fixed point from O4, computing all three variables together in
// one bitset dataflow, the way a real liveness pass would: round robin in
// postorder until a whole pass changes nothing.
enum : unsigned { X = 1, I = 2, A = 4 };
unsigned def_of[count] = {X, I | A, 0, 0, 0, 0};
unsigned use_of[count] = {0, 0, I | A, X | A, I, 0};

int fixed_point(std::array<unsigned, count> &in) {
  in.fill(0);
  int visits = 0;
  for (bool changed = true; changed;) {
    changed = false;
    for (int b = count - 1; b >= 0; --b) {  // postorder for this graph
      ++visits;
      unsigned out = 0;
      for (int s : succ[b]) out |= in[s];
      const unsigned now = use_of[b] | (out & ~def_of[b]);
      if (now != in[b]) in[b] = now, changed = true;
    }
  }
  return visits;
}

int main() {
  for (int b = 0; b < count; ++b)
    for (int s : succ[b]) pred[s].push_back(b);

  std::array<unsigned, count> fixed_in;
  const int fixed_visits = fixed_point(fixed_in);

  unsigned bit[] = {X, I, A};
  int total_walk_visits = 0;
  std::println("var   path-exploration visits   blocks marked live-in   matches fixed point");
  for (std::size_t k = 0; k < vars.size(); ++k) {
    std::set<int> live_in;
    const int visits = path_exploration(vars[k], live_in);
    total_walk_visits += visits;
    std::set<int> from_fixed;
    for (int b = 0; b < count; ++b)
      if (fixed_in[b] & bit[k]) from_fixed.insert(b);
    std::string blocks = "{";
    for (int b : live_in) blocks += (blocks.size() == 1 ? "" : ",") + std::to_string(b);
    blocks += "}";
    std::println("{:5} {:22}  {:<12}  {}", vars[k].name, visits, blocks, live_in == from_fixed ? "yes" : "no");
  }
  std::println("combined fixed point: {} block visits over its passes", fixed_visits);
  std::println("path exploration, summed over all three variables: {} visits", total_walk_visits);
}

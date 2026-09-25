// Three ways to compute block live-in sets for a small SSA loop, and a check
// that they agree. The loop (condition tested at the bottom):
//   0 entry:  x0 = ...; i0 = 0; acc0 = 0
//   1 header: i1 = phi(entry: i0, latch: i2); acc1 = phi(entry: acc0, latch: acc2)
//             branch on i1 to pos or neg
//   2 pos:    acc2p = acc1 + i1
//   3 neg:    call use(x0)
//   4 latch:  acc2 = phi(pos: acc2p, neg: acc1); i2 = i1 + 1
//             branch back to header, or on to exit
//   5 exit:   print acc2
// Only x0, i1 and acc1 are tracked. A phi's operand counts as a use at the
// end of the predecessor it comes from, so acc1's operand in acc2's phi is
// a use in neg. A phi's result counts as defined in its own block.
//   1. The iterative fixed point from O4: repeat postorder passes until one
//      pass changes nothing.
//   2. Two passes (strict SSA, reducible graph): one postorder pass that
//      ignores the back edge, then copy the loop header's live-in set into
//      every block of the loop.
//   3. Path exploration: for each use of each variable, walk predecessor
//      edges upward, marking blocks, until the definition stops the walk.
//
// Follows: Brandner, Boissinot, Darte, Dupont de Dinechin, Rastello,
// "Computing Liveness Sets for SSA-Form Programs", INRIA RR-7503, 2011, and
// the SSA book draft, chapter "Liveness" (Boissinot, Rastello), 9.2 and 9.4.

#include <print>
#include <string>
#include <vector>

enum { entry, header, pos, neg, latch, exit_, nblocks };
const char *block_name[] = {"entry", "header", "pos", "neg", "latch", "exit"};
const std::vector<int> succ[nblocks] = {{header}, {pos, neg}, {latch}, {latch}, {header, exit_}, {}};
const int back_from = latch, back_to = header;  // the one back edge
enum : unsigned { X = 1, I = 2, A = 4 };        // x0, i1, acc1
const unsigned def_of[nblocks] = {X, I | A, 0, 0, 0, 0};
const unsigned use_of[nblocks] = {0, 0, I | A, X | A, I, 0};
using Sets = std::vector<unsigned>;

// One postorder pass (blocks 5..0 is a postorder here). Returns true if any
// set changed. With skip_back_edge, the edge latch -> header is ignored.
bool backward_pass(Sets &in, bool skip_back_edge) {
  bool changed = false;
  for (int b = nblocks - 1; b >= 0; --b) {
    unsigned out = 0;
    for (int s : succ[b])
      if (!(skip_back_edge && b == back_from && s == back_to)) out |= in[s];
    const unsigned now = use_of[b] | (out & ~def_of[b]);
    if (now != in[b]) in[b] = now, changed = true;
  }
  return changed;
}

void up_and_mark(int b, unsigned v, Sets &in, const std::vector<int> preds[]) {
  if (def_of[b] & v) return;  // reached the definition: stop
  if (in[b] & v) return;      // this walk has been here already
  in[b] |= v;
  for (int p : preds[b]) up_and_mark(p, v, in, preds);
}

std::string show(unsigned s) {
  std::string r;
  const char *names[] = {"x0", "i1", "acc1"};
  for (int k = 0; k < 3; ++k)
    if (s & (1u << k)) r += std::string(r.empty() ? "" : " ") + names[k];
  return r.empty() ? "-" : r;
}

int main() {
  Sets fixed(nblocks, 0);
  int passes = 0;
  while (++passes, backward_pass(fixed, false)) {}

  Sets two(nblocks, 0);
  backward_pass(two, true);
  const Sets after_first = two;
  const unsigned live_loop = two[header] & ~def_of[header];  // phi results excluded
  for (int b : {pos, neg, latch}) two[b] |= live_loop;       // the loop's body

  std::vector<int> preds[nblocks];
  for (int b = 0; b < nblocks; ++b)
    for (int s : succ[b]) preds[s].push_back(b);
  Sets walked(nblocks, 0);
  for (unsigned v : {X, I, A})
    for (int b = 0; b < nblocks; ++b)
      if (use_of[b] & v) up_and_mark(b, v, walked, preds);

  std::println("block   fixed point   two-pass, pass 1   two-pass, pass 2   path exploration");
  for (int b = 0; b < nblocks; ++b)
    std::println("{:<6}  {:<12}  {:<17}  {:<17}  {}", block_name[b], show(fixed[b]),
                 show(after_first[b]), show(two[b]), show(walked[b]));
  std::println("fixed point: {} passes, the last one only confirming", passes);
  std::println("all three agree: {}", fixed == two && two == walked ? "yes" : "no");
}

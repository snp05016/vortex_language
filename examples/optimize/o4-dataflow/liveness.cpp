// Live variables on the while loop from guide stage 7, solved three ways:
// round robin in postorder, round robin in reverse postorder, and a worklist.
// All three find the same sets. The strategy only decides how much work that
// takes, and since liveness flows backward, against the edges, postorder is
// the helpful order here: the mirror image of the dominator computation.
//
// The blocks, one letter each; a depth-first search from A, trying true edges
// first, finishes them in the postorder F, E, D, C, B, A:
//   A: count = 0; total = 0       B: count < 10 ?
//   C: count += 1; count even ?   D: total > 10 ?
//   E: total += count             F: print(total)
// Each block is summarized by use (variables read before any write in the
// block) and def (variables written). Sets are bit masks, and all start empty:
//   out(b) = union of in(s) over the successors s of b
//   in(b)  = use(b) | (out(b) & ~def(b))
//
// Follows: Møller and Schwartzbach, Static Program Analysis, sections 5.3 and
// 5.4; Pfenning and Platzer, 15-411 lecture 4, "Liveness Analysis".

#include <array>
#include <deque>
#include <print>
#include <string>
#include <vector>

constexpr int N = 6;
constexpr char name[N] = {'A', 'B', 'C', 'D', 'E', 'F'};
enum : unsigned { count = 1, total = 2 };
const std::array<std::vector<int>, N> succ = {{{1}, {2, 5}, {1, 3}, {5, 4}, {1}, {}}};
constexpr unsigned use[N] = {0, count, count, total, count | total, total};
constexpr unsigned def[N] = {count | total, 0, count, 0, total, 0};
const std::vector<int> postorder = {5, 4, 3, 2, 1, 0}, reverse_postorder = {0, 1, 2, 3, 4, 5};
using Sets = std::array<unsigned, N>;

unsigned out_of(const Sets &in, int b) {
  unsigned out = 0;
  for (int s : succ[b]) out |= in[s];
  return out;
}

std::string show(unsigned s) {
  const char *text[] = {"{}", "{count}", "{total}", "{count, total}"};
  return text[s];
}

// Visit every block in the given order until a whole pass changes nothing.
int round_robin(const std::vector<int> &order, Sets &in, int &passes) {
  in.fill(0);
  int visits = 0;
  for (bool changed = true; changed; ++passes) {
    changed = false;
    for (int b : order) {
      ++visits;
      const unsigned now = use[b] | (out_of(in, b) & ~def[b]);
      if (now != in[b]) in[b] = now, changed = true;
    }
  }
  return visits;
}

// Revisit a block only when the live-in set of one of its successors changed.
int worklist(const std::vector<int> &order, Sets &in) {
  std::array<std::vector<int>, N> pred;
  for (int b = 0; b < N; ++b)
    for (int s : succ[b]) pred[s].push_back(b);
  in.fill(0);
  std::deque<int> work(order.begin(), order.end());
  std::array<bool, N> queued;
  queued.fill(true);
  int visits = 0;
  while (!work.empty()) {
    const int b = work.front();
    work.pop_front(), queued[b] = false, ++visits;
    const unsigned now = use[b] | (out_of(in, b) & ~def[b]);
    if (now == in[b]) continue;
    in[b] = now;
    for (int p : pred[b])
      if (!queued[p]) queued[p] = true, work.push_back(p);
  }
  return visits;
}

int main() {
  Sets post, rpo, list;
  int post_passes = 0, rpo_passes = 0;
  std::println("round robin, postorder          {:2} visits in {} passes", round_robin(postorder, post, post_passes), post_passes);
  std::println("round robin, reverse postorder  {:2} visits in {} passes", round_robin(reverse_postorder, rpo, rpo_passes), rpo_passes);
  std::println("worklist, seeded in postorder   {:2} visits", worklist(postorder, list));
  std::println("same sets from all three: {}", post == rpo && rpo == list ? "yes" : "no");
  std::println("block  live-in          live-out");
  for (int b = 0; b < N; ++b) std::println("{}      {:<16} {}", name[b], show(post[b]), show(out_of(post, b)));
}

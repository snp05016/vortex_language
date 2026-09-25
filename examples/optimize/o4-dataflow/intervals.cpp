// Interval analysis of one counting loop with bound N, where i is 0 on the
// edge into the header H:
//   H:    if i < N goto body else exit
//   body: [check 0 <= i < N]; i = i + 1; goto H
// The facts are intervals for i: at H, in the body and at the exit. The edge
// into the body refines H's interval with i < N, the exit edge with i >= N.
// Round robin runs until a round changes nothing, in three ways:
//   no widening: H's interval grows by one per round, so the analysis runs
//     about as many rounds as the loop runs iterations;
//   widening to infinity: a bound of H that grew jumps to infinity at once;
//   widening to the program's constants: a bound that grew jumps to the next
//     constant in the program (0, 1 and N here), or to infinity.
//
// Follows: Møller and Schwartzbach, Static Program Analysis, sections 6.1,
// 6.2 and 7.1.

#include <algorithm>
#include <climits>
#include <print>
#include <string>
#include <vector>

constexpr long inf = LONG_MAX;  // stands for infinity, and -inf for minus infinity

struct Interval {
  long lo = 1, hi = 0;  // lo > hi is the empty interval, the lattice's bottom
  bool empty() const { return lo > hi; }
  bool operator==(const Interval &o) const { return (empty() && o.empty()) || (lo == o.lo && hi == o.hi); }
};

Interval join(Interval a, Interval b) {
  if (a.empty()) return b;
  if (b.empty()) return a;
  return {std::min(a.lo, b.lo), std::max(a.hi, b.hi)};
}
Interval meet(Interval a, Interval b) { return {std::max(a.lo, b.lo), std::min(a.hi, b.hi)}; }
Interval plus_one(Interval a) {
  if (a.empty()) return a;
  return {a.lo == -inf ? -inf : a.lo + 1, a.hi == inf ? inf : a.hi + 1};
}

// Keep a bound that did not grow; move one that grew out to the nearest step.
Interval widen(Interval old, Interval now, const std::vector<long> &steps) {
  if (old.empty() || now.empty()) return join(old, now);
  const long lo = now.lo < old.lo ? *std::prev(std::upper_bound(steps.begin(), steps.end(), now.lo)) : old.lo;
  const long hi = now.hi > old.hi ? *std::lower_bound(steps.begin(), steps.end(), now.hi) : old.hi;
  return {lo, hi};
}

std::string show(Interval a) {
  if (a.empty()) return "empty";
  auto end = [](long v) { return v == inf ? std::string("+inf") : v == -inf ? std::string("-inf") : std::to_string(v); };
  return "[" + end(a.lo) + ", " + end(a.hi) + "]";
}

int main() {
  std::println("bound  widening              rounds  at H         in the body  at the exit");
  bool check_passes = true;
  for (long n : {100L, 1000L}) {
    for (int mode = 0; mode < 3; ++mode) {
      const std::vector<long> steps = mode == 1 ? std::vector<long>{-inf, inf} : std::vector<long>{-inf, 0, 1, n, inf};
      Interval head, body, after;
      int rounds = 0;
      for (bool changed = true; changed; ++rounds) {
        const Interval into = join({0, 0}, plus_one(body));  // from the entry and from i = i + 1
        const Interval h = mode == 0 ? into : widen(head, into, steps);
        const Interval b = meet(h, {-inf, n - 1}), e = meet(h, {n, inf});
        changed = !(h == head && b == body && e == after);
        head = h, body = b, after = e;
      }
      const char *label[] = {"none", "to infinity", "to program constants"};
      std::println("{:5}  {:<20}  {:6}  {:<11}  {:<11}  {}", n, label[mode], rounds, show(head), show(body), show(after));
      check_passes = check_passes && body.lo >= 0 && body.hi <= n - 1;
    }
  }
  std::println("every body interval lies inside [0, N - 1], so the check always passes: {}", check_passes ? "yes" : "no");
}

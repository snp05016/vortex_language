// The main loop of a backtracking allocator in the style of regalloc2: take
// the largest unallocated bundle; use a free register if one has room; else
// evict bundles that weigh less; else split at the first conflict and put
// both pieces back in the queue. Splitting raises a piece's weight (weight
// is use weight divided by length), so the loop always makes progress.
//
// Different problem from the Vortex exercise: four made-up bundles, each a
// single half-open range [start, end), and two registers.
#include <algorithm>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

struct Use { int pos, weight; };
struct Bundle { std::string name; int start, end; std::vector<Use> uses; int hint = -1; };

int length(const Bundle &b) { return b.end - b.start; }
int use_sum(const Bundle &b) {
  int s = 0;
  for (const Use &u : b.uses) s += u.weight;
  return s;
}
// Compares use_sum/length without floating point: true if a weighs more.
bool heavier(const Bundle &a, const Bundle &b) {
  return use_sum(a) * length(b) > use_sum(b) * length(a);
}
bool overlap(const Bundle &a, const Bundle &b) {
  return a.start < b.end && b.start < a.end;
}
std::string show(const Bundle &b) {
  return b.name + " [" + std::to_string(b.start) + "," + std::to_string(b.end) +
         ") weight " + std::to_string(use_sum(b)) + "/" + std::to_string(length(b));
}

void allocate(std::vector<Bundle> queue, int nregs) {
  std::vector<std::vector<Bundle>> regs(nregs);
  std::vector<Bundle> stack;
  auto by_priority = [](const Bundle &a, const Bundle &b) {
    return length(a) != length(b) ? length(a) > length(b) : a.name < b.name;
  };
  while (!queue.empty()) {
    std::sort(queue.begin(), queue.end(), by_priority);
    Bundle b = queue.front();
    queue.erase(queue.begin());
    std::cout << show(b) << "\n";

    std::vector<int> order;  // try the hinted register first
    if (b.hint >= 0) order.push_back(b.hint);
    for (int r = 0; r < nregs; ++r) if (r != b.hint) order.push_back(r);

    int free_reg = -1, evict_reg = -1, split_reg = -1, split_at = b.start;
    for (int r : order) {
      bool clash = false, all_lighter = true;
      int first = b.end;
      for (const Bundle &o : regs[r]) {
        if (!overlap(b, o)) continue;
        clash = true;
        all_lighter = all_lighter && heavier(b, o);
        first = std::min(first, std::max(b.start, o.start));
      }
      if (!clash) { free_reg = r; break; }
      if (all_lighter && evict_reg < 0) evict_reg = r;
      if (first > split_at) { split_at = first; split_reg = r; }
    }

    if (free_reg >= 0) {
      std::cout << "  take r" << free_reg << "\n";
      regs[free_reg].push_back(b);
    } else if (evict_reg >= 0) {
      auto &held = regs[evict_reg];
      for (auto it = held.begin(); it != held.end();) {
        if (overlap(b, *it)) {
          std::cout << "  evict " << it->name << " from r" << evict_reg << "\n";
          queue.push_back(*it);
          it = held.erase(it);
        } else {
          ++it;
        }
      }
      std::cout << "  take r" << evict_reg << "\n";
      held.push_back(b);
    } else if (split_reg >= 0) {
      Bundle head = b, tail = b;
      head.name += "1"; head.end = split_at; head.hint = split_reg; head.uses.clear();
      tail.name += "2"; tail.start = split_at; tail.hint = -1; tail.uses.clear();
      for (const Use &u : b.uses) (u.pos < split_at ? head : tail).uses.push_back(u);
      std::cout << "  split at " << split_at << ": " << head.name << " hinted r"
                << split_reg << ", " << tail.name << " requeued\n";
      queue.push_back(head);
      queue.push_back(tail);
    } else {
      std::cout << "  conflicts from its first position: stack slot\n";
      stack.push_back(b);
    }
  }
  for (int r = 0; r < nregs; ++r) {
    std::cout << "r" << r << ":";
    for (const Bundle &b : regs[r]) std::cout << " " << b.name;
    std::cout << "\n";
  }
  std::cout << "stack:";
  for (const Bundle &b : stack) std::cout << " " << b.name;
  std::cout << "\n";
}

}  // namespace

int main() {
  // A: long and cold. B: short and hot (a loop body). C and D: in between.
  allocate({{"A", 0, 20, {{0, 1}, {19, 1}}},
            {"B", 2, 12, {{2, 4}, {4, 4}, {6, 4}, {8, 4}, {10, 4}, {11, 4}}},
            {"C", 4, 16, {{4, 1}, {15, 1}}},
            {"D", 13, 18, {{13, 2}, {17, 2}}}},
           2);
  return 0;
}

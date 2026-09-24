// Two flow-insensitive points-to analyses of one small pointer program.
//
// Andersen's analysis reads an assignment p = q as a subset constraint:
// whatever q may point to, p may point to. It iterates to the least solution
// (cubic time in the worst case). Steensgaard's analysis reads the same
// assignment as an equality: what p and q point to becomes one class, merged
// with union-find in almost linear time. Merging loses precision: after
// r = p and r = q, p and q look as if they point to the same places.
//
// Two pointers may alias when their points-to sets intersect.
//
// Follows: Moller and Schwartzbach, "Static Program Analysis", sections 11.1
// to 11.3; Steensgaard, "Points-to Analysis in Almost Linear Time", POPL 1996.

#include <algorithm>
#include <map>
#include <print>
#include <set>
#include <string>
#include <vector>

enum class Op { AddressOf, Copy, Load, Store };  // p = &x, p = q, p = *q, *p = q
struct Stmt { Op op; std::string lhs, rhs; };
const std::vector<Stmt> program = {
    {Op::AddressOf, "p", "x"}, {Op::AddressOf, "q", "y"}, {Op::Copy, "r", "p"},
    {Op::Copy, "r", "q"},      {Op::AddressOf, "s", "r"}, {Op::Load, "t", "s"},
    {Op::AddressOf, "u", "z"}, {Op::Store, "s", "u"}};
const std::vector<std::string> vars = {"p", "q", "r", "s", "t", "u", "x", "y", "z"};
using Sets = std::map<std::string, std::set<std::string>>;

Sets andersen() {
  Sets pts;
  auto add = [&](const std::string& to, const std::set<std::string>& from) {
    std::size_t before = pts[to].size();
    pts[to].insert(from.begin(), from.end());
    return pts[to].size() != before;
  };
  // Whole rounds until nothing grows; real analyses keep a worklist instead.
  for (bool changed = true; changed;) {
    changed = false;
    for (const auto& [op, l, r] : program) {
      if (op == Op::AddressOf) changed |= add(l, {r});
      if (op == Op::Copy) changed |= add(l, pts[r]);
      if (op == Op::Load)
        for (const auto& c : std::set(pts[r])) changed |= add(l, pts[c]);
      if (op == Op::Store)
        for (const auto& c : std::set(pts[l])) changed |= add(c, pts[r]);
    }
  }
  return pts;
}

// Union-find over places. Each class may point to one other class.
std::vector<int> parent, pointee;  // pointee -1: points nowhere yet
int fresh() {
  parent.push_back(static_cast<int>(parent.size()));
  pointee.push_back(-1);
  return parent.back();
}
int find(int n) { return parent[n] == n ? n : parent[n] = find(parent[n]); }
int deref(int n) {  // the class n points to, made up if it has none yet
  n = find(n);
  if (pointee[n] == -1) { int m = fresh(); pointee[n] = m; }
  return find(pointee[n]);
}
void join(int a, int b) {
  a = find(a), b = find(b);
  if (a == b) return;
  int pa = pointee[a], pb = pointee[b];
  parent[b] = a;
  if (pa == -1) pointee[a] = pb;
  else if (pb != -1) join(pa, pb);  // merged places point to merged places
}

Sets steensgaard() {
  std::map<std::string, int> node;
  for (const auto& v : vars) node[v] = fresh();
  for (const auto& [op, l, r] : program) {
    if (op == Op::AddressOf) join(deref(node[l]), node[r]);
    if (op == Op::Copy) join(deref(node[l]), deref(node[r]));
    if (op == Op::Load) join(deref(node[l]), deref(deref(node[r])));
    if (op == Op::Store) join(deref(deref(node[l])), deref(node[r]));
  }
  Sets pts;
  for (const auto& v : vars)
    if (int target = pointee[find(node[v])]; target != -1)
      for (const auto& w : vars)
        if (find(node[w]) == find(target)) pts[v].insert(w);
  return pts;
}

void report(const char* title, Sets pts) {
  std::println("{}", title);
  for (const auto& v : vars) {
    if (pts[v].empty()) continue;
    std::string names;
    for (const auto& n : pts[v]) names += (names.empty() ? "" : ", ") + n;
    std::println("  {} -> {{{}}}", v, names);
  }
  bool shared = std::ranges::any_of(pts["p"], [&](const auto& c) { return pts["q"].contains(c); });
  std::println("  *p and *q may alias: {}", shared ? "yes" : "no");
}

int main() {
  std::println("program:");
  for (const auto& [op, l, r] : program) {
    if (op == Op::AddressOf) std::println("  {} = &{}", l, r);
    if (op == Op::Copy) std::println("  {} = {}", l, r);
    if (op == Op::Load) std::println("  {} = *{}", l, r);
    if (op == Op::Store) std::println("  *{} = {}", l, r);
  }
  report("Andersen (subsets):", andersen());
  report("Steensgaard (unification):", steensgaard());
}

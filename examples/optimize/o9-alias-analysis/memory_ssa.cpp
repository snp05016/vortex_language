// MemorySSA for a small loop, then the walker that turns "may clobber" into
// "clobbers".
//
// All of memory is one variable. A store is a MemoryDef: it makes a new
// version of memory from the one before it. A load is a MemoryUse of the
// version it sees. A MemoryPhi merges versions where paths meet, and it goes
// where O3 put phis: at the iterated dominance frontier of the blocks that
// store, here the loop header. Renaming then hands each access the nearest
// version that dominates it.
//
// That chain is conservative: a load's version may come from a store to some
// other place. The walker climbs the chain asking alias analysis about each
// store and stops at the first one that may write the loaded place. At a phi
// it follows every incoming path; a path that comes back around to the phi met
// no clobber, and if the remaining paths agree, the phi is skipped.
//
// x is a local whose address is never taken, so only x itself reaches it;
// p may point to y.
//
// Follows: LLVM's MemorySSA documentation (MemoryDef, MemoryUse, MemoryPhi,
// liveOnEntry, the walker) and MemorySSA.cpp, release/18.x (placePHINodes,
// renamePass, tryOptimizePhi).

#include <print>
#include <set>
#include <string>
#include <utility>
#include <vector>

enum class Kind { LiveOnEntry, Def, Use, Phi };
struct Access {
  Kind kind;
  std::string loc;                               // what a Def writes or a Use reads
  int from = -1;                                 // the version a Def or Use sees
  std::vector<std::pair<std::string, int>> in;   // a Phi's versions, by predecessor
  int number = 0;                                // printed name of a Def or Phi
};
std::vector<Access> acc = {{Kind::LiveOnEntry, "", -1, {}, 0}};

bool may_alias(const std::string& a, const std::string& b) {
  return a == b || (a == "*p" && b == "y") || (a == "y" && b == "*p");
}

// The first access at or above `a` that may write `loc`; -1 if the path looped.
int walk(int a, const std::string& loc, std::set<int>& open_phis) {
  while (acc[a].kind == Kind::Def && !may_alias(acc[a].loc, loc)) a = acc[a].from;
  if (acc[a].kind != Kind::Phi) return a;
  if (!open_phis.insert(a).second) return -1;
  int answer = -1;
  for (const auto& [block, version] : acc[a].in) {
    int r = walk(version, loc, open_phis);
    if (r == -1 || r == answer) continue;
    answer = (answer == -1) ? r : a;  // two different clobbers: stop at the phi
  }
  open_phis.erase(a);
  return answer == -1 ? a : answer;
}

std::string name(int a) { return a == 0 ? "liveOnEntry" : std::to_string(acc[a].number); }

int main() {
  struct Block { std::string label; std::vector<std::pair<bool, std::string>> ops; };
  const std::vector<Block> blocks = {  // (is store, place); loop -> loop | exit
      {"entry", {{true, "x"}, {true, "y"}}},
      {"loop", {{false, "x"}, {false, "y"}, {true, "*p"}}},
      {"exit", {{false, "x"}, {false, "y"}}}};

  // Number the stores first, then the phi, as LLVM does. Loads get no number.
  std::vector<std::vector<int>> ids(blocks.size());
  int defs = 0;
  for (std::size_t b = 0; b < blocks.size(); ++b)
    for (const auto& [store, place] : blocks[b].ops) {
      ids[b].push_back(static_cast<int>(acc.size()));
      acc.push_back({store ? Kind::Def : Kind::Use, place, -1, {}, store ? ++defs : 0});
    }
  const int phi = static_cast<int>(acc.size());
  acc.push_back({Kind::Phi, "", -1, {}, ++defs});

  // Rename. The dominator tree is the chain entry -> loop -> exit, so block
  // order is a walk of the tree; the loop starts from its phi.
  int current = 0;
  for (std::size_t b = 0; b < blocks.size(); ++b) {
    if (b == 1) { acc[phi].in.push_back({"entry", current}); current = phi; }
    for (int a : ids[b]) {
      acc[a].from = current;
      if (acc[a].kind == Kind::Def) current = a;
    }
    if (b == 1) acc[phi].in.push_back({"loop", current});
  }

  for (std::size_t b = 0; b < blocks.size(); ++b) {
    std::println("{}:", blocks[b].label);
    if (b == 1)
      std::println("  {} = MemoryPhi({{entry,{}}},{{loop,{}}})", name(phi),
                   name(acc[phi].in[0].second), name(acc[phi].in[1].second));
    for (int a : ids[b]) {
      if (acc[a].kind == Kind::Def) {
        std::println("  {:<28}store {}", name(a) + " = MemoryDef(" + name(acc[a].from) + ")",
                     acc[a].loc);
      } else {
        std::set<int> open_phis;
        std::println("  {:<28}load {:<4}clobbered by {}", "MemoryUse(" + name(acc[a].from) + ")",
                     acc[a].loc, name(walk(acc[a].from, acc[a].loc, open_phis)));
      }
    }
  }
}

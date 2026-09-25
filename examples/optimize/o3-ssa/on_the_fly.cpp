// SSA built while the IR is built, the way Braun and his coauthors describe:
// no dominator tree and no frontiers. Each block remembers the current value
// of each variable. A read with no definition in its own block asks the
// predecessors, and a phi joins what several predecessors return. A block whose
// predecessors are not all known yet is unsealed: a read there gets a phi with
// no operands, completed when the block is sealed. A phi whose operands are
// only itself and one other value is trivial, and that value replaces it.
//
// The toy program, built block by block as a front end would build it:
//     x = 0; y = 5; while x < y { x = x + 1 }; read x and y after the loop
// The paper also rechecks the phis that used a removed phi; none do here.
//
// Follows: Braun et al., "Simple and Efficient Construction of Static Single
// Assignment Form", CC 2013, sections 2.1 to 2.3.

#include <map>
#include <print>
#include <set>
#include <string>
#include <utility>
#include <vector>

enum Block { entry, header, body, after };
constexpr const char *block_name[] = {"entry", "header", "body", "after"};

struct Value { std::string op; Block block; std::vector<int> args; };
std::vector<Value> values;
std::vector<int> replaced_by;  // a value's own index, unless it was a trivial phi
std::map<std::pair<Block, std::string>, int> current;
std::map<Block, std::vector<Block>> preds;
std::set<Block> sealed;
std::map<Block, std::map<std::string, int>> incomplete;

int resolve(int v) { return replaced_by[v] == v ? v : resolve(replaced_by[v]); }

int make(std::string op, Block block, std::vector<int> args = {}) {
  values.push_back({std::move(op), block, std::move(args)});
  replaced_by.push_back(static_cast<int>(replaced_by.size()));
  return replaced_by.back();
}

int read(const std::string &var, Block block);

int add_operands(const std::string &var, int phi) {
  for (Block p : preds[values[phi].block]) values[phi].args.push_back(read(var, p));
  std::print("seal {}: phi v{} for {} gets", block_name[values[phi].block], phi, var);
  int other = -1;  // the one value other than the phi itself, while it stays one
  for (int a : values[phi].args) {
    a = resolve(a);
    std::print(" v{}", a);
    if (a == phi || a == other) continue;
    other = (other == -1) ? a : -2;  // -2: two different values meet here
  }
  if (other < 0) {  // -1 would mean a phi of only itself, which this toy never builds
    std::println("");
    return phi;
  }
  std::println(", so it is trivial: v{} replaces it", other);
  return replaced_by[phi] = other;
}

int read(const std::string &var, Block block) {
  if (auto it = current.find({block, var}); it != current.end()) return resolve(it->second);
  int v;
  if (!sealed.contains(block)) {
    v = incomplete[block][var] = make("phi", block);
    std::println("read {} in unsealed {}: phi v{}, no operands yet", var, block_name[block], v);
  } else if (preds[block].size() == 1) {
    v = read(var, preds[block][0]);
  } else {
    v = current[{block, var}] = make("phi", block);  // a path around a loop finds it
    v = add_operands(var, v);
  }
  return current[{block, var}] = v;
}

void seal(Block block) {
  for (const auto &[var, phi] : incomplete[block]) add_operands(var, phi);
  sealed.insert(block);
}

int main() {
  sealed.insert(entry);
  current[{entry, "x"}] = make("0", entry);
  current[{entry, "y"}] = make("5", entry);
  preds[header] = {entry};  // the edge back from the body is not there yet
  make("less", header, {read("x", header), read("y", header)});
  preds[body] = preds[after] = {header};
  seal(body);
  current[{body, "x"}] = make("add", body, {read("x", body), make("1", body)});
  preds[header].push_back(body);  // the back edge
  seal(header);
  seal(after);
  const int x = read("x", after), y = read("y", after);
  std::println("after the loop: x is v{}, y is v{}", x, y);

  for (int v = 0; v < static_cast<int>(values.size()); ++v) {
    if (resolve(v) != v) continue;
    std::print("{:>6}: v{} = {}", block_name[values[v].block], v, values[v].op);
    for (int a : values[v].args) std::print(" v{}", resolve(a));
    std::println("");
  }
}

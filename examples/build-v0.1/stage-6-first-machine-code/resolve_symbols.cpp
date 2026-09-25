// An object file has holes; the linker fills them. This models that idea for
// a tiny made-up instruction set, not for Vortex or for a real object format:
// it is small enough to run in your head, but the shape is the same one
// stage 6 describes. A "module" is a list of instructions. Some instructions
// call a symbol by name instead of by address, because the code that defines
// that symbol lives elsewhere. "Linking" resolves every such name against a
// table of already-compiled routines and reports a hole that linking cannot
// fill, the way a real linker reports an undefined symbol.
//
// Follows: cppreference on std::function, std::unordered_map and std::variant,
// used below to hold the resolvable call table and each instruction's payload.
// https://en.cppreference.com/cpp/utility/functional/function
// https://en.cppreference.com/cpp/container/unordered_map
// https://en.cppreference.com/cpp/utility/variant

#include <functional>
#include <optional>
#include <print>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <variant>
#include <vector>

namespace {

struct PushConst { int value; };
struct CallSymbol { std::string name; };  // a hole: resolved only at link time
using Instr = std::variant<PushConst, CallSymbol>;

using Routine = std::function<void(std::vector<int>&)>;

// Runs a module against an already-linked table of routines. A module with
// an unresolved call would have failed to link before reaching here; running
// it is only meaningful once every hole is filled.
void run(const std::vector<Instr>& module, const std::unordered_map<std::string, Routine>& table) {
  std::vector<int> stack;
  for (const Instr& in : module) {
    std::visit(
        [&](const auto& i) {
          using T = std::decay_t<decltype(i)>;
          if constexpr (std::is_same_v<T, PushConst>) {
            stack.push_back(i.value);
          } else {
            table.at(i.name)(stack);
          }
        },
        in);
  }
}

// Linking: check that every hole in `module` has a matching entry in `table`,
// the way a linker checks every undefined symbol against the libraries it
// was given. Returns the name of the first unresolved symbol, if any.
std::optional<std::string> link(const std::vector<Instr>& module,
                                 const std::unordered_map<std::string, Routine>& table) {
  for (const Instr& in : module) {
    if (const auto* call = std::get_if<CallSymbol>(&in); call && !table.contains(call->name)) {
      return call->name;
    }
  }
  return std::nullopt;
}

}  // namespace

int main() {
  // This object module pushes 14, then calls "print", the way stage 6's
  // program computes a result and hands it to a runtime routine it did not
  // generate itself.
  const std::vector<Instr> object_module = {
      PushConst{14},
      CallSymbol{"print"},
  };

  // The runtime library this program links against. In a real toolchain this
  // table lives in a separate compiled library; here it is a plain map.
  const std::unordered_map<std::string, Routine> runtime = {
      {"print", [](std::vector<int>& stack) {
         std::println("{}", stack.back());
         stack.pop_back();
       }},
  };

  if (const auto missing = link(object_module, runtime)) {
    std::println("link error: undefined symbol {}", *missing);
    return 1;
  }
  run(object_module, runtime);

  // A module that calls a symbol no library provides fails to link, and
  // nothing runs: a real linker in the same position writes no executable.
  const std::vector<Instr> broken_module = {CallSymbol{"trace"}};
  if (const auto missing = link(broken_module, runtime)) {
    std::println("link error: undefined symbol {}", *missing);
  }
}

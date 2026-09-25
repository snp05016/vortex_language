// Backward lowering with register-use counts, in the style of Cranelift's
// 2020 instruction selector: walk the function from its last instruction to
// its first, let each instruction's lowering absorb (fold) its operands'
// producers where the target allows, and count only the uses that still need
// a value in a register. A pure instruction whose count is still zero when
// the walk reaches it is never emitted: every consumer already absorbed it.
//
// Different problem from the Vortex exercise: a six-opcode toy IR with an
// AArch64-flavoured shifted-register add and scaled-index load.
#include <iostream>
#include <string>
#include <vector>

namespace {

struct Instr {
  std::string op;         // "input", "shl", "add", "mul", "load", "ret"
  std::vector<int> args;  // operand value numbers (v0, v1, ...)
  int imm = 0;            // shift amount, for "shl"
};

std::string v(int n) { return "v" + std::to_string(n); }

// Loads may trap and "ret" leaves the function, so both are always lowered;
// inputs are the function's parameters and exist whether used or not.
bool always_lowered(const Instr &i) {
  return i.op == "load" || i.op == "ret" || i.op == "input";
}

void lower(const std::string &title, const std::vector<Instr> &f) {
  std::vector<int> reg_uses(f.size(), 0);
  std::vector<std::string> out(f.size());
  // Reading an operand as a register counts a use; folding it does not.
  auto use = [&](int n) { reg_uses[n]++; return v(n); };
  auto is_shl = [&](int n) { return f[n].op == "shl"; };

  for (int n = static_cast<int>(f.size()) - 1; n >= 0; --n) {
    const Instr &i = f[n];
    // Every consumer of v<n> comes later in the function, so by now all of
    // them have been lowered and reg_uses[n] is final.
    if (!always_lowered(i) && reg_uses[n] == 0) continue;
    const auto &a = i.args;
    if (i.op == "input") {
      out[n] = v(n) + " = input";
    } else if (i.op == "ret") {
      out[n] = "ret " + use(a[0]);
    } else if (i.op == "shl") {
      out[n] = v(n) + " = " + use(a[0]) + " << " + std::to_string(i.imm);
    } else if (i.op == "mul") {  // no shifted form: both operands in registers
      out[n] = v(n) + " = mul " + use(a[0]) + ", " + use(a[1]);
    } else if (i.op == "add") {  // absorb a shift into "add rd, rn, rm, lsl #k"
      if (is_shl(a[1])) {
        const Instr &s = f[a[1]];
        out[n] = v(n) + " = add " + use(a[0]) + ", " + use(s.args[0]) +
                 ", lsl #" + std::to_string(s.imm);
      } else {
        out[n] = v(n) + " = add " + use(a[0]) + ", " + use(a[1]);
      }
    } else if (i.op == "load") {  // absorb add(base, shl(idx)) into [base + idx << k]
      const Instr &addr = f[a[0]];
      if (addr.op == "add" && is_shl(addr.args[1])) {
        const Instr &s = f[addr.args[1]];
        out[n] = v(n) + " = load [" + use(addr.args[0]) + " + " +
                 use(s.args[0]) + " << " + std::to_string(s.imm) + "]";
      } else {
        out[n] = v(n) + " = load [" + use(a[0]) + "]";
      }
    }
  }

  std::cout << title << ":\n";
  std::string skipped;
  for (std::size_t n = 0; n < f.size(); ++n) {
    if (!out[n].empty()) std::cout << "  " << out[n] << "\n";
    else skipped += " " + v(static_cast<int>(n));
  }
  std::cout << "  not emitted:" << skipped << "\n";
}

}  // namespace

int main() {
  // v2 = v1 << 2 and v3 = v0 + v2 feed only the load's address.
  std::vector<Instr> base = {{"input", {}}, {"input", {}}, {"shl", {1}, 2},
                             {"add", {0, 2}}, {"load", {3}}};

  auto one = base;
  one.push_back({"ret", {4}});
  lower("shift feeds one load", one);

  // v2 now has a second consumer, an add, which can also absorb the shift:
  // two uses, zero register uses, so the shift is still never computed.
  auto two = base;
  two.push_back({"add", {4, 2}});
  two.push_back({"ret", {5}});
  lower("second consumer also folds it", two);

  // A mul cannot absorb a shift, so it needs v2 in a register: v2 is emitted
  // once, and the load still repeats the shift inside its address for free.
  auto three = base;
  three.push_back({"mul", {4, 2}});
  three.push_back({"ret", {5}});
  lower("second consumer needs a register", three);
  return 0;
}

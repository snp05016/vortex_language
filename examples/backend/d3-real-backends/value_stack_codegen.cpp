// One-pass code generation with a value stack, in the style of TCC's
// developer guide: there is no tree and no IR. The parser pushes a
// descriptor for each operand saying where the value is right now (a
// constant, a named variable in memory, a register, or a spilled temporary),
// and code is generated only when an operator needs its operands in
// registers. With three registers, a fourth live result forces a spill.
//
// Different problem from the Vortex exercise: an integer calculator with
// + - * over single-letter variables, printing pseudo-assembly.
#include <cctype>
#include <iostream>
#include <string>
#include <vector>

namespace {

enum class Kind { Const, Var, Reg, Spilled };
struct Value { Kind kind; long n; char var; };  // n: constant, register or slot

class Gen {
 public:
  explicit Gen(std::string s) : s_(std::move(s)) {}
  void run() {
    sum();
    if (vstack_.back().kind == Kind::Const)
      std::cout << "  (folded to " << vstack_.back().n << ")\n";
    else
      std::cout << "  result in r" << gv(vstack_.size() - 1) << "\n";
  }

 private:
  static constexpr int kRegs = 3;

  bool reg_in_use(long r) const {
    for (const Value &e : vstack_)
      if (e.kind == Kind::Reg && e.n == r) return true;
    return false;
  }

  // Frees a register by spilling the deepest register-held entry: in an
  // expression, the deepest entry is the one the parser will need last.
  // The top two entries are the operands being combined, so they stay.
  long get_reg() {
    for (long r = 0; r < kRegs; ++r)
      if (!reg_in_use(r)) return r;
    for (std::size_t i = 0; i + 2 < vstack_.size(); ++i) {
      if (vstack_[i].kind != Kind::Reg) continue;
      long r = vstack_[i].n;
      std::cout << "  str r" << r << ", [t" << slots_ << "]\n";
      vstack_[i] = {Kind::Spilled, slots_++, 0};
      return r;
    }
    return -1;  // unreachable with three registers and two operands
  }

  // Makes stack entry i live in a register, emitting a load if needed.
  long gv(std::size_t i) {
    if (vstack_[i].kind == Kind::Reg) return vstack_[i].n;
    long r = get_reg();
    const Value e = vstack_[i];
    if (e.kind == Kind::Const) std::cout << "  mov r" << r << ", #" << e.n << "\n";
    if (e.kind == Kind::Var) std::cout << "  ldr r" << r << ", [" << e.var << "]\n";
    if (e.kind == Kind::Spilled) std::cout << "  ldr r" << r << ", [t" << e.n << "]\n";
    vstack_[i] = {Kind::Reg, r, 0};
    return r;
  }

  // Combines the top two entries. Constants fold without emitting code, a
  // multiply by 8 becomes a shift, and a constant right operand becomes an
  // immediate instead of occupying a register.
  void gen_op(char op) {
    Value b = vstack_.back();
    std::size_t ia = vstack_.size() - 2;
    Value a = vstack_[ia];
    if (a.kind == Kind::Const && b.kind == Kind::Const) {
      long r = op == '+' ? a.n + b.n : op == '-' ? a.n - b.n : a.n * b.n;
      vstack_.pop_back();
      vstack_.back() = {Kind::Const, r, 0};
      return;
    }
    long ra = gv(ia);
    std::string rhs;
    std::string name = op == '+' ? "add" : op == '-' ? "sub" : "mul";
    if (op == '*' && b.kind == Kind::Const && b.n == 8) {
      name = "lsl";
      rhs = "#3";
    } else if (b.kind == Kind::Const) {
      rhs = "#" + std::to_string(b.n);
    } else {
      rhs = "r" + std::to_string(gv(vstack_.size() - 1));
    }
    std::cout << "  " << name << " r" << ra << ", r" << ra << ", " << rhs << "\n";
    vstack_.pop_back();  // the right operand's register is free again
  }

  void atom() {
    char c = s_[pos_++];
    if (c == '(') { sum(); pos_++; return; }  // skip ')'
    if (std::isdigit(static_cast<unsigned char>(c))) vstack_.push_back({Kind::Const, c - '0', 0});
    else vstack_.push_back({Kind::Var, 0, c});
  }
  void product() {
    atom();
    while (pos_ < s_.size() && s_[pos_] == '*') { pos_++; atom(); gen_op('*'); }
  }
  void sum() {
    product();
    while (pos_ < s_.size() && (s_[pos_] == '+' || s_[pos_] == '-')) {
      char op = s_[pos_++];
      product();
      gen_op(op);
    }
  }

  std::string s_;
  std::size_t pos_ = 0;
  std::vector<Value> vstack_;
  long slots_ = 0;
};

void compile(const std::string &expr) {
  std::cout << expr << "\n";
  Gen(expr).run();
}

}  // namespace

int main() {
  compile("2+3*4");            // all constant: no instructions at all
  compile("x*8+(4-1)");        // shift, then an immediate operand
  compile("a*b+(c*d+(e*f+g*h))");  // four products alive at once: spills
  return 0;
}

// A symbolic checker for one register allocation. The program computes
// v3 = v0 + v1 and v4 = v3 * v2 with only two registers, so the allocator
// has to spill. The checker does not run the code on numbers: it tracks which
// virtual value each register and stack slot holds, and checks every read.
#include <array>
#include <cstdio>
#include <vector>

enum Kind { Load, Copy, Add, Mul, Ret };
enum Loc { r0, r1, s0, s1 };
const char* loc_name[] = {"r0", "r1", "s0", "s1"};

struct Inst {
  const char* text;
  Kind kind;
  Loc dst;
  std::vector<Loc> src;
  int def = -1;              // virtual value written (Load, Add, Mul)
  std::vector<int> uses{};   // virtual values the original program reads
};

using Program = std::vector<Inst>;

// Runs the code on concrete inputs a, b, c, as a differential test would.
int run(const Program& p, std::array<int, 3> in) {
  std::array<int, 4> val{};
  for (const Inst& i : p) {
    switch (i.kind) {
      case Load: val[i.dst] = in[i.def]; break;  // v0, v1, v2 come from a, b, c
      case Copy: val[i.dst] = val[i.src[0]]; break;
      case Add: val[i.dst] = val[i.src[0]] + val[i.src[1]]; break;
      case Mul: val[i.dst] = val[i.src[0]] * val[i.src[1]]; break;
      case Ret: return val[i.src[0]];
    }
  }
  return 0;
}

// Symbolic check: holds[l] is the virtual value in location l, or -1.
void check(const char* name, const Program& p) {
  std::array<int, 4> holds{-1, -1, -1, -1};
  for (std::size_t n = 0; n < p.size(); ++n) {
    const Inst& i = p[n];
    for (std::size_t k = 0; k < i.uses.size(); ++k) {
      if (holds[i.src[k]] != i.uses[k]) {
        std::printf("%s: step %zu \"%s\" reads %s holding v%d, expected v%d\n",
                    name, n + 1, i.text, loc_name[i.src[k]], holds[i.src[k]],
                    i.uses[k]);
        return;
      }
    }
    if (i.kind == Copy) holds[i.dst] = holds[i.src[0]];
    else if (i.kind != Ret) holds[i.dst] = i.def;
  }
  std::printf("%s: every read holds the value the program meant\n", name);
}

Program allocate(Loc reload_v2_from) {
  return {
      {"r0 = load a", Load, r0, {}, 0},
      {"r1 = load b", Load, r1, {}, 1},
      {"s0 = r1", Copy, s0, {r1}},  // spill v1
      {"r1 = load c", Load, r1, {}, 2},
      {"s1 = r1", Copy, s1, {r1}},  // spill v2
      {"r1 = s0", Copy, r1, {s0}},  // reload v1
      {"r0 = add r0, r1", Add, r0, {r0, r1}, 3, {0, 1}},
      {reload_v2_from == s1 ? "r1 = s1" : "r1 = s0", Copy, r1, {reload_v2_from}},  // reload v2
      {"r0 = mul r0, r1", Mul, r0, {r0, r1}, 4, {3, 2}},
      {"ret r0", Ret, r0, {r0}, -1, {4}},
  };
}

int main() {
  const Program good = allocate(s1), bad = allocate(s0);  // bad: wrong slot
  std::printf("inputs a=2 b=3 c=3: good returns %d, bad returns %d\n",
              run(good, {2, 3, 3}), run(bad, {2, 3, 3}));
  std::printf("inputs a=2 b=3 c=4: good returns %d, bad returns %d\n",
              run(good, {2, 3, 4}), run(bad, {2, 3, 4}));
  check("good", good);
  check("bad", bad);
}

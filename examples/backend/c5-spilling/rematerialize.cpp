// Reload or recompute? A value that must leave its register can come back
// by a load from its stack slot, or be computed again where it is needed,
// but only if what it is computed from is still available there. Follows:
// Briggs, Cooper and Torczon, "Rematerialization" (see the .toml).
//
// Instruction counts are AArch64's: a 16-bit immediate is one mov, a full
// 64-bit constant can need movz plus three movk, a global's address is adrp
// plus add, and a stack slot's address is one add to sp.
#include <cstdio>

struct Value {
  const char *name;
  int recompute;  // instructions to recompute it; 0 means it cannot be
};

int main() {
  const int uses = 4;  // the value is needed again at four places
  const Value values[] = {
      {"small constant", 1},
      {"64-bit constant", 4},
      {"global address", 2},
      {"stack address", 1},
      {"running sum", 0},  // computed from its own earlier value
  };

  std::printf("%d later uses\n", uses);
  std::printf("%-16s  reload: instrs mem | recompute: instrs mem\n", "value");
  for (const Value &v : values) {
    // Reload plan: one store to the slot, then one load per use.
    const int reload_instrs = 1 + uses;
    const int reload_mem = 1 + uses;
    if (v.recompute == 0) {
      std::printf("%-16s          %2d  %2d | not possible\n", v.name,
                  reload_instrs, reload_mem);
      continue;
    }
    // Recompute plan: no slot, no store, no load.
    std::printf("%-16s          %2d  %2d |            %2d   0\n", v.name,
                reload_instrs, reload_mem, uses * v.recompute);
  }
  return 0;
}

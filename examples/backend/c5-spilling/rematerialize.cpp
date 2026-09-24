// Rematerialization: when it is cheaper to recompute a spilled value than
// to reload it. Follows Briggs, Cooper and Torczon (see the .toml).
//
// A reload always costs one load instruction, but it also demands that the
// value was stored first, and that a stack slot holds it for as long as it
// might still be needed. Some values, such as a small constant or an
// array's base address (a fixed pointer plus a fixed offset), can instead
// be recomputed from operands that are already available wherever they are
// needed, at some fixed instruction cost. That trades a store, once, for
// paying the recompute cost again at every use.
#include <cstdio>

struct Totals {
  int reload_total;
  int remat_total;
};

// `uses` is how many times the value is needed again after its first def.
// `remat_cost` is how many instructions recomputing it takes (1 for a
// literal constant, 2 for "base register plus a fixed offset").
Totals compare(int uses, int remat_cost) {
  const int store_cost = 1;   // spilling needs exactly one store, once
  const int load_cost = 1;    // a reload is exactly one load, every time
  Totals t;
  t.reload_total = store_cost + uses * load_cost;
  t.remat_total = uses * remat_cost;   // never stored: nothing to spill
  return t;
}

int main() {
  const int uses[] = {1, 2, 4, 8};
  const int remat_costs[] = {1, 2};   // a constant, then an address

  for (int remat_cost : remat_costs) {
    std::printf("recompute cost %d instruction%s:\n", remat_cost,
                remat_cost == 1 ? "" : "s");
    for (int u : uses) {
      Totals t = compare(u, remat_cost);
      const char *winner = t.remat_total < t.reload_total   ? "rematerialize"
                            : t.remat_total > t.reload_total ? "reload"
                                                              : "tie";
      std::printf("  uses %d: reload %d, rematerialize %d -> %s\n", u,
                  t.reload_total, t.remat_total, winner);
    }
  }
  return 0;
}

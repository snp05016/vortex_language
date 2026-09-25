// Where the spill code goes. A value is defined, then a stretch of code with
// no free register runs, then a loop that does have a free register. Count
// the loads and stores that run, for two kinds of value and two strategies.
// Follows: Braun and Hack, section 1, and Wimmer and Moessenboeck,
// section 4.1 (see the .toml).
#include <cstdio>

struct Ops {
  long loads = 0;
  long stores = 0;
};

// Spill everywhere: a store after every definition, a load before every use,
// wherever they are.
Ops everywhere(long trips, bool written_in_loop) {
  Ops ops;
  ops.stores += 1;                             // after the definition
  ops.loads += trips;                          // before the read in each trip
  if (written_in_loop) ops.stores += trips;    // after the write in each trip
  ops.loads += 1;                              // before the read after the loop
  return ops;
}

// Split at the loop entry: the piece before the loop lives in memory, the
// piece from the loop entry on gets the free register.
Ops split(long trips, bool written_in_loop) {
  (void)trips;
  (void)written_in_loop;
  Ops ops;
  ops.stores += 1;  // the crowded stretch has no register for it
  ops.loads += 1;   // one reload on the loop's entry edge, outside the loop
  return ops;
}

int main() {
  const long trip_counts[] = {12, 1000};
  const struct {
    const char *name;
    bool written;
  } kinds[] = {{"base (read in loop)", false}, {"sum (read and written)", true}};

  for (long n : trip_counts) {
    std::printf("trips %ld\n", n);
    for (const auto &k : kinds) {
      Ops e = everywhere(n, k.written);
      Ops s = split(n, k.written);
      std::printf("  %-23s everywhere %4ld loads %4ld stores | split %ld load %ld store\n",
                  k.name, e.loads, e.stores, s.loads, s.stores);
    }
  }
  return 0;
}

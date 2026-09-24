// Where spill code goes matters as much as what gets spilled. Follows the
// splitting idea described for LLVM's greedy allocator (see the .toml).
//
// Say a live range cannot keep a register for its whole extent, but a
// register is free for the inner loop that dominates its use count. Under
// "spill everywhere" the whole range is memory for its whole life: every
// use reloads it and every def stores it, inside the loop included.
// "Splitting" cuts the range into pieces and only spills the pieces that
// are actually short of registers, so the piece inside the loop can stay
// in a register while the piece outside cannot.
#include <cstdio>

struct MemOps {
  int loads = 0;
  int stores = 0;
  int total() const { return loads + stores; }
};

// One def before the loop, one read per iteration inside it, one read
// after. `iterations` stands in for the trip count.
MemOps spill_everywhere(int iterations) {
  MemOps ops;
  ops.stores += 1;           // store right after the def
  ops.loads += iterations;   // one reload per loop read, every trip
  ops.loads += 1;            // one reload for the read after the loop
  return ops;
}

MemOps split(int iterations) {
  MemOps ops;
  (void)iterations;
  ops.stores += 1;   // the def still reaches memory once, in case control
                      // leaves before the loop and the value is needed later
  ops.loads += 1;     // one reload, right before the loop, into a register
                       // that then stays live through every loop read and
                       // the read after the loop: the piece inside the
                       // loop was never spilled at all
  return ops;
}

int main() {
  const int iterations = 8;   // stands in for a trip count fixed at compile time
  MemOps everywhere = spill_everywhere(iterations);
  MemOps split_ops = split(iterations);
  std::printf("spill everywhere: %d loads, %d stores, %d total\n",
              everywhere.loads, everywhere.stores, everywhere.total());
  std::printf("split:            %d loads, %d stores, %d total\n",
              split_ops.loads, split_ops.stores, split_ops.total());
  std::printf("memory ops avoided by splitting: %d\n",
              everywhere.total() - split_ops.total());
  return 0;
}

// Why a profiler replays a kernel, and what it must reset between passes.
// The "kernel" updates an array in place, through a tiny direct-mapped
// cache. A profiler that can count only one thing per run collects three
// metrics in three passes. Without restoring memory, each pass sees the
// previous pass's output; without flushing the cache, later passes see a
// warm cache. Only with both resets do the three passes describe one run.

#include <array>
#include <print>

constexpr int elements = 16;
constexpr int line_elems = 4;  // 16-byte lines of f32-sized elements
constexpr int cache_lines = 8; // big enough to hold the whole array

struct Machine {
  std::array<int, elements> memory{};
  std::array<int, cache_lines> tag{};
  void flush() { tag.fill(-1); }
};

struct Counts { int loads = 0, misses = 0; long checksum = 0; };

Counts kernel(Machine& m) {  // x[i] = 2 * x[i] + 1, one load per element
  Counts c;
  for (int i = 0; i < elements; ++i) {
    const int line = i / line_elems;
    ++c.loads;
    if (m.tag[line % cache_lines] != line) { ++c.misses; m.tag[line % cache_lines] = line; }
    m.memory[i] = 2 * m.memory[i] + 1;
    c.checksum += m.memory[i];
  }
  return c;
}

Machine fresh() {
  Machine m;
  for (int i = 0; i < elements; ++i) m.memory[i] = i;
  m.flush();
  return m;
}

void profile(bool restore_memory, bool flush_cache) {
  Machine m = fresh();
  const auto saved = m.memory;
  std::print("restore {:<3} flush {:<3} |", restore_memory ? "yes" : "no",
             flush_cache ? "yes" : "no");
  for (int pass = 1; pass <= 3; ++pass) {
    if (pass > 1 && restore_memory) m.memory = saved;
    if (pass > 1 && flush_cache) m.flush();
    const Counts c = kernel(m);
    // each pass reports one metric, as a single hardware counter would
    if (pass == 1) std::print(" loads {:>2}", c.loads);
    if (pass == 2) std::print(" | misses {:>2}", c.misses);
    if (pass == 3) std::println(" | checksum {:>4}", c.checksum);
  }
}

int main() {
  Machine once = fresh();
  const Counts c = kernel(once);
  std::println("one real run          | loads {:>2} | misses {:>2} | checksum {:>4}",
               c.loads, c.misses, c.checksum);
  profile(false, false);
  profile(true, false);
  profile(false, true);
  profile(true, true);
}

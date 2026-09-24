// Why "the tile fits in the cache" is not the whole rule: a toy direct-mapped
// cache, so every address maps to exactly one line and a conflict is easy to
// see by hand. A real cache is set-associative (several lines share each
// index, so a few colliding addresses can still coexist), which softens this
// effect but does not remove it: the addresses that collide are decided by
// the same arithmetic, stride modulo the cache's size. This is the toy
// version of what Lam, Rothberg and Wolf call self-interference: a tile
// small enough to fit the cache can still thrash it, because the *stride*
// between its rows, not just its total size, decides how the tile lands on
// the cache's lines.
//
// The toy cache: 8 lines, 4 elements (16 bytes) each, 32 elements total.
// `line_of(address)` is the address's line index. A tile of `rows` rows,
// `stride` elements apart, occupies `distinct_lines(rows, stride)` of the 8
// lines; fewer distinct lines than rows means some rows share a line and
// evict each other.
//
// Follows: the self-interference argument in Lam, Rothberg and Wolf, "The
// Cache Performance and Optimizations of Blocked Algorithms", ASPLOS 1991,
// section 3.

#include <print>
#include <set>

constexpr int line_elements = 4;
constexpr int lines = 8;
constexpr int cache_elements = line_elements * lines;  // 32

int line_of(int address) {
  return (address / line_elements) % lines;
}

int distinct_lines(int rows, int stride) {
  std::set<int> seen;
  for (int r = 0; r < rows; ++r) seen.insert(line_of(r * stride));
  return static_cast<int>(seen.size());
}

int main() {
  std::println("toy cache: {} lines x {} elements = {} elements", lines, line_elements, cache_elements);
  std::println("");

  constexpr int rows = 8;
  for (int stride : {cache_elements, cache_elements + 1, 17, 20}) {
    int distinct = distinct_lines(rows, stride);
    std::println("stride {:>3}: {} of {} rows land on distinct lines", stride, distinct, rows);
  }

  std::println("");
  std::println("stride {} in full:", cache_elements);
  for (int r = 0; r < rows; ++r)
    std::println("  row {}: address {:>4}, line {}", r, r * cache_elements, line_of(r * cache_elements));
  std::println("stride {} in full:", cache_elements + 1);
  for (int r = 0; r < rows; ++r)
    std::println("  row {}: address {:>4}, line {}", r, r * (cache_elements + 1), line_of(r * (cache_elements + 1)));
}

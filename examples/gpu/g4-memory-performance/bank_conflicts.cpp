// How many passes does one warp-wide shared-memory access take?
//
// Shared memory is split into 32 banks, and successive 4-byte words sit in
// successive banks, so word w lives in bank w % 32. Lanes that want different
// words from the same bank are served one after another; lanes that want the
// same word share one read. An access therefore takes as many passes as its
// busiest bank has distinct words. For lane t reading word t * s that number
// is gcd(s, 32), which the last column checks.

#include <algorithm>
#include <array>
#include <cstddef>
#include <numeric>
#include <print>
#include <set>
#include <string_view>

constexpr std::size_t lanes = 32;
constexpr std::size_t banks = 32;

// word(t) is the index of the 4-byte word that lane t reads.
template <class Word> std::size_t passes(Word word) {
  std::array<std::set<std::size_t>, banks> in_bank;  // distinct words per bank
  for (std::size_t t = 0; t < lanes; ++t) in_bank[word(t) % banks].insert(word(t));
  std::size_t busiest = 0;
  for (const auto &words : in_bank) busiest = std::max(busiest, words.size());
  return busiest;
}

void row(std::string_view access, std::size_t n) {
  std::println("{:<34} {:>6}", access, n);
}

int main() {
  constexpr std::size_t strides[] = {1, 2, 3, 4, 8, 16, 32, 33};
  std::println("stride  passes  gcd(stride, 32)");
  for (std::size_t s : strides)
    std::println("{:>6}  {:>6}  {:>15}", s, passes([s](std::size_t t) { return t * s; }),
                 std::gcd(s, banks));

  std::println("");
  std::println("{:<34} {:>6}", "access", "passes");
  // Lane t reads row t, column 5 of a tile stored row by row.
  row("column 5 of a 32 x 32 tile", passes([](std::size_t t) { return t * 32 + 5; }));
  row("column 5 of a 32 x 33 tile", passes([](std::size_t t) { return t * 33 + 5; }));
  row("every lane reads word 7", passes([](std::size_t) { return std::size_t{7}; }));
  row("lanes 2k and 2k + 1 share a word", passes([](std::size_t t) { return t / 2; }));
}

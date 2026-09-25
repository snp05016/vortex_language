// How many 32-byte sectors does one warp-wide load touch?
//
// The 32 lanes of a warp each supply one address, and the memory system
// serves the whole instruction in aligned 32-byte sectors. The cost is the
// number of distinct sectors, not the number of lanes: lanes that share a
// sector share its transfer. This program counts sectors for the access
// patterns in the chapter. It models the rule; nothing here runs on a GPU.

#include <cstddef>
#include <print>
#include <set>
#include <string_view>

constexpr std::size_t lanes = 32;         // threads in one warp
constexpr std::size_t sector_bytes = 32;  // the unit global memory moves in
constexpr std::size_t f32_bytes = 4;

struct Cost {
  std::size_t sectors;  // distinct aligned 32-byte chunks touched
  std::size_t used;     // distinct bytes the lanes asked for
};

// address(t) is the byte address lane t reads. PTX requires every address to
// be a multiple of the access size, so a read never straddles two sectors.
template <class Address> Cost cost(Address address, std::size_t width) {
  std::set<std::size_t> sectors, bytes;
  for (std::size_t t = 0; t < lanes; ++t) {
    const std::size_t a = address(t);
    sectors.insert(a / sector_bytes);
    for (std::size_t b = a; b < a + width; ++b) bytes.insert(b);
  }
  return {sectors.size(), bytes.size()};
}

void row(std::string_view pattern, Cost c) {
  std::println("{:<38} {:>7} {:>11} {:>10}", pattern, c.sectors,
               c.sectors * sector_bytes, c.used);
}

int main() {
  constexpr std::size_t w = f32_bytes;
  auto stride = [](std::size_t s) { return [s](std::size_t t) { return t * s * w; }; };
  std::println("{:<38} {:>7} {:>11} {:>10}", "one f32 per lane, unless noted",
               "sectors", "bytes moved", "bytes used");
  row("stride 1: lane t reads word t", cost(stride(1), w));
  row("stride 1, starting 16 bytes in",
      cost([](std::size_t t) { return 16 + t * w; }, w));
  row("reversed: lane t reads word 31 - t",
      cost([](std::size_t t) { return (31 - t) * w; }, w));
  row("stride 2", cost(stride(2), w));
  row("stride 4", cost(stride(4), w));
  row("stride 8", cost(stride(8), w));
  row("stride 64: down a column, 64-wide rows", cost(stride(64), w));
  row("every lane reads word 5", cost([](std::size_t) { return 5 * w; }, w));
  row("16 bytes per lane (float4), stride 1",
      cost([](std::size_t t) { return 16 * t; }, 16));
}

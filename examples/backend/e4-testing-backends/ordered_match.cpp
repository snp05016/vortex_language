// Three ways to check a compiler's output, run on one listing that is wrong:
// the divide by 7 in `fast_div` still uses udiv, and the multiply the test
// looks for belongs to the next function. Only the third check notices.
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

using Lines = std::vector<std::string_view>;

const Lines listing = {
    "fast_div:", "  mov w8, #7", "  udiv w0, w0, w8", "  ret",
    "scale:",    "  umull x8, w0, w1", "  lsr x0, x8, #32", "  ret",
};

bool contains(std::string_view line, std::string_view pat) {
  return line.find(pat) != std::string_view::npos;
}

// Like grep: each pattern may match anywhere, in any order.
bool anywhere(const Lines& in, const Lines& pats) {
  for (auto p : pats) {
    bool found = false;
    for (auto l : in) found = found || contains(l, p);
    if (!found) return false;
  }
  return true;
}

// Like CHECK: a cursor moves forward; each pattern must match after the last.
bool in_order(const Lines& in, const Lines& pats, std::size_t from = 0,
              std::size_t to = SIZE_MAX) {
  std::size_t cur = from;
  for (auto p : pats) {
    while (cur < in.size() && cur < to && !contains(in[cur], p)) ++cur;
    if (cur >= in.size() || cur >= to) return false;
    ++cur;
  }
  return true;
}

// Like CHECK-LABEL: find the line that holds `label`, end the block at the
// next line holding any label, and match the patterns only inside it.
bool in_block(const Lines& in, std::string_view label, const Lines& pats,
              const Lines& all_labels) {
  std::size_t start = 0;
  while (start < in.size() && !contains(in[start], label)) ++start;
  if (start == in.size()) return false;
  std::size_t end = start + 1;
  for (; end < in.size(); ++end) {
    bool is_label = false;
    for (auto l : all_labels) is_label = is_label || contains(in[end], l);
    if (is_label) break;
  }
  return in_order(in, pats, start + 1, end);
}

int main() {
  const Lines pats = {"fast_div:", "umull", "ret"};
  std::printf("grep-like   : %s\n", anywhere(listing, pats) ? "pass" : "FAIL");
  std::printf("CHECK       : %s\n", in_order(listing, pats) ? "pass" : "FAIL");
  const Lines body = {"umull", "ret"};
  const Lines labels = {"fast_div:", "scale:"};
  std::printf("CHECK-LABEL : %s\n",
              in_block(listing, "fast_div:", body, labels) ? "pass" : "FAIL");
}

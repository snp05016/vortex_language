// Walk the chain of frame records by hand, with no debug information.
//
// A function that keeps a frame record stores two 8-byte words: first the
// frame pointer it was given by its caller, then its own return address.
// The frame pointer register (x29 on AArch64, rbp on x86-64) then points at
// the first word. Each record therefore points at its caller's record, and
// the records form a linked list from the innermost call outward. This
// program records each function's frame address on the way in, then has the
// innermost function follow the list and check every link.
//
// The .toml adds -fno-omit-frame-pointer: without it, compilers for Linux
// may use the frame pointer register as an ordinary register, and the list
// is not guaranteed to exist.
//
// Follows: AAPCS64, section "The Frame Pointer"; GCC manual, "Getting the
// Return or Frame Address of a Function" (__builtin_frame_address(0)).

#include <cstdint>
#include <print>

namespace {

void* frame_of_main;
void* frame_of_outer;
void* frame_of_middle;

std::uintptr_t address_of(void* p) { return reinterpret_cast<std::uintptr_t>(p); }

// The first word of a frame record is the caller's frame pointer.
void* caller_record(void* record) { return static_cast<void**>(record)[0]; }

[[gnu::noinline]] int inner() {
  void* record = __builtin_frame_address(0);
  const char* names[] = {"middle", "outer", "main"};
  void* expected[] = {frame_of_middle, frame_of_outer, frame_of_main};
  int matched = 0;
  for (int link = 0; link < 3; ++link) {
    void* next = caller_record(record);
    bool same = next == expected[link];
    // The stack grows down, so every caller's record sits higher up.
    bool higher = address_of(next) > address_of(record);
    std::println("link {}: reaches {}'s record: {}; at a higher address: {}",
                 link + 1, names[link], same, higher);
    matched += same;
    record = next;
  }
  return matched;
}

// Each caller prints after the call returns, so the call cannot become a
// tail call that would reuse the caller's frame.
[[gnu::noinline]] int middle() {
  frame_of_middle = __builtin_frame_address(0);
  int matched = inner();
  std::println("back in middle");
  return matched;
}

[[gnu::noinline]] int outer() {
  frame_of_outer = __builtin_frame_address(0);
  int matched = middle();
  std::println("back in outer");
  return matched;
}

}  // namespace

int main() {
  frame_of_main = __builtin_frame_address(0);
  std::println("{} of 3 links matched", outer());
}

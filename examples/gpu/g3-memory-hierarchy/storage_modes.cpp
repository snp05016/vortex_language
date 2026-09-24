// Which processor may touch a GPU buffer's bytes directly depends on its
// storage mode, a property chosen when the buffer is allocated, not on
// where the code that uses it runs. This models the rule Apple documents
// for its GPUs as a table.

#include <print>
#include <string_view>

struct Mode {
  std::string_view name;
  bool cpu_visible;
  bool gpu_visible;
  std::string_view note;
};

constexpr Mode modes[] = {
    {"shared", true, true,
     "one copy, addressable by both: Apple silicon's unified memory backs it"},
    {"private", false, true, "GPU-only copy; the CPU has no address for it"},
    {"memoryless", false, true,
     "on-tile only, never backed by memory at all"},
};

int main() {
  std::println("{:<12} {:>5} {:>5}  {}", "mode", "CPU", "GPU", "note");
  for (const auto &m : modes)
    std::println("{:<12} {:>5} {:>5}  {}", m.name, m.cpu_visible, m.gpu_visible, m.note);
}

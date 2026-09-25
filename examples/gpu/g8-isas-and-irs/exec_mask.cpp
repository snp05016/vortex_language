// An if/else run the AMDGPU way: one instruction stream for the whole
// wavefront, and an execution mask (EXEC) that the compiled code itself
// sets, flips and restores. Lane l runs y = x > 0 ? x : 0 for its own x.
// Follows: LLVM, "User Guide for AMDGPU Backend", the pseudo code that
// linearizes an IF/THEN/ELSE by manipulating EXEC.
//
// Real wavefronts have 32 or 64 lanes; 8 keep every mask readable. A
// compiler would turn a branch this small into a select and drop the
// masks entirely; the branch is kept here so the masks can be seen.
#include <array>
#include <cstdint>
#include <cstdio>

constexpr int lanes = 8;
using Mask = std::uint8_t;  // bit l set: lane l is active

void show(const char* step, Mask exec) {
  std::printf("%-26s EXEC = ", step);
  for (int l = lanes - 1; l >= 0; --l) std::printf("%d", (exec >> l) & 1);
  std::printf("\n");
}

int main() {
  const std::array<float, lanes> x{3, -1, 4, -1, -5, 9, 2, -6};
  std::array<float, lanes> y{};
  Mask exec = 0xff;  // all lanes enter the region
  show("enter", exec);

  // A vector compare writes one bit per lane: a mask, not a branch.
  Mask cond = 0;
  for (int l = 0; l < lanes; ++l)
    if ((exec >> l & 1) && x[l] > 0.0f) cond |= Mask(1u << l);
  const Mask saved = exec;

  exec = saved & cond;  // THEN: only lanes whose condition is true
  show("then  (saved & cond)", exec);
  for (int l = 0; l < lanes; ++l)
    if (exec >> l & 1) y[l] = x[l];

  exec = Mask(~exec & saved);  // ELSE: the other lanes that entered
  show("else  (~EXEC & saved)", exec);
  for (int l = 0; l < lanes; ++l)
    if (exec >> l & 1) y[l] = 0.0f;

  exec = saved;  // after the region every lane that entered runs again
  show("leave (saved)", exec);

  std::printf("y =");
  for (float v : y) std::printf(" %g", static_cast<double>(v));
  std::printf("\n");
}

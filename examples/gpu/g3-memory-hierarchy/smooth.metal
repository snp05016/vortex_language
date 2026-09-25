// A three-point weighted average, y[i] = w0 x[i-1] + w1 x[i] + w2 x[i+1],
// that names all four of Metal's compute address spaces. Every x[i] is used
// by three threads, so each threadgroup copies its slice of x (plus one
// neighbour on each side) into threadgroup memory once and reads the copy
// three times. Dispatch it with threadgroups of exactly 32 threads.
// Follows: Apple, Metal Shading Language Specification, section 4.
// The harness has no Metal toolchain, so it skips this file; it was checked
// by compiling the source text at run time with makeLibrary(source:options:)
// on an Apple M4 Pro (macOS 27.0, 2026-09-24) and comparing with a CPU loop.

#include <metal_stdlib>
using namespace metal;

constant uint tile_width = 32;  // program-scope data must be `constant`

kernel void smooth(device const float *x [[buffer(0)]],    // device memory
                   device float *y [[buffer(1)]],
                   constant float *w [[buffer(2)]],        // read-only, uniform
                   constant uint &n [[buffer(3)]],
                   uint i [[thread_position_in_grid]],
                   uint t [[thread_position_in_threadgroup]]) {
  threadgroup float tile[tile_width + 2];  // one copy per threadgroup

  float mine = i < n ? x[i] : 0.0f;  // thread address space: this thread only
  tile[t + 1] = mine;
  if (t == 0) tile[0] = (i > 0 && i - 1 < n) ? x[i - 1] : 0.0f;
  if (t == tile_width - 1) tile[tile_width + 1] = i + 1 < n ? x[i + 1] : 0.0f;

  // No thread may read a neighbour's slot before the neighbour has written it.
  threadgroup_barrier(mem_flags::mem_threadgroup);

  if (i < n) y[i] = w[0] * tile[t] + w[1] * tile[t + 1] + w[2] * tile[t + 2];
}

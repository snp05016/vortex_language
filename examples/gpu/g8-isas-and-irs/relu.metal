// The same one-sided clamp as relu_nvvm.mlir and relu_spirv.mlir, at the
// one rung of Apple's ladder that Apple documents: Metal Shading Language
// source text. Apple's own build steps carry this file to an intermediate
// ".ir" file and then to a ".metallib", or a running program can hand this
// same source text to the driver and get back a compiled pipeline; either
// way, the format of the ".ir" file in between is never published.
// Follows: Apple, "Performing calculations on a GPU".
// The harness has no Metal toolchain installed, so this file is always
// skipped; it is here to be read, not compiled, by this build.

#include <metal_stdlib>
using namespace metal;

kernel void relu(device const float* x [[buffer(0)]],
                  device float* y [[buffer(1)]],
                  uint i [[thread_position_in_grid]]) {
  y[i] = max(x[i], 0.0f);
}

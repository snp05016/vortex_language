// The same SAXPY kernel in Metal Shading Language. This file holds only the
// kernel: a host program (Swift, Objective-C, or C++ with metal-cpp) builds
// it into a library, makes a compute pipeline from the function named
// "saxpy", binds the buffers at the indices below and dispatches it.
#include <metal_stdlib>
using namespace metal;

kernel void saxpy(constant float& a [[buffer(0)]],
                  device const float* x [[buffer(1)]],
                  device float* y [[buffer(2)]],
                  uint i [[thread_position_in_grid]]) {
    // No bounds check: the host dispatches with dispatchThreads(n, ...),
    // which on GPUs with nonuniform threadgroups creates exactly n threads.
    // A host that dispatched whole threadgroups would need `if (i < n)`.
    y[i] = a * x[i] + y[i];
}

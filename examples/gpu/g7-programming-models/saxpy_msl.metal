// The same SAXPY kernel in Metal Shading Language. Metal has no launch
// syntax inside this file: a host program (Swift, Objective-C or C++ with
// metal-cpp) builds this source into a library, makes a compute pipeline
// from the "saxpy" function, and dispatches it separately.
#include <metal_stdlib>
using namespace metal;

kernel void saxpy(constant float& a [[buffer(0)]],
                   device const float* x [[buffer(1)]],
                   device float* y [[buffer(2)]],
                   uint i [[thread_position_in_grid]]) {
    // No bounds check against n: the host dispatches exactly n threads,
    // so every thread's id is valid by construction.
    y[i] = a * x[i] + y[i];
}

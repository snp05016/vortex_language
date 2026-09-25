// SAXPY (y = a*x + y) as a single-source CUDA program: the kernel and the
// host code that launches it live in one .cu file, and nvcc separates the
// two when it compiles it. HIP's version has the same shape, with hip* in
// place of cuda* and either the same <<<...>>> launch or hipLaunchKernelGGL.
#include <cuda_runtime.h>

__global__ void saxpy(int n, float a, const float* x, float* y) {
    // No argument says which element is this thread's: it computes its
    // index from built-in variables.
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    // The launch counts whole blocks, so it rounds up and creates more
    // threads than elements; the extra ones must do nothing.
    if (i < n) {
        y[i] = a * x[i] + y[i];
    }
}

int main() {
    const int n = 1000;
    float *x, *y;
    cudaMallocManaged(&x, n * sizeof(float));
    cudaMallocManaged(&y, n * sizeof(float));
    for (int i = 0; i < n; ++i) {
        x[i] = 1.0f;
        y[i] = 2.0f;
    }

    const int threads_per_block = 256;
    const int blocks = (n + threads_per_block - 1) / threads_per_block;  // 4
    saxpy<<<blocks, threads_per_block>>>(n, 2.0f, x, y);
    cudaDeviceSynchronize();

    cudaFree(x);
    cudaFree(y);
    return 0;
}

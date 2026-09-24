// SAXPY (y = a*x + y) as a single-source CUDA program: the kernel and the
// host code that launches it live in one .cu file, compiled by nvcc. HIP's
// version of this file is the same shape with hip* names in place of cuda*
// and hipLaunchKernelGGL or the <<<...>>> syntax it also accepts.
#include <cuda_runtime.h>

__global__ void saxpy(int n, float a, const float* x, float* y) {
    // Every thread reads its own coordinates from built-in variables; no
    // argument tells it which element to touch.
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n) {
        y[i] = a * x[i] + y[i];
    }
}

int main() {
    const int n = 1 << 16;
    float *x, *y;
    cudaMallocManaged(&x, n * sizeof(float));
    cudaMallocManaged(&y, n * sizeof(float));
    for (int i = 0; i < n; ++i) {
        x[i] = 1.0f;
        y[i] = 2.0f;
    }

    const int threads_per_block = 256;
    const int blocks = (n + threads_per_block - 1) / threads_per_block;
    saxpy<<<blocks, threads_per_block>>>(n, 2.0f, x, y);
    cudaDeviceSynchronize();

    cudaFree(x);
    cudaFree(y);
    return 0;
}

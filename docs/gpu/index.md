<!-- generated overview -->
# GPU

<p class="page-intro">How GPUs execute and why they are fast, the software stacks that program them, and the matrix multiplication ladder that turns those facts into speed.</p>


## A. The machine

| Chapter | What it covers | Status |
| --- | --- | --- |
| [G1. Throughput machines](g1-throughput-machines.md) | Latency against throughput design, the roofline and Little's law. | Published |
| [G2. The SIMT execution model](g2-simt.md) | Threads, warps, wavefronts and SIMD-groups, and what divergence costs. | Published |
| [G3. The GPU memory hierarchy](g3-memory-hierarchy.md) | Registers, shared memory, caches and device memory, and unified memory on Apple silicon. | Published |

## B. Performance

| Chapter | What it covers | Status |
| --- | --- | --- |
| [G4. Memory performance: coalescing and bank conflicts](g4-memory-performance.md) | Coalesced access, shared-memory bank conflicts and vector loads, with transpose as the case study. | Published |
| [G5. Occupancy and latency hiding](g5-occupancy.md) | Occupancy, register pressure, and when lower occupancy wins. | Published |
| [G6. Synchronization, atomics and reductions](g6-synchronization.md) | Barriers, memory scopes, atomics, shuffles and deterministic sums. | Published |

## C. Software stack

| Chapter | What it covers | Status |
| --- | --- | --- |
| [G7. Programming models tour](g7-programming-models.md) | CUDA, HIP, OpenCL, SYCL, Metal, Vulkan and WebGPU, with a terminology table. | Published |
| [G8. ISAs and IRs](g8-isas-and-irs.md) | PTX and SASS, the AMDGPU ISA, SPIR-V, and Apple's MSL entry point. | Published |
| [G9. GPU compilers inside LLVM](g9-gpu-compilers-in-llvm.md) | The NVPTX, AMDGPU and SPIR-V back ends, uniformity, structurization and convergence. | Published |

## D. Matmul and friends

| Chapter | What it covers | Status |
| --- | --- | --- |
| [G10. The GPU matmul ladder](g10-matmul-ladder.md) | From a naive kernel to a warp-tiled one, each rung explained by one hardware fact. | Published |
| [G11. Matrix units](g11-matrix-units.md) | Tensor cores, Apple SIMD-group matrices and AMD MFMA. | Published |
| [G12. Fusion case study: FlashAttention](g12-flashattention.md) | IO-awareness, tiling with online softmax, and recomputation. | Published |
| [G13. Tile languages](g13-tile-languages.md) | Triton, CUDA Tile, CuTe DSL and Mojo: who picks the thread mapping. | Published |

## E. Measurement and beyond

| Chapter | What it covers | Status |
| --- | --- | --- |
| [G14. Measuring GPU code](g14-measuring-gpu-code.md) | Nsight Compute, rocprofv3 and Xcode's Metal tools, with a measurement method. | Published |
| [G15. Beyond GPUs: systolic arrays and accelerators](g15-systolic-arrays.md) | Systolic arrays, the TPU, and what changes for a compiler. | Published |

<!-- generated overview -->
# MLIR

<p class="page-intro">How MLIR represents programs at many levels at once, how it lowers them step by step, and how its structured operations and schedules could carry Vortex to GPUs.</p>


## Chapters

| Chapter | What it covers | Status |
| --- | --- | --- |
| [M1. Why MLIR](m1-why-mlir.md) | One infrastructure for many IR levels: dialects and progressive lowering. | Being written |
| [M2. Reading MLIR](m2-reading-mlir.md) | Operations, regions, blocks, values, types, attributes and dialects, read with mlir-opt. | Being written |
| [M3. Passes and pattern rewriting](m3-passes-and-rewriting.md) | The pass manager, canonicalization and greedy rewrite patterns. | Being written |
| [M4. Dialect conversion and lowering to LLVM](m4-dialect-conversion.md) | Conversion targets, type converters, and the path to the llvm dialect. | Being written |
| [M5. Structured ops: linalg, tensor and memref](m5-structured-ops.md) | linalg.generic, indexing maps and iterator types, tensors against buffers. | Being written |
| [M6. Loops: affine and scf](m6-affine-and-scf.md) | The affine dialect's polyhedral view, and scf.for, scf.parallel and scf.forall. | Being written |
| [M7. Bufferization](m7-bufferization.md) | From tensors to memrefs, destination-passing style and One-Shot Bufferize. | Being written |
| [M8. Vectorization in MLIR](m8-vectorization.md) | The vector dialect, vector.contract, transfers and lowering to hardware vectors. | Being written |
| [M9. Schedules as IR: the transform dialect](m9-transform-dialect.md) | Tiling, fusion and vectorization written as IR that transforms IR. | Being written |
| [M10. MLIR for GPUs](m10-mlir-for-gpus.md) | The gpu dialect, kernel outlining, and lowering to NVVM, ROCDL and SPIR-V. | Being written |
| [M11. End-to-end ML compilers](m11-ml-compilers.md) | XLA and StableHLO, TVM, IREE, Triton and Mojo: their inputs, IR stacks and targets. | Being written |
| [M12. Designing Vortex's GPU path](m12-vortex-gpu-path.md) | The options for taking Vortex to GPUs, with their tradeoffs, and no decision yet. | Being written |

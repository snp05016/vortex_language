// The same SAXPY kernel a third way: not in any vendor's source language,
// but in MLIR's "gpu" dialect, which names the shape every model shares
// (grid, block, thread) without picking CUDA, HIP, OpenCL, Metal or WGSL
// spellings for it. mlir-opt only parses and verifies this file; later
// passes could still lower it toward NVVM, ROCDL or SPIR-V.
module attributes {gpu.container_module} {
  gpu.module @kernels {
    gpu.func @saxpy(%a: f32, %x: memref<?xf32>, %y: memref<?xf32>) kernel {
      %block = gpu.block_id x
      %block_size = gpu.block_dim x
      %thread = gpu.thread_id x
      %base = arith.muli %block, %block_size : index
      %i = arith.addi %base, %thread : index
      %xv = memref.load %x[%i] : memref<?xf32>
      %yv = memref.load %y[%i] : memref<?xf32>
      %ax = arith.mulf %a, %xv : f32
      %sum = arith.addf %ax, %yv : f32
      memref.store %sum, %y[%i] : memref<?xf32>
      gpu.return
    }
  }
}

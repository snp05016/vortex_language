// The same one-thread-per-element kernel, lowered toward NVIDIA's rung of
// the ladder: MLIR's nvvm dialect, which becomes LLVM IR with NVVM
// intrinsics, the input LLVM's NVPTX back end turns into PTX text.
// gpu.thread_id and gpu.block_id become reads of PTX special registers
// (nvvm.read.ptx.sreg.*). Each memref becomes five scalar arguments (two
// pointers, an offset, a size and a stride), MLIR's own memref-descriptor
// convention rather than anything PTX requires. scf.if becomes an ordinary
// conditional branch, with the chosen value carried in a block argument;
// nothing marks where the two sides meet again.
// Follows: MLIR, "'nvvm' Dialect" and "'gpu' Dialect".
// Run: mlir-opt --convert-scf-to-cf --convert-gpu-to-nvvm relu_nvvm.mlir
module attributes {gpu.container_module} {
  gpu.module @kernels {
    gpu.func @relu(%x: memref<256xf32>, %y: memref<256xf32>) kernel {
      %b = gpu.block_id x
      %t = gpu.thread_id x
      %n = gpu.block_dim x
      %base = arith.muli %b, %n : index
      %i = arith.addi %base, %t : index
      %v = memref.load %x[%i] : memref<256xf32>
      %c0 = arith.constant 0.0 : f32
      %pos = arith.cmpf ogt, %v, %c0 : f32
      %r = scf.if %pos -> f32 {
        scf.yield %v : f32
      } else {
        scf.yield %c0 : f32
      }
      memref.store %r, %y[%i] : memref<256xf32>
      gpu.return
    }
  }
}

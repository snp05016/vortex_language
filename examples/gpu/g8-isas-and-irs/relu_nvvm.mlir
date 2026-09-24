// The same one-thread-per-element kernel, lowered toward NVIDIA's rung of
// the ladder: NVVM, the LLVM dialect NVIDIA's own back end reads. gpu.thread_id
// and gpu.block_id become calls to read special registers (nvvm.read.ptx.sreg.*),
// the two memrefs become plain pointers plus their shape as scalar arguments
// (LLVM's own memref-descriptor convention, not part of any public ABI), and
// scf.if becomes an ordinary conditional branch between two basic blocks,
// with the value each side computed carried in a block argument. Nothing
// here marks where the branch reconverges: that fact is implicit in the
// shape of the control-flow graph, the way it would be in hand-written
// LLVM IR. Run: mlir-opt --convert-scf-to-cf --convert-gpu-to-nvvm relu_nvvm.mlir
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

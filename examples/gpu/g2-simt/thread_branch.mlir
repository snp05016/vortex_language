// A thread id and a divergent branch, in the gpu dialect's own terms.
//
// gpu.thread_id and gpu.block_id are the operations a lowering pass reaches
// for instead of the hand-computed arithmetic this chapter's C++ example
// uses. The scf.if below is structured: both arms are explicit regions that
// yield a value, the shape a front end produces for an if/else and the
// shape MLIR's control-flow ops require. mlir-opt only parses, verifies and
// prints this file: nothing runs, and no target-specific lowering is
// applied.

module attributes {gpu.container_module} {
  gpu.module @kernels {
    gpu.func @classify(%out: memref<32xi32>) kernel {
      %tid = gpu.thread_id x
      %c16 = arith.constant 16 : index
      %first_half = arith.cmpi ult, %tid, %c16 : index
      %c0 = arith.constant 0 : i32
      %c1 = arith.constant 1 : i32
      // Lanes 0-15 and lanes 16-31 disagree here: this warp needs two
      // passes to finish the function, one per arm.
      %tag = scf.if %first_half -> i32 {
        scf.yield %c0 : i32
      } else {
        scf.yield %c1 : i32
      }
      memref.store %tag, %out[%tid] : memref<32xi32>
      gpu.return
    }
  }
}

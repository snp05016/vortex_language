// Two branches on a thread id, and what canonicalization leaves of each.
//
// gpu.thread_id, gpu.block_id and gpu.block_dim are the indices this
// chapter computes by hand. Both kernels branch on them, so lanes of one
// warp can disagree. Run with --canonicalize:
//
// - @classify's arms only pick a constant. The branch folds into
//   arithmetic on the condition, so no lane waits for another.
// - @increment's arm loads and stores memory, and lanes past the end of
//   the array must not do that, so the branch (the boundary guard) stays.
//
// Nothing here runs on a GPU: mlir-opt parses, verifies, rewrites and
// prints the module.

module attributes {gpu.container_module} {
  gpu.module @kernels {
    gpu.func @classify(%out: memref<32xi32>) kernel {
      %tid = gpu.thread_id x
      %c16 = arith.constant 16 : index
      %low_half = arith.cmpi ult, %tid, %c16 : index
      %c0 = arith.constant 0 : i32
      %c1 = arith.constant 1 : i32
      %tag = scf.if %low_half -> i32 {
        scf.yield %c0 : i32
      } else {
        scf.yield %c1 : i32
      }
      memref.store %tag, %out[%tid] : memref<32xi32>
      gpu.return
    }

    gpu.func @increment(%x: memref<100xf32>) kernel {
      %block = gpu.block_id x
      %size = gpu.block_dim x
      %tid = gpu.thread_id x
      %first = arith.muli %block, %size : index
      %i = arith.addi %first, %tid : index
      %n = arith.constant 100 : index
      %in_range = arith.cmpi ult, %i, %n : index
      scf.if %in_range {
        %v = memref.load %x[%i] : memref<100xf32>
        %one = arith.constant 1.0 : f32
        %w = arith.addf %v, %one : f32
        memref.store %w, %x[%i] : memref<100xf32>
      }
      gpu.return
    }
  }
}

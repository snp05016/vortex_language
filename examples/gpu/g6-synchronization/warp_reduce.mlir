// A one-warp sum reduction using gpu.shuffle instead of shared memory: each
// lane starts with one element and, after five xor-shuffle steps (offsets
// 16, 8, 4, 2, 1), every lane holds the sum of all 32 lanes' values, so any
// one of them may store the result. No gpu.barrier is needed: values move
// through registers, not memory, and the shuffle itself involves only the
// lanes of one warp. Like a barrier it must not sit behind a branch that
// splits those lanes. mlir-opt only parses, verifies and prints this file;
// nothing runs.

module attributes {gpu.container_module} {
  gpu.module @kernels {
    gpu.func @warp_sum(%in: memref<32xf32>, %out: memref<1xf32>) kernel {
      %tx = gpu.thread_id x
      %width = arith.constant 32 : i32
      %v0 = memref.load %in[%tx] : memref<32xf32>
      %off16 = arith.constant 16 : i32
      %s16, %valid16 = gpu.shuffle xor %v0, %off16, %width : f32
      %v1 = arith.addf %v0, %s16 : f32
      %off8 = arith.constant 8 : i32
      %s8, %valid8 = gpu.shuffle xor %v1, %off8, %width : f32
      %v2 = arith.addf %v1, %s8 : f32
      %off4 = arith.constant 4 : i32
      %s4, %valid4 = gpu.shuffle xor %v2, %off4, %width : f32
      %v3 = arith.addf %v2, %s4 : f32
      %off2 = arith.constant 2 : i32
      %s2, %valid2 = gpu.shuffle xor %v3, %off2, %width : f32
      %v4 = arith.addf %v3, %s2 : f32
      %off1 = arith.constant 1 : i32
      %s1, %valid1 = gpu.shuffle xor %v4, %off1, %width : f32
      %v5 = arith.addf %v4, %s1 : f32
      %c0 = arith.constant 0 : index
      memref.store %v5, %out[%c0] : memref<1xf32>
      gpu.return
    }
  }
}

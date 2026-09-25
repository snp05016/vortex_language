// A 64-thread block (two warps of 32) sums 64 values. Round 1 goes through
// workgroup (shared) memory, because it combines values held by different
// warps; the last five rounds stay inside warp 0 and use subgroup_reduce.
// The barrier sits outside every branch, so all 64 threads reach it. The
// branch tx < 32 splits the block but not a warp: warp 0 takes it whole,
// which is why a warp-level operation may sit inside it and a barrier may
// not. mlir-opt only parses, verifies and prints this file; nothing runs.

module attributes {gpu.container_module} {
  gpu.module @kernels {
    gpu.func @block_sum(%in: memref<64xf32>, %out: memref<1xf32>)
        workgroup(%tile: memref<64xf32, #gpu.address_space<workgroup>>)
        kernel {
      %tx = gpu.thread_id x
      %v = memref.load %in[%tx] : memref<64xf32>
      memref.store %v, %tile[%tx] : memref<64xf32, #gpu.address_space<workgroup>>
      gpu.barrier
      %c32 = arith.constant 32 : index
      %first_warp = arith.cmpi ult, %tx, %c32 : index
      scf.if %first_warp {
        %partner = arith.addi %tx, %c32 : index
        %a = memref.load %tile[%tx] : memref<64xf32, #gpu.address_space<workgroup>>
        %b = memref.load %tile[%partner] : memref<64xf32, #gpu.address_space<workgroup>>
        %pair = arith.addf %a, %b : f32
        %total = gpu.subgroup_reduce add %pair : (f32) -> f32
        %c0 = arith.constant 0 : index
        %lane0 = arith.cmpi eq, %tx, %c0 : index
        scf.if %lane0 {
          memref.store %total, %out[%c0] : memref<1xf32>
        }
      }
      gpu.return
    }
  }
}

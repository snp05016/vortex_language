// A tiled transpose in MLIR's gpu dialect, for 32 x 32 thread blocks.
//
// The workgroup buffer is the shared-memory tile. Its type says 32 x 33: the
// extra column is the padding that keeps the column read free of bank
// conflicts, and because it is part of the type, every later pass sees it.
// In both global accesses the lanes of a warp vary %tx, the last index, so
// neighbouring lanes touch neighbouring addresses. mlir-opt only parses,
// verifies and prints this file; nothing runs.

module attributes {gpu.container_module} {
  gpu.module @kernels {
    gpu.func @transpose(%in: memref<64x64xf32>, %out: memref<64x64xf32>)
        workgroup(%tile: memref<32x33xf32, #gpu.address_space<workgroup>>)
        kernel {
      %c32 = arith.constant 32 : index
      %tx = gpu.thread_id x
      %ty = gpu.thread_id y
      %bx = gpu.block_id x
      %by = gpu.block_id y
      %row0 = arith.muli %by, %c32 : index
      %col0 = arith.muli %bx, %c32 : index
      // Read a row of the input tile, write a row of the shared tile.
      %r = arith.addi %row0, %ty : index
      %c = arith.addi %col0, %tx : index
      %v = memref.load %in[%r, %c] : memref<64x64xf32>
      memref.store %v, %tile[%ty, %tx] : memref<32x33xf32, #gpu.address_space<workgroup>>
      gpu.barrier
      // Read a column of the shared tile, write a row of the output tile.
      %w = memref.load %tile[%tx, %ty] : memref<32x33xf32, #gpu.address_space<workgroup>>
      %orow = arith.addi %col0, %ty : index
      %ocol = arith.addi %row0, %tx : index
      memref.store %w, %out[%orow, %ocol] : memref<64x64xf32>
      gpu.return
    }
  }
}

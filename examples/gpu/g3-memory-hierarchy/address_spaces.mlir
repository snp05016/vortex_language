// Two address spaces a GPU compiler chooses between for every value. Here
// "private" memory is one buffer per thread, holding a value that only that
// thread ever touches (what a register does, and what a register spills to
// when the compiler runs out of them). "workgroup" memory is one buffer
// shared by the whole block, the only way to move a value from one thread's
// lane to another's. mlir-opt only parses, verifies and prints this file;
// nothing runs.

module attributes {gpu.container_module} {
  gpu.module @kernels {
    // Reverses one 32-wide block's slice of x into y. Thread tx doubles its
    // own value privately, then stages it so thread (31 - tx) can read it:
    // that hand-off is only possible through workgroup memory.
    gpu.func @reverse_block(%x: memref<64xf32>, %y: memref<64xf32>)
        workgroup(%tile: memref<32xf32, #gpu.address_space<workgroup>>)
        private(%doubled: memref<1xf32, #gpu.address_space<private>>)
        kernel {
      %c0 = arith.constant 0 : index
      %c2 = arith.constant 2.0 : f32
      %c31 = arith.constant 31 : index
      %c32 = arith.constant 32 : index
      %tx = gpu.thread_id x
      %bx = gpu.block_id x
      %base = arith.muli %bx, %c32 : index
      %i = arith.addi %base, %tx : index

      %v = memref.load %x[%i] : memref<64xf32>
      %d = arith.mulf %v, %c2 : f32
      memref.store %d, %doubled[%c0] : memref<1xf32, #gpu.address_space<private>>
      %d2 = memref.load %doubled[%c0] : memref<1xf32, #gpu.address_space<private>>

      memref.store %d2, %tile[%tx] : memref<32xf32, #gpu.address_space<workgroup>>
      gpu.barrier
      %rev = arith.subi %c31, %tx : index
      %r = memref.load %tile[%rev] : memref<32xf32, #gpu.address_space<workgroup>>
      memref.store %r, %y[%i] : memref<64xf32>
      gpu.return
    }
  }
}

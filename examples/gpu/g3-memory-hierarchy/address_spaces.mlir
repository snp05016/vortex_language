// The three kinds of memory a gpu.func can name. The kernel's memref
// arguments carry no address space: they point at buffers the host
// allocated in device memory. A "workgroup" attribution is one buffer per
// block, seen by every thread of that block. A "private" attribution is one
// buffer per thread, which no other thread can reach; a back end keeps it in
// registers when it can and in the thread's slice of device memory when it
// cannot. mlir-opt 18 parses, verifies and prints this file; nothing runs.
// Follows: MLIR, 'gpu' Dialect, "GPU address spaces" and "Memory attribution".

module attributes {gpu.container_module} {
  gpu.module @kernels {
    // Doubles and reverses each 32-element slice of x into y. Thread tx
    // keeps its doubled value privately, then stores it in the block's tile
    // so that thread 31 - tx can read it after the barrier. A private buffer
    // could not make that hand-off: no other thread can address it.
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

// A kernel that writes its own block and thread number, lowered with the
// default calling convention: the memref<2xi32> argument arrives as five
// scalars (two pointers, an offset, one size, one stride) that the kernel
// packs back into a descriptor. gpu.block_id and gpu.thread_id change
// meaning in the body: they become reads of NVVM special registers.
gpu.module @ids {
  gpu.func @where_am_i(%out: memref<2xi32>) kernel {
    %c0 = arith.constant 0 : index
    %c1 = arith.constant 1 : index
    %bid = gpu.block_id x
    %tid = gpu.thread_id x
    %bid32 = arith.index_cast %bid : index to i32
    %tid32 = arith.index_cast %tid : index to i32
    memref.store %bid32, %out[%c0] : memref<2xi32>
    memref.store %tid32, %out[%c1] : memref<2xi32>
    gpu.return
  }
}

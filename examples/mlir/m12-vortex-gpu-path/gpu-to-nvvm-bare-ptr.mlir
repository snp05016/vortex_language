// The same kernel, lowered with the bare-pointer calling convention.
// Because memref<2xi32> has a static shape, the kernel receives one
// pointer, and the offset, size and stride that the default convention
// passed at run time become constants inside the kernel.
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

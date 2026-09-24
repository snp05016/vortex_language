// The MLIR path's last step this machine can check: gpu.block_id and
// gpu.thread_id become reads of NVVM's special registers. No NVPTX target
// is registered here, so llc cannot turn the result into PTX, but mlir-opt
// alone already carries a kernel this far toward NVIDIA hardware.
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

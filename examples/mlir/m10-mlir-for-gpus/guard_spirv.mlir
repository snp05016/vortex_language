// Halve 250 values with 4 workgroups of 64 invocations: 256 threads for
// 250 values, so every thread checks its index first. The check becomes
// structured control flow in SPIR-V: a spirv.mlir.selection region whose
// last block merges the two paths.
//
// SPIR-V needs facts the gpu dialect leaves open, so they are written here:
//   spirv.target_env    the version, capabilities and extensions allowed
//   spirv.entry_point_abi   the workgroup size, fixed in the kernel
//   #spirv.storage_class    where each buffer lives (a storage buffer)
// --spirv-update-vce then records the smallest version, capabilities and
// extensions the module needs, which can be less than the target allows.
module attributes {
  gpu.container_module,
  spirv.target_env = #spirv.target_env<
    #spirv.vce<v1.3, [Shader], [SPV_KHR_storage_buffer_storage_class]>,
    #spirv.resource_limits<>>
} {
  gpu.module @kernels {
    gpu.func @halve(%data: memref<250xf32, #spirv.storage_class<StorageBuffer>>) kernel
        attributes {spirv.entry_point_abi = #spirv.entry_point_abi<workgroup_size = [64, 1, 1]>} {
      %c64 = arith.constant 64 : index
      %c250 = arith.constant 250 : index
      %half = arith.constant 0.5 : f32
      %bx = gpu.block_id x
      %tx = gpu.thread_id x
      %base = arith.muli %bx, %c64 : index
      %i = arith.addi %base, %tx : index
      %inside = arith.cmpi ult, %i, %c250 : index
      scf.if %inside {
        %v = memref.load %data[%i] : memref<250xf32, #spirv.storage_class<StorageBuffer>>
        %h = arith.mulf %v, %half : f32
        memref.store %h, %data[%i] : memref<250xf32, #spirv.storage_class<StorageBuffer>>
      }
      gpu.return
    }
  }
}

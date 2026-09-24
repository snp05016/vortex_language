// The same kernel as relu_nvvm.mlir, lowered toward the Khronos rung
// instead: SPIR-V, the portable binary IR that Vulkan and OpenCL drivers
// consume. gpu.thread_id and gpu.block_id become loads from builtin input
// variables (every vendor's driver supplies these under the same names),
// and the memrefs need their storage class spelled out up front because
// SPIR-V has no single default. The interesting difference is the branch:
// SPIR-V's structured control flow rule requires every selection to be
// wrapped in an explicit spirv.mlir.selection region that ends in a
// spirv.mlir.merge, naming the one block where both sides reconverge. The
// pass reaches for that rule on its own; nothing in the source asked for it.
// Run: mlir-opt --convert-gpu-to-spirv relu_spirv.mlir
module attributes {gpu.container_module, spirv.target_env = #spirv.target_env<#spirv.vce<v1.0, [Shader], [SPV_KHR_storage_buffer_storage_class]>, #spirv.resource_limits<>>} {
  gpu.module @kernels {
    gpu.func @relu(%x: memref<256xf32, #spirv.storage_class<StorageBuffer>>,
                    %y: memref<256xf32, #spirv.storage_class<StorageBuffer>>) kernel
        attributes {spirv.entry_point_abi = #spirv.entry_point_abi<workgroup_size = [64, 1, 1]>} {
      %b = gpu.block_id x
      %t = gpu.thread_id x
      %n = gpu.block_dim x
      %base = arith.muli %b, %n : index
      %i = arith.addi %base, %t : index
      %v = memref.load %x[%i] : memref<256xf32, #spirv.storage_class<StorageBuffer>>
      %c0 = arith.constant 0.0 : f32
      %pos = arith.cmpf ogt, %v, %c0 : f32
      %r = scf.if %pos -> f32 {
        scf.yield %v : f32
      } else {
        scf.yield %c0 : f32
      }
      memref.store %r, %y[%i] : memref<256xf32, #spirv.storage_class<StorageBuffer>>
      gpu.return
    }
  }
}

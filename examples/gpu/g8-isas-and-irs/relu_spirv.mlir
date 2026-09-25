// The same kernel lowered toward the Khronos rung instead: MLIR's spirv
// dialect, which mlir-translate can serialize into a SPIR-V binary.
// gpu.thread_id and gpu.block_id become loads from built-in input
// variables, and each memref must name its storage class. The workgroup
// size in spirv.entry_point_abi travels with the kernel, so gpu.block_dim
// becomes the constant 64. The branch is the interesting part: this is a
// Shader module (Vulkan's kind of SPIR-V), where every selection must
// declare its merge block, so the pass wraps the scf.if in a
// spirv.mlir.selection region; serialization writes that region out as an
// OpSelectionMerge instruction. Nothing in the source asked for it.
// Follows: MLIR, "'spirv' Dialect", section "Control Flow".
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

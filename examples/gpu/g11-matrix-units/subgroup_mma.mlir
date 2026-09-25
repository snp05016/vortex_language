// MLIR's gpu dialect names the warp-level matrix contract directly. The type
// !gpu.mma_matrix<16x16xf16, "AOp"> is a fragment: one warp's share of a
// 16 x 16 tile, used as operand A. The dialect keeps its layout opaque, so the
// only ways in and out are load_matrix and store_matrix, as with WMMA. Here
// one warp computes a 16 x 16 tile of C = A * B + C for 16 x 32 by 32 x 16
// operands: two k-steps, the second consuming the first's result as its C.
// mlir-opt parses, verifies and prints the file; nothing runs.

func.func @warp_tile(%a: memref<16x32xf16>, %b: memref<32x16xf16>, %c: memref<16x16xf32>) {
  %c0 = arith.constant 0 : index
  %c16 = arith.constant 16 : index
  %acc = gpu.subgroup_mma_load_matrix %c[%c0, %c0] {leadDimension = 16 : index}
      : memref<16x16xf32> -> !gpu.mma_matrix<16x16xf32, "COp">
  %a0 = gpu.subgroup_mma_load_matrix %a[%c0, %c0] {leadDimension = 32 : index}
      : memref<16x32xf16> -> !gpu.mma_matrix<16x16xf16, "AOp">
  %b0 = gpu.subgroup_mma_load_matrix %b[%c0, %c0] {leadDimension = 16 : index}
      : memref<32x16xf16> -> !gpu.mma_matrix<16x16xf16, "BOp">
  %d0 = gpu.subgroup_mma_compute %a0, %b0, %acc
      : !gpu.mma_matrix<16x16xf16, "AOp">, !gpu.mma_matrix<16x16xf16, "BOp"> -> !gpu.mma_matrix<16x16xf32, "COp">
  %a1 = gpu.subgroup_mma_load_matrix %a[%c0, %c16] {leadDimension = 32 : index}
      : memref<16x32xf16> -> !gpu.mma_matrix<16x16xf16, "AOp">
  %b1 = gpu.subgroup_mma_load_matrix %b[%c16, %c0] {leadDimension = 16 : index}
      : memref<32x16xf16> -> !gpu.mma_matrix<16x16xf16, "BOp">
  %d1 = gpu.subgroup_mma_compute %a1, %b1, %d0
      : !gpu.mma_matrix<16x16xf16, "AOp">, !gpu.mma_matrix<16x16xf16, "BOp"> -> !gpu.mma_matrix<16x16xf32, "COp">
  gpu.subgroup_mma_store_matrix %d1, %c[%c0, %c0] {leadDimension = 16 : index}
      : !gpu.mma_matrix<16x16xf32, "COp">, memref<16x16xf32>
  return
}

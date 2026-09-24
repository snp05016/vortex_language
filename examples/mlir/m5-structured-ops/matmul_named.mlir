// Follows: MLIR linalg dialect, "Named Payload-Carrying Ops" (linalg.matmul).
func.func @matmul_named(%a: memref<4x3xf32>, %b: memref<3x5xf32>, %c: memref<4x5xf32>) {
  linalg.matmul ins(%a, %b : memref<4x3xf32>, memref<3x5xf32>) outs(%c : memref<4x5xf32>)
  return
}

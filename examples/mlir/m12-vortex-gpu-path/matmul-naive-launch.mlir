// Vortex's own IR could reach this same shape: a fixed 4x4 matmul, staged
// through linalg on memrefs, then mapped onto the GPU. The default
// parallel-loop-to-GPU mapping asks for one thread per output element and
// nothing more, so mlir-opt gives it one thread per block.
func.func @matmul4(%a: memref<4x4xf32>, %b: memref<4x4xf32>, %c: memref<4x4xf32>) {
  linalg.matmul ins(%a, %b : memref<4x4xf32>, memref<4x4xf32>) outs(%c : memref<4x4xf32>)
  return
}

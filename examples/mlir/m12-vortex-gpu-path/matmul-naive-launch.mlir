// A fixed-shape 4x4 matmul on memrefs, mapped onto a GPU by MLIR's stock
// passes: linalg to parallel loops, parallel loops to a gpu.launch, then
// outlining. Nothing chooses a block size, so each iteration of the two
// parallel loops becomes its own block of one thread.
func.func @matmul4(%a: memref<4x4xf32>, %b: memref<4x4xf32>, %c: memref<4x4xf32>) {
  linalg.matmul ins(%a, %b : memref<4x4xf32>, memref<4x4xf32>) outs(%c : memref<4x4xf32>)
  return
}

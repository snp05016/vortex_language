// The same 4x4 matmul, with its two parallel loops tiled 2x2 before the
// GPU mapping. The loop over tiles becomes the grid and the loop inside a
// tile becomes the block: 2x2 blocks of 2x2 threads. The k loop, and every
// arithmetic operation in it, is unchanged.
func.func @matmul4(%a: memref<4x4xf32>, %b: memref<4x4xf32>, %c: memref<4x4xf32>) {
  linalg.matmul ins(%a, %b : memref<4x4xf32>, memref<4x4xf32>) outs(%c : memref<4x4xf32>)
  return
}

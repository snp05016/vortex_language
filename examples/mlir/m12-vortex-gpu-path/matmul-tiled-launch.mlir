// The same fixed 4x4 matmul, tiled 2x2 before the GPU mapping runs. Asking
// for a 2x2 tile is asking for a 2x2 block of threads at each grid point,
// so the launch configuration changes with it, with no change to the
// matmul itself.
func.func @matmul4(%a: memref<4x4xf32>, %b: memref<4x4xf32>, %c: memref<4x4xf32>) {
  linalg.matmul ins(%a, %b : memref<4x4xf32>, memref<4x4xf32>) outs(%c : memref<4x4xf32>)
  return
}

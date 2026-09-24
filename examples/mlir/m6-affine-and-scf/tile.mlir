// Element-wise add over a fixed 4x4 array: c[i, j] = a[i, j] + b[i, j].
//
// Every iteration writes a different c[i, j] and reads only a[i, j] and
// b[i, j], so no iteration depends on another. Tiling may freely change the
// order tiles are visited in; it cannot change what any iteration computes.

func.func @add2d(%a: memref<4x4xf32>, %b: memref<4x4xf32>, %c: memref<4x4xf32>) {
  affine.for %i = 0 to 4 {
    affine.for %j = 0 to 4 {
      %x = affine.load %a[%i, %j] : memref<4x4xf32>
      %y = affine.load %b[%i, %j] : memref<4x4xf32>
      %z = arith.addf %x, %y : f32
      affine.store %z, %c[%i, %j] : memref<4x4xf32>
    }
  }
  return
}

// Two loop nests that rectangular 2x2 tiling would break, and what
// -affine-loop-tile in MLIR 18.1.8 does with each.
//
// In both, iteration (i, j) reads an element that an iteration of the
// previous row, further right, wrote. Tiling runs a whole tile before the
// tile to its right, so the read would happen before the write it needs.

// @spread reads a[i - 1, 2j + 1]. The distance along j, j - (2j + 1), is
// negative and differs from one j to the next: the pass's check sees a
// negative range and refuses.
func.func @spread(%a: memref<5x8xf32>) {
  // expected-remark @below {{tiling code is illegal due to dependences}}
  affine.for %i = 1 to 5 {
    affine.for %j = 0 to 4 {
      %v = affine.load %a[%i - 1, 2 * %j + 1] : memref<5x8xf32>
      affine.store %v, %a[%i, %j] : memref<5x8xf32>
    }
  }
  return
}

// -----

// @skew reads a[i - 1, j + 1]: one fixed distance, (1, -1), the stencil of
// P6 and P9. It is just as illegal to tile, but the check misses a negative
// distance that is one fixed number, and the pass tiles it without a word.
func.func @skew(%a: memref<5x5xf32>) {
  %one = arith.constant 1.0 : f32
  affine.for %i = 1 to 5 {
    affine.for %j = 0 to 4 {
      %v = affine.load %a[%i - 1, %j + 1] : memref<5x5xf32>
      %w = arith.addf %v, %one : f32
      affine.store %w, %a[%i, %j] : memref<5x5xf32>
    }
  }
  return
}

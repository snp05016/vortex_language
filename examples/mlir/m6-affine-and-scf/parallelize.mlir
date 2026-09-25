// What -affine-parallelize proves, and what one option lets it assume.
//
// The pass turns an affine.for into an affine.parallel only when dependence
// analysis shows that no iteration depends on another. Run here with
// parallel-reductions=true, which also lets it rewrite a loop-carried
// reduction; the option is off by default.

// A 4x3 by 3x2 matrix product, the loops that linalg.matmul lowers to.
// Rows and columns write different elements of %c: provably independent.
// The k loop adds into the same element each time: it stays a loop.
func.func @matmul(%a: memref<4x3xf32>, %b: memref<3x2xf32>, %c: memref<4x2xf32>) {
  affine.for %i = 0 to 4 {
    affine.for %j = 0 to 2 {
      affine.for %k = 0 to 3 {
        %x = affine.load %a[%i, %k] : memref<4x3xf32>
        %y = affine.load %b[%k, %j] : memref<3x2xf32>
        %acc = affine.load %c[%i, %j] : memref<4x2xf32>
        %p = arith.mulf %x, %y : f32
        %s = arith.addf %acc, %p : f32
        affine.store %s, %c[%i, %j] : memref<4x2xf32>
      }
    }
  }
  return
}

// A sum carried in a value, not in memory. With the option on, the pass
// rewrites it as a parallel reduction, whose order the dialect leaves
// unspecified: for f32, a different order can round differently.
func.func @total(%v: memref<8xf32>) -> f32 {
  %zero = arith.constant 0.0 : f32
  %sum = affine.for %i = 0 to 8 iter_args(%run = %zero) -> f32 {
    %x = affine.load %v[%i] : memref<8xf32>
    %next = arith.addf %run, %x : f32
    affine.yield %next : f32
  }
  return %sum : f32
}

// The affine loop nest produced by row-sum-to-affine, given to an analysis.
//
// The parallel marker was not carried into this form, but affine loads and
// stores use subscripts that are affine functions of the loop variables, so a
// pass can compute exactly which iterations touch the same element.
// --affine-parallelize turns a loop into affine.parallel only when it proves
// that no two iterations conflict. Rows write different sums[i], so the outer
// loop qualifies. Every column of one row reads and writes the same sums[i],
// so the inner loop stays sequential.
func.func @row_sum(%m: memref<3x4xf32>, %sums: memref<3xf32>) {
  affine.for %i = 0 to 3 {
    affine.for %j = 0 to 4 {
      %x = affine.load %m[%i, %j] : memref<3x4xf32>
      %acc = affine.load %sums[%i] : memref<3xf32>
      %next = arith.addf %acc, %x : f32
      affine.store %next, %sums[%i] : memref<3xf32>
    }
  }
  return
}

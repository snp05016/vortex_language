// Scale every element of a fixed 4x3 array by factor, in place.
//
// scf.forall states that its rows are independent: nothing in one row's
// body may read or write another row, so a compiler is free to run rows in
// any order, or at once. The plain scf.for inside each row is sequential:
// row i's three elements are visited in order, though nothing here depends
// on that order either.

func.func @scale_rows(%a: memref<4x3xf32>, %factor: f32) {
  %c0 = arith.constant 0 : index
  %c1 = arith.constant 1 : index
  %c3 = arith.constant 3 : index
  scf.forall (%i) in (4) {
    scf.for %j = %c0 to %c3 step %c1 {
      %v = memref.load %a[%i, %j] : memref<4x3xf32>
      %scaled = arith.mulf %v, %factor : f32
      memref.store %scaled, %a[%i, %j] : memref<4x3xf32>
    }
  }
  return
}

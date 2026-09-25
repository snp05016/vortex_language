// Two loops that the affine dialect cannot state, written in scf.
//
// @ragged_sum: row i's trip count is loaded from memory inside the outer
// loop, so it is not fixed before the nest starts. affine.for would reject
// it as a bound (only values fixed for the whole nest may be symbols);
// scf.for takes any index value.
func.func @ragged_sum(%rows: memref<4x8xf32>, %counts: memref<4xindex>) -> f32 {
  %zero = arith.constant 0.0 : f32
  %c0 = arith.constant 0 : index
  %c1 = arith.constant 1 : index
  %c4 = arith.constant 4 : index
  %total = scf.for %i = %c0 to %c4 step %c1 iter_args(%outer = %zero) -> f32 {
    %n = memref.load %counts[%i] : memref<4xindex>
    %row_sum = scf.for %j = %c0 to %n step %c1 iter_args(%inner = %zero) -> f32 {
      %v = memref.load %rows[%i, %j] : memref<4x8xf32>
      %next = arith.addf %inner, %v : f32
      scf.yield %next : f32
    }
    %next_outer = arith.addf %outer, %row_sum : f32
    scf.yield %next_outer : f32
  }
  return %total : f32
}

// @scale_rows: scf.forall says its rows may run in any order, or at once.
// Nothing checks that claim: the writer of the IR makes it. Here it holds,
// because row i touches only row i.
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

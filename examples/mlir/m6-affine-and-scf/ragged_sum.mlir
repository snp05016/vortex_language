// Sum the first counts[i] elements of row i of a ragged table.
//
// Each row's trip count is read from memory inside the outer loop, so it is
// not fixed before the loop nest starts: it depends on which row %i is. An
// affine.for cannot take it as a bound (only a value defined outside every
// affine loop may be a symbol); scf.for has no such restriction, because it
// gives up affine's exact dependence analysis in exchange for that freedom.

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

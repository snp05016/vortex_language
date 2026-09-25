// The same row sums, lowered by a different pass.
//
// --convert-linalg-to-parallel-loops reads iterator_types: each "parallel"
// dimension becomes a dimension of an scf.parallel loop, whose iterations may
// run in any order, and each "reduction" dimension becomes an ordinary
// sequential scf.for. Compare the output with row-sum-to-affine, which
// started from the same operation.
func.func @row_sum(%m: memref<3x4xf32>, %sums: memref<3xf32>) {
  linalg.generic {
    indexing_maps = [affine_map<(i, j) -> (i, j)>, affine_map<(i, j) -> (i)>],
    iterator_types = ["parallel", "reduction"]
  } ins(%m : memref<3x4xf32>) outs(%sums : memref<3xf32>) {
  ^bb0(%x: f32, %acc: f32):
    %next = arith.addf %acc, %x : f32
    linalg.yield %next : f32
  }
  return
}

// A marker that is false, and a verifier that accepts it.
//
// Both dimensions are marked "parallel", but j is really a reduction: all
// four columns of a row add into the same sums[i]. Nothing in MLIR checks the
// claim. The file verifies, and --convert-linalg-to-parallel-loops trusts the
// marker: the output is one two-dimensional scf.parallel whose iterations
// read and write the same element with no ordering between them. The
// iterator types are a promise made by whoever wrote the operation.
func.func @row_sum(%m: memref<3x4xf32>, %sums: memref<3xf32>) {
  linalg.generic {
    indexing_maps = [affine_map<(i, j) -> (i, j)>, affine_map<(i, j) -> (i)>],
    iterator_types = ["parallel", "parallel"]
  } ins(%m : memref<3x4xf32>) outs(%sums : memref<3xf32>) {
  ^bb0(%x: f32, %acc: f32):
    %next = arith.addf %acc, %x : f32
    linalg.yield %next : f32
  }
  return
}

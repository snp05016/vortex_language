// Row sums of a 3x4 matrix, written at the highest level this chapter uses.
//
// Three dialects share one function: func for the function, linalg for the
// whole-array operation, arith for the scalar add in its body. The
// iterator_types line states two facts once: rows are independent
// ("parallel"), and the columns of one row are combined into one value
// ("reduction").
//
// The pass run here lowers the operation to an affine loop nest. Look for
// either fact in the output: neither word appears there.
func.func @row_sum(%m: memref<3x4xf32>, %sums: memref<3xf32>) {
  linalg.generic {
    indexing_maps = [affine_map<(i, j) -> (i, j)>, affine_map<(i, j) -> (i)>],
    iterator_types = ["parallel", "reduction"]
  } ins(%m : memref<3x4xf32>) outs(%sums : memref<3xf32>) {
  ^bb0(%x: f32, %acc: f32):
    // %acc is the running sum for this row; the result is written back to it.
    %next = arith.addf %acc, %x : f32
    linalg.yield %next : f32
  }
  return
}

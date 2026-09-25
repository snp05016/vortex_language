// Follows: MLIR 'linalg' dialect, "Payload-Carrying Ops" and linalg.generic.
// Sums each row of a 3x4 matrix into out[i], adding to what out already holds.
// The source has no loop: the maps and iterator types say what one step
// touches, and --convert-linalg-to-affine-loops writes the loops out.
#map_in = affine_map<(i, j) -> (i, j)>
#map_out = affine_map<(i, j) -> (i)>

func.func @row_sum_into(%in: memref<3x4xf32>, %out: memref<3xf32>) {
  linalg.generic {
    indexing_maps = [#map_in, #map_out],
    iterator_types = ["parallel", "reduction"]
  } ins(%in : memref<3x4xf32>) outs(%out : memref<3xf32>) {
  ^bb0(%elem: f32, %acc: f32):
    // %acc is out[i] as it stands before this step; the yield replaces it.
    %sum = arith.addf %acc, %elem : f32
    linalg.yield %sum : f32
  }
  return
}

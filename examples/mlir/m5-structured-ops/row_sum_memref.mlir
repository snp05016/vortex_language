// Follows: MLIR linalg dialect and memref dialect (the same reduction as
// row_sum_tensor.mlir, written on buffers instead of tensor values).
#map_in = affine_map<(i, j) -> (i, j)>
#map_out = affine_map<(i, j) -> (i)>

func.func @row_sum_inplace(%in: memref<3x4xf32>, %out: memref<3xf32>) {
  linalg.generic {
    indexing_maps = [#map_in, #map_out],
    iterator_types = ["parallel", "reduction"]
  } ins(%in : memref<3x4xf32>) outs(%out : memref<3xf32>) {
  ^bb0(%elem: f32, %acc: f32):
    %sum = arith.addf %acc, %elem : f32
    linalg.yield %sum : f32
  }
  return
}

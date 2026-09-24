// Follows: MLIR linalg dialect and tensor dialect (linalg.generic on
// tensors, destination-passing style: outs names the init value).
#map_in = affine_map<(i, j) -> (i, j)>
#map_out = affine_map<(i, j) -> (i)>

func.func @row_sum(%in: tensor<3x4xf32>, %init: tensor<3xf32>) -> tensor<3xf32> {
  %out = linalg.generic {
    indexing_maps = [#map_in, #map_out],
    iterator_types = ["parallel", "reduction"]
  } ins(%in : tensor<3x4xf32>) outs(%init : tensor<3xf32>) {
  ^bb0(%elem: f32, %acc: f32):
    %sum = arith.addf %acc, %elem : f32
    linalg.yield %sum : f32
  } -> tensor<3xf32>
  return %out : tensor<3xf32>
}

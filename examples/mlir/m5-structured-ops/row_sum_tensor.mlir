// Follows: MLIR 'tensor' dialect (tensor.empty) and 'linalg' dialect
// (linalg.fill, linalg.generic on tensors in destination-passing style).
// The same row sum as a function of values: nothing is written through an
// argument, and the result starts from zeros instead of the caller's data.
#map_in = affine_map<(i, j) -> (i, j)>
#map_out = affine_map<(i, j) -> (i)>

func.func @row_sums(%in: tensor<3x4xf32>) -> tensor<3xf32> {
  %zero = arith.constant 0.0 : f32
  // A shape with unspecified contents: only the destination's type matters.
  %empty = tensor.empty() : tensor<3xf32>
  %init = linalg.fill ins(%zero : f32) outs(%empty : tensor<3xf32>) -> tensor<3xf32>
  %sums = linalg.generic {
    indexing_maps = [#map_in, #map_out],
    iterator_types = ["parallel", "reduction"]
  } ins(%in : tensor<3x4xf32>) outs(%init : tensor<3xf32>) {
  ^bb0(%elem: f32, %acc: f32):
    %sum = arith.addf %acc, %elem : f32
    linalg.yield %sum : f32
  } -> tensor<3xf32>
  return %sums : tensor<3xf32>
}

// Two ways to give a linalg.generic its destination. @scale has no output
// of its own to write into, so tensor.empty stands in and bufferization must
// allocate. @scale_into receives its destination as a parameter, so the
// result can live in the caller's buffer and no allocation is needed.

#id = affine_map<(i) -> (i)>

func.func @scale(%in: tensor<4xf32>, %factor: f32) -> tensor<4xf32> {
  %init = tensor.empty() : tensor<4xf32>
  %r = linalg.generic {indexing_maps = [#id, #id], iterator_types = ["parallel"]}
      ins(%in : tensor<4xf32>) outs(%init : tensor<4xf32>) {
  ^bb0(%x: f32, %unused: f32):
    %y = arith.mulf %x, %factor : f32
    linalg.yield %y : f32
  } -> tensor<4xf32>
  return %r : tensor<4xf32>
}

func.func @scale_into(%in: tensor<4xf32>, %factor: f32, %dest: tensor<4xf32>)
    -> tensor<4xf32> {
  %r = linalg.generic {indexing_maps = [#id, #id], iterator_types = ["parallel"]}
      ins(%in : tensor<4xf32>) outs(%dest : tensor<4xf32>) {
  ^bb0(%x: f32, %unused: f32):
    %y = arith.mulf %x, %factor : f32
    linalg.yield %y : f32
  } -> tensor<4xf32>
  return %r : tensor<4xf32>
}

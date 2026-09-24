// A tensor-valued op written in destination-passing style: outs names the
// value the result must overwrite. tensor.empty supplies a nameless "any
// value will do here" destination for a computation with no natural one.
// mlir-opt --one-shot-bufferize turns the tensor into a memref and the
// destination into a real memref.alloc.

func.func @scale(%in: tensor<4xf32>, %factor: f32) -> tensor<4xf32> {
  %init = tensor.empty() : tensor<4xf32>
  %out = linalg.generic {
    indexing_maps = [affine_map<(i) -> (i)>, affine_map<(i) -> (i)>],
    iterator_types = ["parallel"]
  } ins(%in : tensor<4xf32>) outs(%init : tensor<4xf32>) {
  ^bb0(%a: f32, %acc: f32):
    %m = arith.mulf %a, %factor : f32
    linalg.yield %m : f32
  } -> tensor<4xf32>
  return %out : tensor<4xf32>
}

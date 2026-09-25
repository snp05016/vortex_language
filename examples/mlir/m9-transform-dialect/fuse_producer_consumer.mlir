// Two elementwise linalg.generic ops, square then negate, in
// producer-consumer order through the tensor %sq. The schedule tiles only
// the consumer, then fuses the producer into the consumer's loop, so each
// iteration recomputes its own 4x4 tile of "square" instead of the whole
// 16x16 producer running to completion first. No intermediate the shape of
// the full tensor is ever materialized.
#map = affine_map<(d0, d1) -> (d0, d1)>
module attributes {transform.with_named_sequence} {
  func.func @square_then_negate(%in: tensor<16x16xf32>, %out: tensor<16x16xf32>)
      -> tensor<16x16xf32> {
    %init = tensor.empty() : tensor<16x16xf32>
    %sq = linalg.generic {indexing_maps = [#map, #map], iterator_types = ["parallel", "parallel"]}
        ins(%in : tensor<16x16xf32>) outs(%init : tensor<16x16xf32>) {
    ^bb0(%x: f32, %acc: f32):
      %m = arith.mulf %x, %x : f32
      linalg.yield %m : f32
    } -> tensor<16x16xf32>
    %neg = linalg.generic {indexing_maps = [#map, #map], iterator_types = ["parallel", "parallel"]}
        ins(%sq : tensor<16x16xf32>) outs(%out : tensor<16x16xf32>) {
    ^bb0(%x: f32, %acc: f32):
      %n = arith.negf %x : f32
      linalg.yield %n : f32
    } -> tensor<16x16xf32>
    return %neg : tensor<16x16xf32>
  }

  transform.named_sequence @__transform_main(%root: !transform.any_op {transform.readonly}) {
    %generics = transform.structured.match ops{["linalg.generic"]} in %root
        : (!transform.any_op) -> !transform.any_op
    %producer, %consumer = transform.split_handle %generics
        : (!transform.any_op) -> (!transform.any_op, !transform.any_op)
    %tiled_consumer, %loop = transform.structured.tile_using_forall %consumer tile_sizes [4, 4]
        : (!transform.any_op) -> (!transform.any_op, !transform.any_op)
    %fused, %new_loop = transform.structured.fuse_into_containing_op %producer into %loop
        : (!transform.any_op, !transform.any_op) -> (!transform.any_op, !transform.any_op)
    transform.yield
  }
}

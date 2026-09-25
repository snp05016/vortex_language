// Fusion as a decision: the same graph, but a schedule tiles the clamp one
// output row at a time and pulls the matrix product (and its zero fill) into
// the same loop, so each row is computed and clamped before the next row
// starts. The schedule is written in the transform dialect (M9): match the
// clamp's linalg.generic, then tile it by [batch 1, row 1, column all] and
// fuse its producers. canonicalize then removes the batch loop, which runs
// once. The floating-point operations and their order within each output
// element are unchanged; only which element is finished when changes.
func.func @matmul_clamp(%a: tensor<1x8x16xf32>, %b: tensor<1x16x4xf32>) -> tensor<1x8x4xf32> {
  %prod = tosa.matmul %a, %b : (tensor<1x8x16xf32>, tensor<1x16x4xf32>) -> tensor<1x8x4xf32>
  %clamped = tosa.clamp %prod {min_int = 0 : i64, max_int = 0 : i64, min_fp = 0.0 : f32, max_fp = 3.4028235e+38 : f32} : (tensor<1x8x4xf32>) -> tensor<1x8x4xf32>
  return %clamped : tensor<1x8x4xf32>
}

module attributes {transform.with_named_sequence} {
  transform.named_sequence @__transform_main(%root: !transform.any_op {transform.readonly}) {
    %clamp = transform.structured.match ops{["linalg.generic"]} in %root : (!transform.any_op) -> !transform.any_op
    %fused, %loops:2 = transform.structured.fuse %clamp [1, 1, 0] : (!transform.any_op) -> (!transform.any_op, !transform.any_op, !transform.any_op)
    transform.yield
  }
}

// One step down: each tosa operation becomes a linalg structured operation,
// still on tensors, still with no loop order chosen. tosa.matmul becomes the
// named op linalg.batch_matmul, preceded by a linalg.fill that supplies the
// zeros it accumulates into; tosa.clamp has no named counterpart and becomes
// a linalg.generic whose body is a min followed by a max.
// Two passes, in this order: the named-op lowering, then the rest.
func.func @matmul_clamp(%a: tensor<1x8x16xf32>, %b: tensor<1x16x4xf32>) -> tensor<1x8x4xf32> {
  %prod = tosa.matmul %a, %b : (tensor<1x8x16xf32>, tensor<1x16x4xf32>) -> tensor<1x8x4xf32>
  %clamped = tosa.clamp %prod {min_int = 0 : i64, max_int = 0 : i64, min_fp = 0.0 : f32, max_fp = 3.4028235e+38 : f32} : (tensor<1x8x4xf32>) -> tensor<1x8x4xf32>
  return %clamped : tensor<1x8x4xf32>
}

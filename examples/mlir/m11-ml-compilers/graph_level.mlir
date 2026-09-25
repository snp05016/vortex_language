// The graph level: one node per whole-tensor operation, no loops, no
// memory. This is one linear layer without a bias, followed by a ReLU:
// a matrix product, then a clamp of every element to [0, largest f32].
// tosa (Tensor Operator Set Architecture) ships inside MLIR, so it stands in
// here for StableHLO, which XLA and IREE read and which is not part of the
// MLIR 18 install. With no pass given, mlir-opt only parses, verifies and
// prints the file back, with the values renumbered.
// The clamp's four attributes are MLIR 18's spelling; newer tosa renames them.
func.func @matmul_clamp(%a: tensor<1x8x16xf32>, %b: tensor<1x16x4xf32>) -> tensor<1x8x4xf32> {
  %prod = tosa.matmul %a, %b : (tensor<1x8x16xf32>, tensor<1x16x4xf32>) -> tensor<1x8x4xf32>
  %clamped = tosa.clamp %prod {min_int = 0 : i64, max_int = 0 : i64, min_fp = 0.0 : f32, max_fp = 3.4028235e+38 : f32} : (tensor<1x8x4xf32>) -> tensor<1x8x4xf32>
  return %clamped : tensor<1x8x4xf32>
}

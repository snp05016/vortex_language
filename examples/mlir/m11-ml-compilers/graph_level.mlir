// The shape every one of these compilers starts from: a whole-tensor graph
// with no loops and no memory, matmul followed by a clamp (the shape of a
// fused bias-free matmul+ReLU epilogue). tosa is a real, versioned opset
// (Tensor Operator Set Architecture) built into MLIR; StableHLO plays the
// same role for XLA and IREE. Checked as is: mlir-opt only verifies and
// prints it back, unchanged in meaning, values renumbered.
func.func @matmul_clamp(%a: tensor<1x8x16xf32>, %b: tensor<1x16x4xf32>) -> tensor<1x8x4xf32> {
  %prod = tosa.matmul %a, %b : (tensor<1x8x16xf32>, tensor<1x16x4xf32>) -> tensor<1x8x4xf32>
  %clamped = tosa.clamp %prod {min_int = 0 : i64, max_int = 0 : i64, min_fp = 0.0 : f32, max_fp = 3.4028235e+38 : f32} : (tensor<1x8x4xf32>) -> tensor<1x8x4xf32>
  return %clamped : tensor<1x8x4xf32>
}

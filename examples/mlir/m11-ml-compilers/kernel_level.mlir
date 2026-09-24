// The same graph as graph_level.mlir, taken the rest of the way: tosa's
// whole-tensor ops become linalg's structured ops (still tensors, no loops),
// then one-shot bufferization turns each tensor into a memref, then
// convert-linalg-to-loops turns each structured op into explicit scf.for
// loops over memory. This is the trip XLA (through IREE or via HLO fusion),
// TVM (Relay to Tensor Expression to TIR) and Triton (Triton-IR to loops) each make in
// their own dialects; Vortex's own compiler makes a version of it too, by
// hand, one stage at a time (see the stage 6 through 10 guide).
func.func @matmul_clamp(%a: tensor<1x8x16xf32>, %b: tensor<1x16x4xf32>) -> tensor<1x8x4xf32> {
  %prod = tosa.matmul %a, %b : (tensor<1x8x16xf32>, tensor<1x16x4xf32>) -> tensor<1x8x4xf32>
  %clamped = tosa.clamp %prod {min_int = 0 : i64, max_int = 0 : i64, min_fp = 0.0 : f32, max_fp = 3.4028235e+38 : f32} : (tensor<1x8x4xf32>) -> tensor<1x8x4xf32>
  return %clamped : tensor<1x8x4xf32>
}

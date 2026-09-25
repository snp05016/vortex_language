// The same graph taken down to loops over memory: tosa to linalg, then
// one-shot bufferization turns every tensor into a memref (a buffer with an
// address), then convert-linalg-to-loops writes each structured operation as
// its own scf.for nest of loads and stores. Nothing here fuses anything, so
// the product is written to a whole 1x8x4 buffer and read back by a separate
// clamp nest: three loop nests, two buffers.
func.func @matmul_clamp(%a: tensor<1x8x16xf32>, %b: tensor<1x16x4xf32>) -> tensor<1x8x4xf32> {
  %prod = tosa.matmul %a, %b : (tensor<1x8x16xf32>, tensor<1x16x4xf32>) -> tensor<1x8x4xf32>
  %clamped = tosa.clamp %prod {min_int = 0 : i64, max_int = 0 : i64, min_fp = 0.0 : f32, max_fp = 3.4028235e+38 : f32} : (tensor<1x8x4xf32>) -> tensor<1x8x4xf32>
  return %clamped : tensor<1x8x4xf32>
}

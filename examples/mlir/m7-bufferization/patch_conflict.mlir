// Write a 4-element patch into the front of t, then return both the patched
// tensor and t itself. t is still needed after the write, so the write may
// not reuse t's buffer: One-Shot Bufferize copies t into a new buffer and
// patches the copy, leaving the caller's buffer unchanged.

func.func @patch_then_reread(%t: tensor<8xf32>, %patch: tensor<4xf32>)
    -> (tensor<8xf32>, tensor<8xf32>) {
  %patched = tensor.insert_slice %patch into %t[0] [4] [1]
      : tensor<4xf32> into tensor<8xf32>
  return %patched, %t : tensor<8xf32>, tensor<8xf32>
}

// Two results share one tensor: patched is the value after writing a 4-wide
// slice into t, and t is the original, unpatched value. Both are read after
// the write, so no memref can serve both: One-Shot Bufferize must copy t's
// data before patching one copy, instead of overwriting t's own buffer.

func.func @patch_then_reread(%t: tensor<8xf32>, %patch: tensor<4xf32>)
    -> (tensor<8xf32>, tensor<8xf32>) {
  %patched = tensor.insert_slice %patch into %t[0] [4] [1]
      : tensor<4xf32> into tensor<8xf32>
  return %patched, %t : tensor<8xf32>, tensor<8xf32>
}

// fetch_add with one more read, placed after the write. The analysis-only
// mode does not bufferize; it annotates each tensor operand with its
// in-place decision and labels the three parts of the read-after-write
// conflict that forces a copy: the definition, the write, the later read.

func.func @add_then_peek(%t: tensor<8xf32>, %i: index, %d: f32)
    -> (tensor<8xf32>, f32) {
  %old = tensor.extract %t[%i] : tensor<8xf32>
  %new = arith.addf %old, %d : f32
  %u = tensor.insert %new into %t[%i] : tensor<8xf32>
  %again = tensor.extract %t[%i] : tensor<8xf32>
  return %u, %again : tensor<8xf32>, f32
}

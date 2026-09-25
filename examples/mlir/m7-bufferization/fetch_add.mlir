// Read an element, then write a new value into the same tensor. The read
// happens before the write, so nothing needs the old contents afterwards:
// One-Shot Bufferize lets the write reuse the argument's buffer, and the
// function becomes a load and a store on the caller's memory.

func.func @fetch_add(%t: tensor<8xf32>, %i: index, %d: f32)
    -> (tensor<8xf32>, f32) {
  %old = tensor.extract %t[%i] : tensor<8xf32>
  %new = arith.addf %old, %d : f32
  %u = tensor.insert %new into %t[%i] : tensor<8xf32>
  return %u, %old : tensor<8xf32>, f32
}

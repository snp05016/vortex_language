// A callee that writes into its tensor argument, and a caller that still
// reads that argument after the call. Function arguments bufferize as
// writable, so @bump stores into its parameter in place; the conflict is in
// the caller, which therefore copies before the call.

func.func private @bump(%t: tensor<4xf32>, %i: index, %v: f32) -> tensor<4xf32> {
  %u = tensor.insert %v into %t[%i] : tensor<4xf32>
  return %u : tensor<4xf32>
}

func.func @caller(%t: tensor<4xf32>, %v: f32) -> (tensor<4xf32>, f32) {
  %c0 = arith.constant 0 : index
  %u = call @bump(%t, %c0, %v) : (tensor<4xf32>, index, f32) -> tensor<4xf32>
  %before = tensor.extract %t[%c0] : tensor<4xf32>
  return %u, %before : tensor<4xf32>, f32
}

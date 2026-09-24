// bufferization.to_tensor and bufferization.to_memref cross the boundary by
// hand, for code that mixes bufferized and un-bufferized functions instead
// of letting One-Shot Bufferize convert a whole module at once. In MLIR
// 18.1.8 these two ops are named to_tensor and to_memref; a newer MLIR
// renames the second one to_buffer.

func.func @first_element(%m: memref<4xf32>) -> f32 {
  %t = bufferization.to_tensor %m : memref<4xf32>
  %c0 = arith.constant 0 : index
  %v = tensor.extract %t[%c0] : tensor<4xf32>
  return %v : f32
}

func.func @as_memref(%t: tensor<4xf32>) -> memref<4xf32> {
  %m = bufferization.to_memref %t : memref<4xf32>
  return %m : memref<4xf32>
}

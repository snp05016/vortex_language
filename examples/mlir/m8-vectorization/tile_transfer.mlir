// Move a 2x2 tile between a memref and a vector register value.
// transfer_read/transfer_write are the vector dialect's only ops that touch
// memory; every other vector op, including vector.contract, works purely on
// values already sitting in registers.
func.func @load_add_store(%src: memref<4x4xf32>, %dst: memref<4x4xf32>) {
  %c1 = arith.constant 1 : index
  %pad = arith.constant 0.0 : f32
  %tile = vector.transfer_read %src[%c1, %c1], %pad
      {in_bounds = [true, true]} : memref<4x4xf32>, vector<2x2xf32>
  %doubled = arith.addf %tile, %tile : vector<2x2xf32>
  vector.transfer_write %doubled, %dst[%c1, %c1]
      {in_bounds = [true, true]} : vector<2x2xf32>, memref<4x4xf32>
  return
}

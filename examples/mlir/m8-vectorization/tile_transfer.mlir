// Move a 2x2 tile from one memref into a vector value, double it, and write
// it to another memref. The two transfer ops are the only places this
// function touches memory; arith.addf works on the vector value itself.
// in_bounds = [true, true] promises that no lane of the tile falls outside
// the 4x4 memref, so no lane will ever need the padding value.
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

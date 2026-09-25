// A 2x2 read at the bottom-right corner of a 3x3 memref: only element
// [2, 2] exists. Without in_bounds, every lane past the edge receives %pad.
// Lowering the transfer to 1-D rows shows what padding costs: row 3 is out
// of bounds for every lane, so it folds to a constant of %pad, while row 2
// stays a 1-D read that may run past the edge and keeps its padding.
func.func @read_partial_tile(%src: memref<3x3xf32>) -> vector<2x2xf32> {
  %c2 = arith.constant 2 : index
  %pad = arith.constant -1.0 : f32
  %tile = vector.transfer_read %src[%c2, %c2], %pad
      : memref<3x3xf32>, vector<2x2xf32>
  return %tile : vector<2x2xf32>
}

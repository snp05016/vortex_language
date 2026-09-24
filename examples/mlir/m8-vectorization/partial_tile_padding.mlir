// A 2x2 read at the bottom-right corner of a 3x3 memref only has one real
// row and one real column left. transfer_read pads the missing lanes with
// %pad instead of needing a separate scalar remainder loop the way a
// vectorized LLVM loop needs a scalar epilogue for a trip count that does
// not divide evenly (see P10, "Trip counts and the scalar epilogue").
func.func @read_partial_tile(%src: memref<3x3xf32>) -> vector<2x2xf32> {
  %c2 = arith.constant 2 : index
  %pad = arith.constant -1.0 : f32
  %tile = vector.transfer_read %src[%c2, %c2], %pad
      : memref<3x3xf32>, vector<2x2xf32>
  return %tile : vector<2x2xf32>
}

// Blend two 8x16 images: out[row, col] = a[row, col] + b[row, col].
// scf.parallel says the 128 iterations are independent; it says nothing
// about blocks or threads. The flags in the .toml choose them:
//   1. tile both dimensions, rows by 4 and columns by 8;
//   2. map the outer (tile) loop to blocks and the inner loop to threads,
//      each loop's first dimension to x, its second to y;
//   3. turn the mapped loops into a gpu.launch, then outline it.
// Predict the grid and block sizes before reading the output. Then look
// at which of row and col ended up on thread x.
func.func @blend(%a: memref<8x16xf32>, %b: memref<8x16xf32>, %out: memref<8x16xf32>) {
  %c0 = arith.constant 0 : index
  %c1 = arith.constant 1 : index
  %c8 = arith.constant 8 : index
  %c16 = arith.constant 16 : index
  scf.parallel (%row, %col) = (%c0, %c0) to (%c8, %c16) step (%c1, %c1) {
    %x = memref.load %a[%row, %col] : memref<8x16xf32>
    %y = memref.load %b[%row, %col] : memref<8x16xf32>
    %s = arith.addf %x, %y : f32
    memref.store %s, %out[%row, %col] : memref<8x16xf32>
    scf.reduce
  }
  return
}

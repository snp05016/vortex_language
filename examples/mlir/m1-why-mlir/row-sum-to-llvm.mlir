// The affine loop nest taken all the way down to the llvm dialect.
//
// Six passes run in order: --lower-affine turns affine loops into scf loops
// with plain memref loads; --convert-scf-to-cf replaces structured loops with
// blocks and branches; the next three convert arith, memref and func
// operations to the llvm dialect; --reconcile-unrealized-casts removes the
// temporary casts the conversions left between them.
//
// In the output, each memref argument has become several scalar arguments
// (pointers, offset, sizes, strides), and the element m[i, j] is found at
// i * 4 + j elements past the aligned pointer.
func.func @row_sum(%m: memref<3x4xf32>, %sums: memref<3xf32>) {
  affine.for %i = 0 to 3 {
    affine.for %j = 0 to 4 {
      %x = affine.load %m[%i, %j] : memref<3x4xf32>
      %acc = affine.load %sums[%i] : memref<3xf32>
      %next = arith.addf %acc, %x : f32
      affine.store %next, %sums[%i] : memref<3xf32>
    }
  }
  return
}

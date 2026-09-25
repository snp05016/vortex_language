// A host function that brightens 256 pixel values: add a bias, cap at 1.0.
// The kernel is written inline, as the region of a gpu.launch: 4 blocks of
// 64 threads, one thread per value.
//
// The region reads four values defined outside it: %pixels, %bias, %c64
// and %one. A gpu.launch region may do that, because gpu.launch is not
// isolated from above. A gpu.func may not, so --gpu-kernel-outlining must
// turn every captured value into a kernel argument. Count them in the
// output's args(...) list.
func.func @brighten(%pixels: memref<256xf32>, %bias: f32) {
  %c1 = arith.constant 1 : index
  %c4 = arith.constant 4 : index
  %c64 = arith.constant 64 : index
  %one = arith.constant 1.0 : f32
  gpu.launch blocks(%bx, %by, %bz) in (%gx = %c4, %gy = %c1, %gz = %c1)
             threads(%tx, %ty, %tz) in (%sx = %c64, %sy = %c1, %sz = %c1) {
    // Which value this thread owns: block index * block size + thread index.
    %base = arith.muli %bx, %c64 : index
    %i = arith.addi %base, %tx : index
    %v = memref.load %pixels[%i] : memref<256xf32>
    %lit = arith.addf %v, %bias : f32
    %capped = arith.minimumf %lit, %one : f32
    memref.store %capped, %pixels[%i] : memref<256xf32>
    gpu.terminator
  }
  return
}

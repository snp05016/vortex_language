// A reduction over two 1-D arrays, dot(a, b) = sum_i a[i] * b[i], the same
// shape of loop the stage 10 kernel writes for a single row and column but
// over one dimension only. scf, memref, arith and func all appear, so a
// full conversion needs a pattern set for each: scf.for becomes a
// branch and a loop-carrying block argument, memref.load becomes a
// pointer computation, and the two f32 array parameters become the five
// fields of a memref descriptor apiece: allocated pointer, aligned
// pointer, offset, one size, one stride.

func.func @dot(%a: memref<8xf32>, %b: memref<8xf32>) -> f32 {
  %c0 = arith.constant 0 : index
  %c1 = arith.constant 1 : index
  %c8 = arith.constant 8 : index
  %zero = arith.constant 0.0 : f32
  %result = scf.for %i = %c0 to %c8 step %c1 iter_args(%acc = %zero) -> (f32) {
    %x = memref.load %a[%i] : memref<8xf32>
    %y = memref.load %b[%i] : memref<8xf32>
    %p = arith.mulf %x, %y : f32
    %next = arith.addf %acc, %p : f32
    scf.yield %next : f32
  }
  return %result : f32
}

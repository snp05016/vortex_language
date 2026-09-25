// A reduction over two 1-D arrays, dot(a, b) = sum of a[i] * b[i], the loop
// the stage 10 kernel runs for one output element, over one dimension only.
// scf, cf, memref, arith and func all appear (cf only after the first pass
// creates it), so the pipeline runs one conversion pass per dialect:
// scf.for becomes branches and a block argument that carries the running
// sum, memref.load becomes address arithmetic and a load, and each
// memref<8xf32> parameter becomes the five fields of a memref descriptor
// (allocated pointer, aligned pointer, offset, one size, one stride).
// The last pass removes the casts that bridged the passes in between.

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

// Sum a fixed-length row from a runtime-chosen start to its end.
//
// %start is a function argument: its value is not known until the function
// runs, but it is fixed for the whole loop below it, because nothing inside
// the loop redefines it. That is what affine calls a symbol. The running
// total is carried between iterations as %acc, not through memory: each
// iteration reads the previous total as an operand and yields the next one.

func.func @suffix_sum(%row: memref<8xf32>, %start: index) -> f32 {
  %zero = arith.constant 0.0 : f32
  %sum = affine.for %i = %start to 8 iter_args(%acc = %zero) -> f32 {
    %v = affine.load %row[%i] : memref<8xf32>
    %next = arith.addf %acc, %v : f32
    affine.yield %next : f32
  }
  return %sum : f32
}

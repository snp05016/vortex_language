// The same function as dot_full, converted with one pattern set only:
// arith to llvm. scf, memref and func stay legal, so the driver leaves
// their operations alone; it does not fail just because llvm and scf now
// sit in the same function.
//
// arith.constant 0 : index becomes an i64 llvm.mlir.constant, because the
// TypeConverter maps MLIR's index type to a fixed-width integer. But
// scf.for still declares its loop variable as index, so the converted
// constant cannot feed it directly: the driver inserts an
// unrealized_conversion_cast, a materialization that stands in for the
// missing i64-to-index pattern until a later pass removes it.

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

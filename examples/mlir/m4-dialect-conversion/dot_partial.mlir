// The same function as dot_full, converted by one pass only:
// convert-arith-to-llvm. Its conversion target knows two things, that the
// llvm dialect is legal and that the bridging cast is legal. Every other
// operation, scf.for, memref.load and func.func included, is unknown to it,
// and a partial conversion leaves unknown operations alone.
//
// arith.constant 0 : index becomes an i64 llvm.mlir.constant, because the
// type converter maps index to a 64-bit integer. But scf.for, which nobody
// converted, still wants index operands. So the driver inserts an
// unrealized_conversion_cast from i64 back to index for each bound: a
// materialization that keeps the module well typed until a later pass
// converts the loop and the cast can go.

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

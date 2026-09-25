// Horner's rule: evaluate c0*x^3 + c1*x^2 + c2*x + c3 as
// ((c0*x + c1)*x + c2)*x + c3, one coefficient per loop iteration.
//
// The function is written in custom syntax, the form people usually write.
// The example runs with --mlir-print-op-generic, so the output is the same
// function in generic syntax, where every operation has one shape: quoted
// name, operands, properties, regions, then its type. Match each token here
// to a field there.
//
// The multiply and the add stay two operations. Fusing them into one fused
// multiply-add rounds once instead of twice and can change the result, so a
// strict compiler must not do it silently. The generic output shows where
// that permission would be recorded.

func.func @horner(%coeffs: memref<4xf32>, %x: f32) -> f32 {
  %c0 = arith.constant 0 : index
  %c1 = arith.constant 1 : index
  %c4 = arith.constant 4 : index
  // Start from c0, not from 0.0: 0.0 * x is NaN when x is infinite.
  %first = memref.load %coeffs[%c0] : memref<4xf32>
  // %acc carries the running value from one iteration to the next.
  %result = scf.for %i = %c1 to %c4 step %c1 iter_args(%acc = %first) -> (f32) {
    %c = memref.load %coeffs[%i] : memref<4xf32>
    %scaled = arith.mulf %acc, %x : f32
    %next = arith.addf %scaled, %c : f32
    scf.yield %next : f32
  }
  return %result : f32
}

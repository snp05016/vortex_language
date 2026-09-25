// The ends function again, with finalize-memref-to-llvm left out of the
// pipeline. Every pass that runs still succeeds on its own: each is a
// partial conversion, and memref.load is merely unknown to them.
// convert-func-to-llvm rewrites the signature anyway, so the memref
// parameter becomes a descriptor, and a cast turns it back into a memref
// for the loads that nobody converted.
//
// The failure comes at the end. reconcile-unrealized-casts is the one pass
// whose target marks the cast illegal, and this cast cannot be removed: its
// user, memref.load, still wants a memref. So the error names the cast, at
// the parameter's location, not the operation that was never converted.
//
// --verify-diagnostics turns the expected-error comment into an assertion,
// so this file passes only while mlir-opt reports exactly that error.

// expected-error@+1 {{failed to legalize operation 'builtin.unrealized_conversion_cast' that was explicitly marked illegal}}
func.func @ends(%v: memref<4xf32>) -> f32 {
  %c0 = arith.constant 0 : index
  %c3 = arith.constant 3 : index
  %first = memref.load %v[%c0] : memref<4xf32>
  %last = memref.load %v[%c3] : memref<4xf32>
  %sum = arith.addf %first, %last : f32
  return %sum : f32
}

// The same kind of memref parameter, lowered with the "bare pointer" calling
// convention instead of the default one. The function adds the first and the
// last element of a fixed-size array; no loop, so only arith, memref and func
// need converting.
//
// With use-bare-ptr-memref-call-conv=1, convert-func-to-llvm passes each
// memref argument as one pointer to its aligned data. That is only possible
// because memref<4xf32> is fully static with the default layout: the callee
// can rebuild the rest of the descriptor from constants, which is what the
// first lines of the output do. Inside the function the descriptor is still
// the currency; only the signature changed.

func.func @ends(%v: memref<4xf32>) -> f32 {
  %c0 = arith.constant 0 : index
  %c3 = arith.constant 3 : index
  %first = memref.load %v[%c0] : memref<4xf32>
  %last = memref.load %v[%c3] : memref<4xf32>
  %sum = arith.addf %first, %last : f32
  return %sum : f32
}

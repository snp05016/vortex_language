// A function with no memref, no loop and no control flow: scalar arithmetic
// and a function boundary. Two dialects appear, arith and func, and each has
// a pass that rewrites its operations into the llvm dialect.
//
// No value changes type on the way: f32 is a legal llvm type as it stands,
// so no pass has to bridge a converted value to an unconverted user, and the
// output contains no cast. The last pass, which removes such casts, finds
// nothing to do here.
//
// clamp01 forces x into [0, 1] with two comparisons and two selects,
// branch-free: both arms of each choice are always computed.

func.func @clamp01(%x: f32) -> f32 {
  %lo = arith.constant 0.0 : f32
  %hi = arith.constant 1.0 : f32
  %below = arith.cmpf olt, %x, %lo : f32
  %raised = arith.select %below, %lo, %x : f32
  %above = arith.cmpf ogt, %raised, %hi : f32
  %result = arith.select %above, %hi, %raised : f32
  return %result : f32
}

// A function with no memref, no loop and no control flow: only scalar
// arithmetic and a function boundary. Two dialects appear, arith and func,
// and both have a complete lowering to llvm. Converting it needs no
// bridging step at all, because nothing outside arith and func is present
// to leave unconverted.
//
// clamp01 forces x into [0, 1] with two comparisons and two selects,
// entirely branch-free: both arms of each choice are always computed.

func.func @clamp01(%x: f32) -> f32 {
  %lo = arith.constant 0.0 : f32
  %hi = arith.constant 1.0 : f32
  %below = arith.cmpf olt, %x, %lo : f32
  %raised = arith.select %below, %lo, %x : f32
  %above = arith.cmpf ogt, %raised, %hi : f32
  %result = arith.select %above, %hi, %raised : f32
  return %result : f32
}

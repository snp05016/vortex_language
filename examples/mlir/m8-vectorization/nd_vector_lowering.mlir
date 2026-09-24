// A 2x2 "virtual" vector has no hardware register to match it: NEON, like
// every mainstream SIMD extension, only has 1-D registers. Lowering this
// function to the LLVM dialect turns vector<2x2xf32> into an array of two
// 1-D vector<2xf32> values, one hardware register per row, instead of one
// flat 4-lane vector.
func.func @identity(%a: vector<2x2xf32>) -> vector<2x2xf32> {
  return %a : vector<2x2xf32>
}

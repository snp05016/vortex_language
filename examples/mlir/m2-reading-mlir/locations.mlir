// Every operation carries a location, even when mlir-opt does not print it.
//
// A front end attaches positions in its own source file. Here a made-up
// calculator language has this as line 2 of shop.calc:
//
//   let total = price * (1.0 + rate)
//
// The three operations built from that line point at column 22 (1.0),
// 26 (+) and 19 (*). Operations written without loc(...) get their position
// in this .mlir file instead. --mlir-print-debuginfo prints the locations,
// and --mlir-print-local-scope prints each one in place rather than as an
// alias defined at the end of the output.

func.func @total_price(%price: f32, %rate: f32) -> f32 {
  %one = arith.constant 1.0 : f32 loc("shop.calc":2:22)
  %factor = arith.addf %one, %rate : f32 loc("shop.calc":2:26)
  %total = arith.mulf %price, %factor : f32 loc("shop.calc":2:19)
  return %total : f32
}

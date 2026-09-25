// One run of the greedy driver, four kinds of rewrite. Canonicalization
// moves the constant in %a to the right-hand side, a table-driven pattern
// turns (x + 2) + 3 into x + 5, a fold turns %b - %b into 0, another fold
// turns %b + 0 into %b, and every operation left without users is erased.
// Each rewrite creates the shape the next one matches, so no single pass
// over the operations in order could finish the job; the worklist can.
func.func @chain(%x: i32) -> i32 {
  %c2 = arith.constant 2 : i32
  %a = arith.addi %c2, %x : i32
  %c3 = arith.constant 3 : i32
  %b = arith.addi %a, %c3 : i32
  %z = arith.subi %b, %b : i32
  %r = arith.addi %b, %z : i32
  return %r : i32
}

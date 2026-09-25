// %a and %b compute the same value, x + 5, but reach it two ways: %a adds
// a sum of two constants, %b names 5 directly. CSE merges operations that
// are already identical (same name, operands and attributes), so on
// its own it sees two different additions. Canonicalize folds 2 + 3 into
// the constant 5 it already has, which makes %a identical to %b; only then
// can CSE, running second, merge them. Swap the two flags and both stay.
func.func @h(%x: i32) -> i32 {
  %c2 = arith.constant 2 : i32
  %c3 = arith.constant 3 : i32
  %sum = arith.addi %c2, %c3 : i32
  %a = arith.addi %x, %sum : i32
  %c5 = arith.constant 5 : i32
  %b = arith.addi %x, %c5 : i32
  %r = arith.addi %a, %b : i32
  return %r : i32
}

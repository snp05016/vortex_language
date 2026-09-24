// %a and %b compute the same value (x + 5) but arrive at it two different
// ways: one folds two constants first, the other names 5 directly. CSE
// matches operations that are already textually identical, so run alone it
// cannot see that %a and %b agree. Canonicalize's constant folding rewrites
// both to "arith.addi %x, 5", and only then does CSE find one addition to
// remove. Run these two flags in the other order and %a and %b stay separate.
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

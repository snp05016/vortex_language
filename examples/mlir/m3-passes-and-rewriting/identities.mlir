// Two canonicalization patterns at once: an algebraic identity (x + 0 -> x)
// erases the use that kept %unused's multiply alive, and the greedy driver
// then deletes that now-dead, side-effect-free operation in the same pass.
// Neither identity depends on the other; canonicalize applies whichever
// pattern matches next until none does.
func.func @cleanup(%x: i32) -> i32 {
  %c0 = arith.constant 0 : i32
  %c1 = arith.constant 1 : i32
  %unused = arith.muli %x, %c1 : i32
  %kept = arith.addi %x, %c0 : i32
  return %kept : i32
}

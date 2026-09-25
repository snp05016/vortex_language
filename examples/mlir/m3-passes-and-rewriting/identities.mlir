// Two cleanups that --canonicalize finds without being told about either.
// arith.addi's fold hook knows that adding zero returns the other operand,
// so every use of %kept becomes a use of %x. %unused has no users and no
// side effects, so the driver erases it: that rule belongs to the driver,
// not to any dialect. The constants lose their last users and go too.
func.func @cleanup(%x: i32) -> i32 {
  %c0 = arith.constant 0 : i32
  %unused = arith.muli %x, %x : i32
  %kept = arith.addi %x, %c0 : i32
  return %kept : i32
}

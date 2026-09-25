// An identity that holds for every i32 may fail for f32. Rounding to
// nearest, -0.0 + 0.0 is +0.0, so x + 0.0 is not x when x is -0.0, while
// x + (-0.0) gives back every x. x * 1.0 is exact. x * 0.0 is NaN for an
// infinite x and -0.0 for a negative one, and x - x is NaN for an infinite
// x. The folders in arith apply only the rewrites that hold for every input.
func.func @float_identities(%x: f32) -> (f32, f32, f32, f32, f32) {
  %zero = arith.constant 0.0 : f32
  %minus_zero = arith.constant -0.0 : f32
  %one = arith.constant 1.0 : f32
  %add_zero = arith.addf %x, %zero : f32
  %add_minus_zero = arith.addf %x, %minus_zero : f32
  %times_one = arith.mulf %x, %one : f32
  %times_zero = arith.mulf %x, %zero : f32
  %minus_self = arith.subf %x, %x : f32
  return %add_zero, %add_minus_zero, %times_one, %times_zero, %minus_self
      : f32, f32, f32, f32, f32
}

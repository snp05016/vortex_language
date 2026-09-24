// A pass pipeline written as text must nest exactly like the IR: an
// operation name, then the passes that run on that operation, in
// parentheses. "canonicalize" is a pass, not an operation name, so
// wrapping --cse inside it does not mean "run cse after canonicalize": it
// means "run cse inside every operation named canonicalize". No operation
// in this module is named that, so cse runs zero times and the duplicate
// multiply below survives untouched. The pipeline that means what it looks
// like it means is builtin.module(func.func(canonicalize,cse)).
func.func @g(%x: i32, %y: i32) -> i32 {
  %a = arith.muli %x, %y : i32
  %b = arith.muli %x, %y : i32
  %r = arith.addi %a, %b : i32
  return %r : i32
}

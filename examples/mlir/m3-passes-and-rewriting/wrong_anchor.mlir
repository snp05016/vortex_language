// In a textual pipeline, a name followed by parentheses is an anchor: an
// operation name, with the passes to run on each such operation inside.
// canonicalize(cse) therefore means "run cse on every operation named
// canonicalize". MLIR 18.1.8 accepts the string, finds no such operation,
// and runs cse nowhere, so the duplicate multiply survives. The pipeline
// that says what was meant is builtin.module(func.func(canonicalize, cse)).
func.func @g(%x: i32, %y: i32) -> i32 {
  %a = arith.muli %x, %y : i32
  %b = arith.muli %x, %y : i32
  %r = arith.addi %a, %b : i32
  return %r : i32
}

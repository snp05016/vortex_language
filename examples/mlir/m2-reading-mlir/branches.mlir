// Branches and block arguments: return %x, raised to %floor when it is below.
//
// Where two paths meet, MLIR passes a value along each branch as a block
// argument, instead of a phi at the top of the join block. The output is the
// generic form: each branch lists its target blocks in [...], and cf.cond_br
// keeps all its operands in one list, with a property that says how the list
// splits between the condition and the two targets.
//
// @note_clamped is only declared here. A declaration is a function operation
// with an empty region; the call refers to it by symbol name, not by value.

func.func private @note_clamped(f32)

func.func @clamp_below(%x: f32, %floor: f32) -> f32 {
  %below = arith.cmpf olt, %x, %floor : f32
  cf.cond_br %below, ^raise, ^done(%x : f32)
^raise:
  func.call @note_clamped(%x) : (f32) -> ()
  cf.br ^done(%floor : f32)
^done(%result: f32):
  return %result : f32
}

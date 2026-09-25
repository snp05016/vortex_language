; Poison spreads through almost every instruction. `select` is the notable
; exception: it is poison only when its condition or the value it picks is.
;
;   @and_false     `and i1 %p, false` is poison when %p is poison, but a
;                   poison result may be replaced by any value, so the
;                   optimizer may still return false. The add goes too.
;   @shift_select  `select i1 %small, i1 %set, i1 false` never lets the
;                   poison from an over-wide shift escape: when %n >= 32,
;                   %small is false and the select picks the constant.
;   @shift_and     the same test written with `and`. When %n >= 32 the
;                   result is poison even though %small is false, so this
;                   is a different function. InstCombine keeps the select
;                   in @shift_select instead of turning it into this `and`.
;
; Run: opt -S -passes=instcombine poison_select.ll
;
; Follows: LLVM Language Reference, sections "Poison Values", "'shl'
; Instruction" and "'select' Instruction"; LLVM IR Undefined Behavior Manual,
; section "Propagation of Poison Through Select".

define i1 @and_false(i32 %x) {
  %y = add nsw i32 %x, 1
  %wrapped = icmp slt i32 %y, %x
  %r = and i1 %wrapped, false
  ret i1 %r
}

define i1 @shift_select(i32 %n, i32 %mask) {
  %small = icmp ult i32 %n, 32
  %bit = shl i32 1, %n
  %set = icmp ne i32 %bit, %mask
  %r = select i1 %small, i1 %set, i1 false
  ret i1 %r
}

define i1 @shift_and(i32 %n, i32 %mask) {
  %small = icmp ult i32 %n, 32
  %bit = shl i32 1, %n
  %set = icmp ne i32 %bit, %mask
  %r = and i1 %small, %set
  ret i1 %r
}

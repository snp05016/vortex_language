; freeze turns a poison or undef value into one arbitrary but fixed value,
; and leaves an already-defined value alone.
;
;   @raw_poison            freeze of a literal poison folds to one concrete
;                           constant, the simplest one the optimizer can pick.
;   @raw_undef              the same, for undef.
;   @freeze_of_a_parameter  %x is an ordinary argument, never poison, so
;                           freezing it changes nothing: the instruction
;                           survives untouched.
;   @freeze_of_an_overflow  the source freezes the result of a possibly-
;                           overflowing add. InstCombine instead freezes the
;                           operand and drops nsw from the add: the frozen
;                           input makes the addition well-defined for every
;                           choice, so the no-wrap claim is no longer needed.
;
; Run: opt -S -passes=instcombine freeze.ll
;
; Follows: LLVM Language Reference, section "'freeze' Instruction".

define i32 @raw_poison() {
  %f = freeze i32 poison
  ret i32 %f
}

define i32 @raw_undef() {
  %f = freeze i32 undef
  ret i32 %f
}

define i32 @freeze_of_a_parameter(i32 %x) {
  %f = freeze i32 %x
  ret i32 %f
}

define i32 @freeze_of_an_overflow(i32 %x) {
  %y = add nsw i32 %x, 1
  %f = freeze i32 %y
  ret i32 %f
}

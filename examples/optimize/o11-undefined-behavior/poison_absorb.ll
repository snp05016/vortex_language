; Poison propagates through most instructions, but not through every one.
; Each function here computes a value that may be poison (the add overflows
; when %x is INT_MAX), then combines it with something that already decides
; the answer on its own.
;
;   @and_false       false alone decides `and`: poison or not, the result
;                     is false, and the overflowing add can be dropped.
;   @or_true         true alone decides `or`, the same way, in the other
;                     direction.
;   @select_unused   the overflowing add still runs (select is not lazy),
;                     but its poisoned result only escapes when %c picks it;
;                     when %c picks %x, the caller never sees the poison.
;
; Run: opt -S -passes=instcombine poison_absorb.ll
;
; Follows: LLVM Language Reference, sections "Poison Values" and "'select'
; Instruction".

define i1 @and_false(i32 %x) {
  %y = add nsw i32 %x, 1
  %ovf = icmp slt i32 %y, %x
  %r = and i1 %ovf, false
  ret i1 %r
}

define i1 @or_true(i32 %x) {
  %y = add nsw i32 %x, 1
  %ovf = icmp slt i32 %y, %x
  %r = or i1 %ovf, true
  ret i1 %r
}

define i32 @select_unused(i1 %c, i32 %x) {
  %y = add nsw i32 %x, 1
  %ovf_val = add nsw i32 %y, 2147483647
  %r = select i1 %c, i32 %x, i32 %ovf_val
  ret i32 %r
}

; The rewrites from rewrite_check.cpp and float_rewrites.cpp, handed to LLVM
; 18's instruction combiner. LLVM IR does not know which language it came
; from. Flags on an instruction say what the optimizer may assume about it;
; an instruction without flags keeps its strict meaning.
;
; Run: opt -S -passes=instcombine ir_flags.ll
;
;   @grows            add wraps, so x + 1 > x only becomes x != 127.
;   @grows_nsw        add nsw makes an overflow poison, which may stand for
;                     any value, so the comparison becomes true.
;   @plus_zero        x + 0.0 stays: for x = -0.0 the sum is +0.0.
;   @plus_minus_zero  x + (-0.0) equals x for every x, so it becomes x.
;   @plus_zero_nsz    nsz lets the sign of a zero change, so it becomes x.
;   @half             x / 2.0 becomes x * 0.5: 0.5 is exact, so both round
;                     the same exact value.
;   @tenth            x / 10.0 stays, because 0.1 has no exact binary form.
;   @tenth_arcp       arcp allows multiplying by a rounded reciprocal. LLVM
;                     prints that float, the one nearest 0.1, in hexadecimal
;                     as 0x3FB99999A0000000.
;
; Follows: LLVM Language Reference, sections "'add' Instruction" (nsw),
; "Poison Values" and "Fast-Math Flags".

define i1 @grows(i8 %x) {
  %y = add i8 %x, 1
  %c = icmp sgt i8 %y, %x
  ret i1 %c
}

define i1 @grows_nsw(i8 %x) {
  %y = add nsw i8 %x, 1
  %c = icmp sgt i8 %y, %x
  ret i1 %c
}

define float @plus_zero(float %x) {
  %r = fadd float %x, 0.0
  ret float %r
}

define float @plus_minus_zero(float %x) {
  %r = fadd float %x, -0.0
  ret float %r
}

define float @plus_zero_nsz(float %x) {
  %r = fadd nsz float %x, 0.0
  ret float %r
}

define float @half(float %x) {
  %r = fdiv float %x, 2.0
  ret float %r
}

define float @tenth(float %x) {
  %r = fdiv float %x, 10.0
  ret float %r
}

define float @tenth_arcp(float %x) {
  %r = fdiv arcp float %x, 10.0
  ret float %r
}

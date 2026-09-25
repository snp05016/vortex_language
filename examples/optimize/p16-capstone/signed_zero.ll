; "Start the sum at the first product instead of at 0.0" looks like the
; fold 0.0 + x = x. That fold is wrong for one input: x = -0.0, because
; +0.0 + -0.0 rounds to +0.0. Adding -0.0 instead is an exact identity.
;
; @plus_zero keeps its fadd; @plus_zero_nsz may drop it, because `nsz`
; lets the optimizer ignore the sign of a zero; @plus_neg_zero drops it
; with no flag at all.
;
; Run: opt -S -passes=instcombine signed_zero.ll
;
; Follows: LLVM Language Reference, "Fast-Math Flags" (the `nsz` flag).
; <https://llvm.org/docs/LangRef.html#fast-math-flags>

define float @plus_zero(float %x) {
  %s = fadd float 0.0, %x
  ret float %s
}

define float @plus_zero_nsz(float %x) {
  %s = fadd nsz float 0.0, %x
  ret float %s
}

define float @plus_neg_zero(float %x) {
  %s = fadd float %x, -0.0
  ret float %s
}

; Each pair is the same rewrite, tried once without a fast-math flag and once
; with the flag that permits it. InstCombine makes the rewrite only when the
; flag is present, because each one is wrong for some input:
;
;   x + 0.0 -> x          wrong at x = -0.0 (the sum is +0.0); needs nsz
;   x * 0.0 -> 0.0        wrong at NaN, infinity and negative x; nnan nsz
;   x / 10.0 -> x * 0.1   0.1 is not exact in binary; needs arcp
;   (x + 1) + 2 -> x + 3  regroups the additions; needs reassoc and nsz
;
; @madd_contract shows that `contract` changes nothing here: fusion happens
; later, in the code generator (llc -O2 turns it into one fmadd on AArch64).
;
; Run: opt -S -passes=instcombine fmf_folds.ll
;
; Follows: LLVM Language Reference, "Fast-Math Flags".

define float @add_zero(float %x) {
  %r = fadd float %x, 0.0
  ret float %r
}
define float @add_zero_nsz(float %x) {
  %r = fadd nsz float %x, 0.0
  ret float %r
}

define float @mul_zero(float %x) {
  %r = fmul float %x, 0.0
  ret float %r
}
define float @mul_zero_nnan_nsz(float %x) {
  %r = fmul nnan nsz float %x, 0.0
  ret float %r
}

define float @div_ten(float %x) {
  %r = fdiv float %x, 10.0
  ret float %r
}
define float @div_ten_arcp(float %x) {
  %r = fdiv arcp float %x, 10.0
  ret float %r
}

define float @add_consts(float %x) {
  %t = fadd float %x, 1.0
  %r = fadd float %t, 2.0
  ret float %r
}
define float @add_consts_reassoc_nsz(float %x) {
  %t = fadd reassoc nsz float %x, 1.0
  %r = fadd reassoc nsz float %t, 2.0
  ret float %r
}

define float @madd_contract(float %a, float %b, float %c) {
  %p = fmul contract float %a, %b
  %s = fadd contract float %p, %c
  ret float %s
}

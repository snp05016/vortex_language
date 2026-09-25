; The same two operations, both carrying the "contract" fast-math flag:
; permission to fuse the multiply into the add, one rounding instead of two.
; The Language Reference asks for the flag on every instruction that takes
; part in the rewrite, so it goes on the fmul as well as the fadd.
define float @madd(float %a, float %b, float %c) {
entry:
  %m = fmul contract float %a, %b
  %s = fadd contract float %m, %c
  ret float %s
}

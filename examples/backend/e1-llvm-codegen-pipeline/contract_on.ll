; The same two operations, but the add carries the "contract" fast-math
; flag: permission for the code generator to fuse a multiply that feeds an
; add into one instruction with one rounding, when the target has one.
define float @madd(float %a, float %b, float %c) {
entry:
  %m = fmul float %a, %b
  %s = fadd contract float %m, %c
  ret float %s
}

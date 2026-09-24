; A multiply and an add on their own: no fast-math flags, so nothing tells
; the code generator these two roundings may be fused into one.
define float @madd(float %a, float %b, float %c) {
entry:
  %m = fmul float %a, %b
  %s = fadd float %m, %c
  ret float %s
}

; The squared length of a 3-vector as LLVM IR, in source order: each square
; is added in as soon as it is computed. The IR fixes the data dependences
; and the grouping of the two fadds; it says nothing about which load runs
; first. llc's scheduler chooses that, from the scheduling model of the
; processor named by -mcpu. Compare:
;
;   llc -O2 -mtriple=aarch64-linux-gnu -mcpu=cortex-a53 len2.ll -o -
;   llc -O2 -mtriple=aarch64-linux-gnu -mcpu=apple-m1   len2.ll -o -
;
; No fast-math flags, so llc may not fuse or regroup the arithmetic.
;
; Follows: LLVM Language Reference Manual, "load", "fmul" and "fadd"
; (https://llvm.org/docs/LangRef.html#fadd-instruction).

define void @len2(ptr %v, ptr %out) {
entry:
  %x = load float, ptr %v, align 4
  %xx = fmul float %x, %x
  %py = getelementptr inbounds float, ptr %v, i64 1
  %y = load float, ptr %py, align 4
  %yy = fmul float %y, %y
  %s = fadd float %xx, %yy
  %pz = getelementptr inbounds float, ptr %v, i64 2
  %z = load float, ptr %pz, align 4
  %zz = fmul float %z, %z
  %t = fadd float %s, %zz
  store float %t, ptr %out, align 4
  ret void
}

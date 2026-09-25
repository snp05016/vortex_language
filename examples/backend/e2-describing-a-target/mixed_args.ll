; Five scalar arguments of four types, interleaved so that argument order and
; register numbers cannot be confused. Each value is converted to double and
; added, so every argument is used and the function cannot be folded away.
; Stop llc after instruction selection to see where the arguments arrived:
;   llc -O2 -stop-after=finalize-isel mixed_args.ll -o -
define double @mix(i32 %a, float %b, i64 %c, double %d, i32 %e) {
entry:
  %a64 = sitofp i32 %a to double
  %b64 = fpext float %b to double
  %c64 = sitofp i64 %c to double
  %e64 = sitofp i32 %e to double
  %s1 = fadd double %a64, %b64
  %s2 = fadd double %s1, %c64
  %s3 = fadd double %s2, %d
  %s4 = fadd double %s3, %e64
  ret double %s4
}

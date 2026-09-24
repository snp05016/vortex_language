; Two integers and two floats, deliberately interleaved (int, float, int,
; float) so the argument order cannot be mistaken for the register order.
; AAPCS64's CallingConv.td rule keeps one counter per register bank: this
; file exists to show llc actually placing a and c in x0/x1, b and d in
; s0/s1, exactly as that rule predicts, with no argument skipped or shared.
define i64 @combine(i64 %a, float %b, i64 %c, float %d) {
entry:
  %bi = fptosi float %b to i64
  %di = fptosi float %d to i64
  %s1 = add i64 %a, %bi
  %s2 = add i64 %s1, %c
  %s3 = add i64 %s2, %di
  ret i64 %s3
}

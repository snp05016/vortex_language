; Twelve values are loaded before a call and all twelve are read after it.
; A call may overwrite every caller-saved register, so each value must spend
; the call in a callee-saved register or in a stack slot. AAPCS64 has ten
; callee-saved general registers, x19 to x28, so at least two values have to
; go to memory. The loads are volatile only so that the optimizer cannot move
; them after the call and shrink the problem away.
;
; opt only verifies this module; the chapter shows what `llc -O2` builds.
;
; Follows: AAPCS64, "General-purpose Registers", and the LLVM Language
; Reference Manual, "'load' Instruction" (see the .toml).

declare void @tick()

define i64 @crowded(ptr %p) {
entry:
  %v0 = load volatile i64, ptr %p
  %g1 = getelementptr inbounds i64, ptr %p, i64 1
  %v1 = load volatile i64, ptr %g1
  %g2 = getelementptr inbounds i64, ptr %p, i64 2
  %v2 = load volatile i64, ptr %g2
  %g3 = getelementptr inbounds i64, ptr %p, i64 3
  %v3 = load volatile i64, ptr %g3
  %g4 = getelementptr inbounds i64, ptr %p, i64 4
  %v4 = load volatile i64, ptr %g4
  %g5 = getelementptr inbounds i64, ptr %p, i64 5
  %v5 = load volatile i64, ptr %g5
  %g6 = getelementptr inbounds i64, ptr %p, i64 6
  %v6 = load volatile i64, ptr %g6
  %g7 = getelementptr inbounds i64, ptr %p, i64 7
  %v7 = load volatile i64, ptr %g7
  %g8 = getelementptr inbounds i64, ptr %p, i64 8
  %v8 = load volatile i64, ptr %g8
  %g9 = getelementptr inbounds i64, ptr %p, i64 9
  %v9 = load volatile i64, ptr %g9
  %g10 = getelementptr inbounds i64, ptr %p, i64 10
  %v10 = load volatile i64, ptr %g10
  %g11 = getelementptr inbounds i64, ptr %p, i64 11
  %v11 = load volatile i64, ptr %g11
  call void @tick()
  %s1 = add i64 %v0, %v1
  %s2 = add i64 %s1, %v2
  %s3 = add i64 %s2, %v3
  %s4 = add i64 %s3, %v4
  %s5 = add i64 %s4, %v5
  %s6 = add i64 %s5, %v6
  %s7 = add i64 %s6, %v7
  %s8 = add i64 %s7, %v8
  %s9 = add i64 %s8, %v9
  %s10 = add i64 %s9, %v10
  %s11 = add i64 %s10, %v11
  ; 0x0123456789abcdef: no single AArch64 instruction can hold it.
  %s12 = add i64 %s11, 81985529216486895
  ret i64 %s12
}

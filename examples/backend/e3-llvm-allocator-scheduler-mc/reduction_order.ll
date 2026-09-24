; Two ways to add four numbers. chain_sum folds them left to right, so each
; fadd has a true dependency on the one before it: there is exactly one legal
; order. tree_sum groups them into two independent pairs, combined last, so
; the two partial sums have no dependency on each other. Same three fadds,
; different data-dependence graph.
;
; Follows: LLVM Language Reference Manual, "fadd" instruction
; (https://llvm.org/docs/LangRef.html#fadd-instruction).

define float @chain_sum(float %a, float %b, float %c, float %d) {
entry:
  %s1 = fadd float %a, %b
  %s2 = fadd float %s1, %c
  %s3 = fadd float %s2, %d
  ret float %s3
}

define float @tree_sum(float %a, float %b, float %c, float %d) {
entry:
  %s1 = fadd float %a, %b
  %s2 = fadd float %c, %d
  %s3 = fadd float %s1, %s2
  ret float %s3
}

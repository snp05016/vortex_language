; Three ways to add four f32 values, and what the optimizer may do to each.
;
; chain_sum folds left to right, so every fadd needs the one before it: the
; dependence graph is a single chain and has exactly one legal order.
; tree_sum adds two independent pairs and then combines them: the first two
; fadds have no edge between them, so a scheduler may issue them in either
; order without changing any result.
; tree_sum_reassoc is tree_sum with the `reassoc` and `nsz` fast-math flags.
; Those flags permit regrouping, which can change the rounded result; the
; O3 pipeline uses that permission and rebuilds the sum as a chain.
;
; The checker runs this file through `opt -passes='default<O3>'`. The
; expected output shows the first two functions untouched: without flags,
; LLVM's middle end keeps each grouping exactly as written.
;
; Follows: LLVM Language Reference Manual, "fadd" instruction and
; "Fast-Math Flags" (https://llvm.org/docs/LangRef.html#fadd-instruction,
; https://llvm.org/docs/LangRef.html#fast-math-flags).

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

define float @tree_sum_reassoc(float %a, float %b, float %c, float %d) {
entry:
  %s1 = fadd reassoc nsz float %a, %b
  %s2 = fadd reassoc nsz float %c, %d
  %s3 = fadd reassoc nsz float %s1, %s2
  ret float %s3
}

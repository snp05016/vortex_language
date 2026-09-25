; axpy over 17 elements: y[i] = a*x[i] + y[i]. Each y[i] is independent, so
; vectorizing needs no reduction and no reassociation.
;
; 17 is not a multiple of the forced vector width, 4. Run with
; -force-vector-width=4, the loop vectorizer splits the function into a
; vector loop that covers the first 16 elements four at a time, and a scalar
; "epilogue" loop that covers the one element the vector loop could not: a
; direct copy of the original loop, resumed with the index the vector loop
; reached. Both write to the same array in increasing index order, so the
; epilogue's loads and stores start exactly where the vector loop's stopped.
;
; Run: opt -S -passes=loop-vectorize -force-vector-width=4 -force-vector-interleave=1 scalar_epilogue.ll
;
; Follows: LLVM, "Auto-Vectorization in LLVM", sections "Loops with unknown
; trip count" and "Epilogue Vectorization" (a vector loop plus a scalar
; remainder loop); the function itself is original.

define void @axpy17(ptr noalias %y, ptr noalias readonly %x, float %a) {
entry:
  br label %loop

loop:
  %i = phi i64 [ 0, %entry ], [ %i.next, %loop ]
  %xp = getelementptr inbounds float, ptr %x, i64 %i
  %xv = load float, ptr %xp, align 4
  %yp = getelementptr inbounds float, ptr %y, i64 %i
  %yv = load float, ptr %yp, align 4
  %mul = fmul float %a, %xv
  %add = fadd float %mul, %yv
  store float %add, ptr %yp, align 4
  %i.next = add nuw nsw i64 %i, 1
  %cond = icmp eq i64 %i.next, 17
  br i1 %cond, label %exit, label %loop

exit:
  ret void
}

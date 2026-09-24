; y[i] = y[i] + a * x[i] for i in 0..64, vectorized four lanes at a time.
;
; A vector iteration loads x[i..i+3] and y[i..i+3] before it stores y[i..i+3].
; If y overlapped x, the scalar loop's store to y[i] could change x[i+1],
; which the vector loop has already loaded. So the vectorizer must be sure
; that the 256 bytes of y and the 256 bytes of x do not overlap.
;
; @axpy_may_alias has two plain pointers. The vectorizer adds a runtime check
; in %entry: if the two ranges overlap, control goes to the original scalar
; %loop; otherwise to the vector loop. It then marks the vector loop's loads
; and store with alias.scope and noalias metadata, recording what the check
; proved.
;
; @axpy_noalias says y is noalias, so the check, and the scalar fallback,
; are gone. Each lane still does its own fmul then fadd, so no result changes.
;
; Run: opt -S -passes='loop-vectorize,simplifycfg' -force-vector-width=4
;      -force-vector-interleave=1 runtime_checks.ll
; (simplifycfg only removes the blocks that can no longer run.)
;
; Follows: LLVM Auto-Vectorization, "Runtime Checks of Pointers"; LLVM
; Language Reference, noalias and "'noalias' and 'alias.scope' Metadata".

define void @axpy_may_alias(ptr %y, ptr %x, float %a) {
entry:
  br label %loop

loop:
  %i = phi i64 [ 0, %entry ], [ %i.next, %loop ]
  %px = getelementptr inbounds float, ptr %x, i64 %i
  %py = getelementptr inbounds float, ptr %y, i64 %i
  %xv = load float, ptr %px
  %yv = load float, ptr %py
  %t = fmul float %a, %xv
  %s = fadd float %yv, %t
  store float %s, ptr %py
  %i.next = add nuw nsw i64 %i, 1
  %more = icmp ult i64 %i.next, 64
  br i1 %more, label %loop, label %done

done:
  ret void
}

define void @axpy_noalias(ptr noalias %y, ptr %x, float %a) {
entry:
  br label %loop

loop:
  %i = phi i64 [ 0, %entry ], [ %i.next, %loop ]
  %px = getelementptr inbounds float, ptr %x, i64 %i
  %py = getelementptr inbounds float, ptr %y, i64 %i
  %xv = load float, ptr %px
  %yv = load float, ptr %py
  %t = fmul float %a, %xv
  %s = fadd float %yv, %t
  store float %s, ptr %py
  %i.next = add nuw nsw i64 %i, 1
  %more = icmp ult i64 %i.next, 64
  br i1 %more, label %loop, label %done

done:
  ret void
}

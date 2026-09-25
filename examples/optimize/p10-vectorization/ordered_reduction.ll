; Two copies of one loop, sum += x[i] over 64 floats. They differ only in
; the fast-math flag on the add: @sum_strict has none, @sum_reassoc has
; `reassoc`, the permission to regroup the additions.
;
; The loop vectorizer widens both on an AArch64 target, in two different
; shapes. @sum_strict becomes an ordered reduction: each group of four loaded
; values is folded into the running total by llvm.vector.reduce.fadd without
; `reassoc`, which the Language Reference defines as sequential, lane 0 first.
; @sum_reassoc keeps four partial sums in one vector and folds them together
; once, after the loop. Change the triple to x86_64-apple-macosx and
; @sum_strict is refused instead: that target does not offer ordered
; reductions.
;
; Run: opt -S -passes=loop-vectorize -force-vector-interleave=1 ordered_reduction.ll
; (the vector width is left to the AArch64 cost model; forcing a width would
; itself grant permission to reorder)
;
; Follows: LLVM, "Auto-Vectorization in LLVM", section "Reductions"; LLVM
; Language Reference, "llvm.vector.reduce.fadd.*" Intrinsic. The functions
; are original.

target triple = "arm64-apple-macosx"

define float @sum_strict(ptr noalias readonly %x) {
entry:
  br label %loop

loop:
  %i = phi i64 [ 0, %entry ], [ %i.next, %loop ]
  %sum = phi float [ 0.0, %entry ], [ %sum.next, %loop ]
  %p = getelementptr inbounds float, ptr %x, i64 %i
  %v = load float, ptr %p, align 4
  %sum.next = fadd float %sum, %v
  %i.next = add nuw nsw i64 %i, 1
  %done = icmp eq i64 %i.next, 64
  br i1 %done, label %exit, label %loop

exit:
  ret float %sum.next
}

define float @sum_reassoc(ptr noalias readonly %x) {
entry:
  br label %loop

loop:
  %i = phi i64 [ 0, %entry ], [ %i.next, %loop ]
  %sum = phi float [ 0.0, %entry ], [ %sum.next, %loop ]
  %p = getelementptr inbounds float, ptr %x, i64 %i
  %v = load float, ptr %p, align 4
  %sum.next = fadd reassoc float %sum, %v
  %i.next = add nuw nsw i64 %i, 1
  %done = icmp eq i64 %i.next, 64
  br i1 %done, label %exit, label %loop

exit:
  ret float %sum.next
}

; Optimization remarks from LLVM 18's loop vectorizer, serialized as YAML.
;
; Run: opt -passes=loop-vectorize -disable-output -pass-remarks-output=- \
;          remarks.ll
; The file name "-" sends the YAML to standard output.
; For the same remarks as diagnostics on standard error, use
; -pass-remarks=, -pass-remarks-missed= and -pass-remarks-analysis=loop-vectorize.
;
;   @scale       y[i] = 2 * x[i]. Legal to vectorize, but this file names no
;                target, so opt uses LLVM's default machine description, whose
;                registers are 32 bits wide, and the cost model refuses: a
;                profitability decision.
;   @sum         total += x[i] in float. Vectorizing would add the numbers in
;                another order, which the plain fadd does not allow: a
;                legality decision, reported as an AnalysisFPCommute remark.
;   @sum_hinted  The same loop with a hint asking for 4 lanes. LLVM 18 treats
;                such a hint as permission to reorder floating-point
;                additions, so this loop is vectorized and its result bits
;                may differ from @sum's.
;
; Follows: LLVM "Remarks" documentation (YAML remarks), and LLVM 18
; LoopVectorizationLegality.cpp (LoopVectorizeHints::allowReordering).

define void @scale(ptr noalias %y, ptr noalias %x) {
entry:
  br label %loop
loop:
  %i = phi i64 [ 0, %entry ], [ %i.next, %loop ]
  %px = getelementptr inbounds float, ptr %x, i64 %i
  %v = load float, ptr %px
  %s = fmul float %v, 2.0
  %py = getelementptr inbounds float, ptr %y, i64 %i
  store float %s, ptr %py
  %i.next = add nuw nsw i64 %i, 1
  %done = icmp eq i64 %i.next, 1024
  br i1 %done, label %exit, label %loop
exit:
  ret void
}

define float @sum(ptr %x) {
entry:
  br label %loop
loop:
  %i = phi i64 [ 0, %entry ], [ %i.next, %loop ]
  %total = phi float [ 0.0, %entry ], [ %total.next, %loop ]
  %px = getelementptr inbounds float, ptr %x, i64 %i
  %v = load float, ptr %px
  %total.next = fadd float %total, %v
  %i.next = add nuw nsw i64 %i, 1
  %done = icmp eq i64 %i.next, 1024
  br i1 %done, label %exit, label %loop
exit:
  ret float %total.next
}

define float @sum_hinted(ptr %x) {
entry:
  br label %loop
loop:
  %i = phi i64 [ 0, %entry ], [ %i.next, %loop ]
  %total = phi float [ 0.0, %entry ], [ %total.next, %loop ]
  %px = getelementptr inbounds float, ptr %x, i64 %i
  %v = load float, ptr %px
  %total.next = fadd float %total, %v
  %i.next = add nuw nsw i64 %i, 1
  %done = icmp eq i64 %i.next, 1024
  br i1 %done, label %exit, label %loop, !llvm.loop !0
exit:
  ret float %total.next
}

!0 = distinct !{!0, !1}
!1 = !{!"llvm.loop.vectorize.width", i32 4}

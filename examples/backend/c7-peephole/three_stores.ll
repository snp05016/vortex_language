; Three stores of f32 values to consecutive addresses, as a code generator
; writes out the three elements of a small array.
define void @store3(ptr %p, float %a, float %b, float %c) {
  %p1 = getelementptr inbounds float, ptr %p, i64 1
  %p2 = getelementptr inbounds float, ptr %p, i64 2
  store float %a, ptr %p
  store float %b, ptr %p1
  store float %c, ptr %p2
  ret void
}

; A loop that reads two neighbouring f32 values per trip and moves the
; pointer on by two elements.
define float @sum_pairs(ptr %p, i64 %n) {
entry:
  br label %loop

loop:
  %q = phi ptr [ %p, %entry ], [ %q.next, %loop ]
  %i = phi i64 [ 0, %entry ], [ %i.next, %loop ]
  %s = phi float [ 0.0, %entry ], [ %s2, %loop ]
  %q1 = getelementptr inbounds float, ptr %q, i64 1
  %x = load float, ptr %q
  %y = load float, ptr %q1
  %s1 = fadd float %s, %x
  %s2 = fadd float %s1, %y
  %q.next = getelementptr inbounds float, ptr %q, i64 2
  %i.next = add i64 %i, 1
  %done = icmp eq i64 %i.next, %n
  br i1 %done, label %exit, label %loop

exit:
  ret float %s2
}

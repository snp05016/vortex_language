; Two timing loops around the same pure function, before `opt -O2`.
; @plain calls @work(42) five times and sums the results. @guarded does the
; same, but passes the input through an empty inline-asm statement that may
; rewrite it ("=r,0") and hands each result to one that reads it and may
; touch memory ("r,~{memory}"). Compare what -O2 leaves of each.
;
; Follows: google/benchmark user guide, "Preventing Optimization"; LLVM
; Language Reference, "Inline Assembler Expressions".

define internal i64 @work(i64 %seed) {
entry:
  %a = mul i64 %seed, %seed
  %b = xor i64 %a, 12345
  ret i64 %b
}

define i64 @plain() {
entry:
  br label %loop
loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %total = phi i64 [ 0, %entry ], [ %total.next, %loop ]
  %r = call i64 @work(i64 42)
  %total.next = add i64 %total, %r
  %i.next = add i32 %i, 1
  %more = icmp ult i32 %i.next, 5
  br i1 %more, label %loop, label %exit
exit:
  ret i64 %total.next
}

define i64 @guarded() {
entry:
  br label %loop
loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %total = phi i64 [ 0, %entry ], [ %total.next, %loop ]
  %in = call i64 asm sideeffect "", "=r,0"(i64 42)
  %r = call i64 @work(i64 %in)
  call void asm sideeffect "", "r,~{memory}"(i64 %r)
  %total.next = add i64 %total, %r
  %i.next = add i32 %i, 1
  %more = icmp ult i32 %i.next, 5
  br i1 %more, label %loop, label %exit
exit:
  ret i64 %total.next
}

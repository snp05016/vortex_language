; Two loop counters that always hold the same value. %i and %j both start at
; 0 and both add 1 on every trip, so %i.next - %j.next is always 0.
;
; Proving it needs values the loop has not computed yet. A numbering that
; hashes each instruction once, in order, reaches the phis before %i.next and
; %j.next exist, so it cannot give the two phis one number, and then the two
; additions differ as well. LLVM's gvn pass, which gives every phi its own
; number, leaves this function unchanged.
;
; NewGVN is optimistic: every value starts in one class, equal to everything,
; and a class splits only when a difference is proved. Assuming %i equals %j
; makes %i.next equal %j.next, which confirms the assumption. So %j and
; %j.next are replaced by %i and %i.next, and the subtraction folds to 0.
;
; Follows: LLVM's NewGVN.cpp and GVN.cpp, release/18.x; Alpern, Wegman and
; Zadeck, POPL 1988, section 1.

define i32 @twins(i32 %n) {
entry:
  br label %loop

loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %j = phi i32 [ 0, %entry ], [ %j.next, %loop ]
  %i.next = add i32 %i, 1
  %j.next = add i32 %j, 1
  %done = icmp sge i32 %i.next, %n
  br i1 %done, label %exit, label %loop

exit:
  %diff = sub i32 %i.next, %j.next
  ret i32 %diff
}

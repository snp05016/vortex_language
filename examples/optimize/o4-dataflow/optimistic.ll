; A loop in which x starts at 1 and each iteration sets x = 2 - x, so x is
; 1 forever. The header's phi joins 1 from the entry with x.next from the
; back edge, and x.next depends on the phi: the facts form a cycle.
;
; A pessimistic analysis assumes nothing about x.next before it has been
; computed, so the phi joins 1 with "not a constant" and x is lost. SCCP is
; optimistic: every value starts as "no value yet", the phi first sees only 1,
; 2 - 1 gives 1 again, and the cycle confirms x = 1. The pass then replaces x
; and x.next with the constant, and returns 1.
;
; Follows: LLVM's pass list (sccp) and ValueLattice.h, release/18.x.

define i32 @flip(i32 %n) {
entry:
  br label %loop

loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %x = phi i32 [ 1, %entry ], [ %x.next, %loop ]
  %x.next = sub i32 2, %x
  %i.next = add i32 %i, 1
  %done = icmp sge i32 %i.next, %n
  br i1 %done, label %exit, label %loop

exit:
  ret i32 %x.next
}

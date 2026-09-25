; 1 + 2 + ... + n for n >= 1. The loop carries two values around its back
; edge, so the IR needs two phis: the chapter follows them through
; phi-node-elimination (phis become copies) and register-coalescer (the
; copies disappear, and with them SSA form).
define i32 @triangle(i32 %n) {
entry:
  br label %loop

loop:
  %i = phi i32 [ 1, %entry ], [ %i.next, %loop ]
  %acc = phi i32 [ 0, %entry ], [ %acc.next, %loop ]
  %acc.next = add i32 %acc, %i
  %i.next = add i32 %i, 1
  %done = icmp sgt i32 %i.next, %n
  br i1 %done, label %exit, label %loop

exit:
  ret i32 %acc.next
}

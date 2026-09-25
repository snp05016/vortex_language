; A counting loop in the shape a front end might first emit it, from
;
;   int i = skip_zero ? 1 : 0, sum = 0;
;   while (i < n) { sum += i; i += 1; }
;   return sum;
;
; Three things about it are awkward for loop passes: two edges enter the
; header from outside, so there is no preheader; the test is at the top, so
; the body may run zero times; and sum, defined in the loop, is used after it.
;
; The loop pass manager first puts the loop into loop simplify form and
; loop-closed SSA form, then loop-rotate runs. In the output:
;
;   header.preheader       the preheader that loop simplify form added; its phi
;                          merges the two starting values, and after rotation
;                          it holds the guard %more1, the test copied in front
;                          of the loop
;   latch.lr.ph            the rotated loop's own preheader
;   latch                  the whole loop: one block, tested at the bottom
;   header.exit_crit_edge  a dedicated exit, holding the loop-closing phi %split
;   exit                   merges that value with 0 from the zero-trip path
;
; Follows: LLVM Loop Terminology, sections "Loop Simplify Form", "Loop Closed
; SSA (LCSSA)" and "Rotated Loops"; LLVM's pass list (loop-simplify, lcssa,
; loop-rotate).

define i32 @triangle(i32 %n, i1 %skip_zero) {
entry:
  br i1 %skip_zero, label %from_one, label %header

from_one:
  br label %header

header:
  %i = phi i32 [ 0, %entry ], [ 1, %from_one ], [ %next, %latch ]
  %sum = phi i32 [ 0, %entry ], [ 0, %from_one ], [ %sum.next, %latch ]
  %more = icmp slt i32 %i, %n
  br i1 %more, label %latch, label %exit

latch:
  %sum.next = add i32 %sum, %i
  %next = add nsw i32 %i, 1
  br label %header

exit:
  ret i32 %sum
}

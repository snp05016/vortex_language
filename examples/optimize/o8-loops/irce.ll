; The loop from check_elimination.ll whose check could not be removed:
;
;   clear_first:  for i in 0..count { values[i] = 0; }   (values has 64 elements)
;
; The check passes exactly while i < 64, a contiguous range of the induction
; variable, so it is what IRCE calls an inductive range check. IRCE needs the
; loop rotated (tested at the bottom), then splits the iteration space. A main
; loop runs i from 0 up to min(count, 64) with no check, and a post-loop runs
; whatever is left, still checked. No pre-loop is needed, since i starts at 0.
; If count is 70, iterations 0 to 63 run unchecked and iteration 64 fails its
; check in the post-loop: the same iteration and the same report as before.
; simplifycfg and dce only tidy up afterwards.
;
; Follows: LLVM 18's InductiveRangeCheckElimination.cpp (file header and the
; InductiveRangeCheck class comment), and LLVM Loop Terminology, "Rotated Loops".

declare void @report_bounds(i32, i32) noreturn

define void @clear_first(ptr %values, i32 %count) {
entry:
  br label %header

header:
  %i = phi i32 [ 0, %entry ], [ %next, %body ]
  %more = icmp slt i32 %i, %count
  br i1 %more, label %check, label %done

check:
  %ok = icmp ult i32 %i, 64
  br i1 %ok, label %body, label %fail

body:
  %offset = zext i32 %i to i64
  %p = getelementptr inbounds i32, ptr %values, i64 %offset
  store i32 0, ptr %p
  %next = add nsw i32 %i, 1
  br label %header

done:
  ret void

fail:
  call void @report_bounds(i32 %i, i32 64)
  unreachable
}

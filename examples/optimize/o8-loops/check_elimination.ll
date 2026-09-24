; Two loops that clear an array of 64 i32 values, each index checked the way a
; safe language checks it: a comparison, and a call to a runtime report that
; never returns. Compared as unsigned numbers, one comparison covers both
; halves of 0 <= i < 64, because a negative i32 reads as a large unsigned value.
;
;   clear_all:    for i in 0..64    { values[i] = 0; }
;   clear_first:  for i in 0..count { values[i] = 0; }
;
; IndVarSimplify asks scalar evolution about each comparison that uses an
; induction variable, at the point where it is used. In clear_all, i is
; {0,+,1} and the header's test i < 64 dominates the check, so the check is
; always true: it becomes the constant true, and simplifycfg then deletes the
; branch and the report. In clear_first nothing bounds count, so the check
; stays. Both loops also gain nuw nsw on the step, which the pass proved.
;
; Follows: LLVM 18's IndVarSimplify.cpp (file header) and SimplifyIndVar.cpp
; (eliminateIVComparison), and LLVM's pass list (indvars, simplifycfg).

declare void @report_bounds(i32, i32) noreturn

define void @clear_all(ptr %values) {
entry:
  br label %header

header:
  %i = phi i32 [ 0, %entry ], [ %next, %body ]
  %more = icmp slt i32 %i, 64
  br i1 %more, label %check, label %done

check:
  %ok = icmp ult i32 %i, 64
  br i1 %ok, label %body, label %fail

body:
  %offset = zext i32 %i to i64
  %p = getelementptr inbounds i32, ptr %values, i64 %offset
  store i32 0, ptr %p
  %next = add i32 %i, 1
  br label %header

done:
  ret void

fail:
  call void @report_bounds(i32 %i, i32 64)
  unreachable
}

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
  %next = add i32 %i, 1
  br label %header

done:
  ret void

fail:
  call void @report_bounds(i32 %i, i32 64)
  unreachable
}

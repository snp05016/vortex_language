; Two checked loops and the loop vectorizer.
;
;   scale_all:  for i in 0..64 { values[i] = values[i] * 2.0; }   (f32, bounds check)
;   bump_all:   for i in 0..64 { counts[i] += 1; }                (i32, overflow check)
;
; LLVM 18's vectorizer asks scalar evolution for the loop's backedge-taken
; count, which exists only if every exit's count can be computed. The bounds
; check exits when i reaches 64, a count scalar evolution can work out, so the
; first loop is vectorized; the check stays in a scalar epilogue that runs the
; final iterations one at a time. The overflow check exits when a loaded value
; is the largest i32, so adding 1 would overflow; no formula predicts when that
; happens, and the second loop is not vectorized.
;
; Each multiply by 2.0 happens in its own lane, so vectorizing the first loop
; changes no result bit. The vector width is forced to 4 so that the output
; does not depend on a target's cost model.
;
; Follows: LLVM 18's LoopAccessAnalysis.cpp (canAnalyzeLoop), LoopVectorize.cpp
; (requiresScalarEpilogue), and LLVM Remarks.

declare void @report_bounds(i32, i32) noreturn
declare void @report_overflow() noreturn

define void @scale_all(ptr %values) {
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
  %p = getelementptr inbounds float, ptr %values, i64 %offset
  %x = load float, ptr %p
  %y = fmul float %x, 2.0
  store float %y, ptr %p
  %next = add nsw i32 %i, 1
  br label %header

done:
  ret void

fail:
  call void @report_bounds(i32 %i, i32 64)
  unreachable
}

define void @bump_all(ptr %counts) {
entry:
  br label %header

header:
  %i = phi i32 [ 0, %entry ], [ %next, %body ]
  %more = icmp slt i32 %i, 64
  br i1 %more, label %add, label %done

add:
  %offset = zext i32 %i to i64
  %p = getelementptr inbounds i32, ptr %counts, i64 %offset
  %x = load i32, ptr %p
  %overflow = icmp eq i32 %x, 2147483647
  br i1 %overflow, label %fail, label %body

body:
  %y = add i32 %x, 1
  store i32 %y, ptr %p
  %next = add nsw i32 %i, 1
  br label %header

done:
  ret void

fail:
  call void @report_overflow()
  unreachable
}

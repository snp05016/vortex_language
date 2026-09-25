; LLVM 18's loop-interchange pass, applied to one small loop nest.
;
; @bump_columns adds 1 to every element of a row-major 64 x 64 array, but its
; outer loop runs over columns (%j) and its inner loop over rows (%i), so
; consecutive inner iterations are a whole row (256 bytes) apart. The only
; dependence is each element on itself within one iteration, so swapping the
; loops is legal. The pass's first cost model, loop cache analysis, needs a
; cache line size, which this file does not give, so it rates both loops alike
; (opt -passes='print<loop-cache-cost>'). Its fallback sees the subscripts
; [%i][%j] name the inner loop's variable first and finds the swap profitable.
;
; Run: opt -S -passes=loop-interchange column_walk.ll
; Add -pass-remarks=loop-interchange to see the remark on standard error:
; "Loop interchanged with enclosing loop."
;
; In the output the %row loop (i) is outside and the %col loop (j) inside, so
; the inner loop walks memory with unit stride. The old inner-loop exit test
; (%i.next, %row.done) is left behind unused; later cleanup passes delete it.
;
; Follows: LLVM 18 LoopInterchange.cpp, and the LLVM Language Reference for
; getelementptr on nested array types.

@grid = global [64 x [64 x i32]] zeroinitializer

define void @bump_columns() {
entry:
  br label %col

col:                                      ; outer loop: j = 0 .. 63
  %j = phi i64 [ 0, %entry ], [ %j.next, %col.latch ]
  br label %row

row:                                      ; inner loop: i = 0 .. 63
  %i = phi i64 [ 0, %col ], [ %i.next, %row ]
  %p = getelementptr inbounds [64 x [64 x i32]], ptr @grid, i64 0, i64 %i, i64 %j
  %v = load i32, ptr %p
  %v.1 = add i32 %v, 1
  store i32 %v.1, ptr %p
  %i.next = add nuw nsw i64 %i, 1
  %row.done = icmp eq i64 %i.next, 64
  br i1 %row.done, label %col.latch, label %row

col.latch:
  %j.next = add nuw nsw i64 %j, 1
  %col.done = icmp eq i64 %j.next, 64
  br i1 %col.done, label %exit, label %col

exit:
  ret void
}

; LLVM 18's dependence analysis on one loop nest written two ways.
;
; Both functions copy each element of a 64 x 64 array of i32 one row down:
; grid[i + 1, j] = grid[i, j], with the column loop j outside and the row
; loop i inside. The flow dependence has distance (0, 1) in (j, i) order, so
; swapping the loops is legal: in (i, j) order it becomes (1, 0).
;
; @down_2d addresses the array as [64 x i32] rows, so the subscripts arrive
; separately. @down_flat computes the same address by hand, i * 64 + j, as C
; code over a plain pointer does. loop-interchange asks DependenceAnalysis
; about each nest and reports what it decided (standard output, YAML).
;
; Run: opt -passes=loop-interchange -disable-output -pass-remarks-output=- llvm_subscripts.ll
; To see the analysis itself (on standard error):
;      opt -passes='print<da>' -disable-output llvm_subscripts.ll
;
; Follows: LLVM 18 DependenceAnalysis.cpp (file header, tryDelinearize) and
; LoopInterchange.cpp.

define void @down_2d(ptr noalias %grid) {
entry:
  br label %col

col:                                      ; outer loop: j = 0 .. 63
  %j = phi i64 [ 0, %entry ], [ %j.next, %col.latch ]
  br label %row

row:                                      ; inner loop: i = 0 .. 62
  %i = phi i64 [ 0, %col ], [ %i.next, %row ]
  %src = getelementptr inbounds [64 x i32], ptr %grid, i64 %i, i64 %j
  %v = load i32, ptr %src
  %i.next = add nuw nsw i64 %i, 1
  %dst = getelementptr inbounds [64 x i32], ptr %grid, i64 %i.next, i64 %j
  store i32 %v, ptr %dst
  %row.done = icmp eq i64 %i.next, 63
  br i1 %row.done, label %col.latch, label %row

col.latch:
  %j.next = add nuw nsw i64 %j, 1
  %col.done = icmp eq i64 %j.next, 64
  br i1 %col.done, label %exit, label %col

exit:
  ret void
}

define void @down_flat(ptr noalias %grid) {
entry:
  br label %col

col:
  %j = phi i64 [ 0, %entry ], [ %j.next, %col.latch ]
  br label %row

row:
  %i = phi i64 [ 0, %col ], [ %i.next, %row ]
  %base = mul nuw nsw i64 %i, 64
  %at = add nuw nsw i64 %base, %j         ; i * 64 + j
  %src = getelementptr inbounds i32, ptr %grid, i64 %at
  %v = load i32, ptr %src
  %below = add nuw nsw i64 %at, 64        ; (i + 1) * 64 + j
  %dst = getelementptr inbounds i32, ptr %grid, i64 %below
  store i32 %v, ptr %dst
  %i.next = add nuw nsw i64 %i, 1
  %row.done = icmp eq i64 %i.next, 63
  br i1 %row.done, label %col.latch, label %row

col.latch:
  %j.next = add nuw nsw i64 %j, 1
  %col.done = icmp eq i64 %j.next, 64
  br i1 %col.done, label %exit, label %col

exit:
  ret void
}

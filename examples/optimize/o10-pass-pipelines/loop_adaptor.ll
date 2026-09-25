; fill writes k * k into every cell of a table, row by row. Two things about
; its loops matter here. The outer loop is entered from two blocks, entry and
; alt, so it has no preheader. And %ksq, computed in the inner loop, has the
; same value on every trip of both loops.
;
; The pipeline text names one loop pass, licm, under loop-mssa. The adaptor
; that loop-mssa builds runs LoopSimplify and LCSSA over the function before
; any loop pass (its constructor adds both), and LoopSimplify builds
; outer.preheader, with a phi, %r.ph, for the two starting values of %r. The
; adaptor then runs licm on each loop, innermost first. On the inner loop,
; licm hoists %ksq into row, the block that runs once before each row. On the
; outer loop, row is an ordinary block of that loop, so licm hoists %ksq
; again, into outer.preheader. LICM skips blocks that belong to inner loops,
; which it expects to be done already, so it is the innermost-first order
; that lets one licm move %ksq out of both loops.
;
; Follows: LLVM's New Pass Manager guide (Adding Passes to a Pass Manager),
; and LLVM 18's LoopPassManager.h (FunctionToLoopPassAdaptor) and LICM.cpp
; (hoistRegion).

define void @fill(ptr %table, i32 %rows, i32 %cols, float %k, i1 %skip_first) {
entry:
  br i1 %skip_first, label %alt, label %outer
alt:
  br label %outer
outer:
  %r = phi i32 [ 0, %entry ], [ 1, %alt ], [ %r.next, %outer.latch ]
  %r.more = icmp slt i32 %r, %rows
  br i1 %r.more, label %row, label %done
row:
  %row.start = mul i32 %r, %cols
  br label %inner
inner:
  %c = phi i32 [ 0, %row ], [ %c.next, %cell ]
  %c.more = icmp slt i32 %c, %cols
  br i1 %c.more, label %cell, label %outer.latch
cell:
  %ksq = fmul float %k, %k
  %index = add i32 %row.start, %c
  %p = getelementptr float, ptr %table, i32 %index
  store float %ksq, ptr %p
  %c.next = add i32 %c, 1
  br label %inner
outer.latch:
  %r.next = add i32 %r, 1
  br label %outer
done:
  ret void
}

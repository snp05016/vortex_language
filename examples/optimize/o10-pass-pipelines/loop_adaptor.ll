; scale_by_square has no preheader in the source text: two blocks reach
; header from outside the loop (entry directly, and alt after a check), so
; header has no single predecessor to hold code that runs once before the
; loop starts. licm is a loop pass, so the pipeline text 'loop-mssa(licm)'
; wraps it in a FunctionToLoopPassAdaptor before running it. The adaptor is
; the thing this chapter calls a level: it puts the function's loops into
; the canonical form every loop pass expects (LLVM's Loop Terminology, O8)
; before licm ever runs, splitting a fresh header.preheader block out of the
; two incoming edges on its own. Only then does licm hoist %ksq, which does
; not change across iterations, into that block, out of the loop it was
; written inside.
;
; Run: opt -passes='loop-mssa(licm)' loop_adaptor.ll
;
; Follows: LLVM's New Pass Manager guide (adaptors between IR levels) and
; LLVM's Analysis and Transform Passes (the licm entry).

define void @scale_by_square(ptr noalias %out, float %k, i32 %n, i1 %use_alt) {
entry:
  br i1 %use_alt, label %alt, label %header
alt:
  br label %header
header:
  %i = phi i32 [ 0, %entry ], [ 0, %alt ], [ %next, %body ]
  %cmp = icmp slt i32 %i, %n
  br i1 %cmp, label %body, label %exit
body:
  %ksq = fmul float %k, %k
  %gep.out = getelementptr inbounds float, ptr %out, i32 %i
  %old = load float, ptr %gep.out
  %scaled = fmul float %old, %ksq
  store float %scaled, ptr %gep.out
  %next = add nsw i32 %i, 1
  br label %header
exit:
  ret void
}

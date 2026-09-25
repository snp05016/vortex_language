; One pass in two positions: the loop vectorizer, then SROA, then the loop
; vectorizer again. The output is the remark stream, not the IR.
;
; halve is written the way a front end emits code before any optimization:
; the loop counter lives in a stack slot, %i, and every trip loads it,
; compares it and stores the next value back. The first loop-vectorize finds
; no induction variable, since the counter is in memory rather than in a
; register, and so cannot count the trips; it reports both reasons and misses.
; SROA turns the slot into SSA values joined by a phi. The second
; loop-vectorize, the same pass with the same options, now finds both and
; vectorizes the loop.
;
; The width is forced to 4 so that the output does not depend on a target's
; cost model. With a forced width, the vectorizer files its analysis remarks
; under an empty pass name, '', which means "always report".
;
; Follows: LLVM Remarks (serialized remarks), LLVM's New Pass Manager guide
; (Invoking opt), and LLVM 18's LoopVectorizationLegality.cpp
; (vectorizeAnalysisPassName).

define void @halve(ptr %x, i32 %n) {
entry:
  %i = alloca i32
  store i32 0, ptr %i
  br label %test
test:
  %i.0 = load i32, ptr %i
  %more = icmp slt i32 %i.0, %n
  br i1 %more, label %body, label %done
body:
  %i.1 = load i32, ptr %i
  %wide = sext i32 %i.1 to i64
  %p = getelementptr inbounds float, ptr %x, i64 %wide
  %v = load float, ptr %p
  %h = fmul float %v, 5.000000e-01
  store float %h, ptr %p
  %next = add nsw i32 %i.1, 1
  store i32 %next, ptr %i
  br label %test
done:
  ret void
}

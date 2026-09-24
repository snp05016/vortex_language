; LLVM 18's inliner judges each call site on its own. It walks the callee as
; if the call's arguments were already in place, adds a cost for every
; instruction that would survive, subtracts what removing the call saves, and
; inlines when the total is below a threshold. It reports every decision.
;
; Run: opt -passes=inline -disable-output -pass-remarks-output=- inline_cost.ll
;
;   @shade         Two calls from @fast_caller. With the constant mode 0 the
;                  branch folds, so the slow block is never counted.
;   @square        Internal, and called once: inlining that call lets the
;                  function be deleted, which earns a large bonus.
;   @factorial     Calls itself, so this inliner never inlines it.
;   @small_caller  Marked minsize, which -Oz sets: its threshold is 5 and the
;                  bonuses are gone, so the same call to @shade is refused.
;
; Follows: LLVM 18 InlineCost.cpp (CallAnalyzer, updateThreshold,
; getCallsiteCost), InlineCost.h (the thresholds) and InlineAdvisor.cpp (the
; remark names).

define float @shade(float %x, i32 %mode) {
entry:
  %plain = icmp eq i32 %mode, 0
  br i1 %plain, label %fast, label %slow
fast:
  ret float %x
slow:
  %x2 = fmul float %x, %x
  %x3 = fmul float %x2, %x
  %sum = fadd float %x2, %x3
  %scaled = fmul float %sum, 5.000000e-01
  %shifted = fsub float %scaled, %x
  %clamped = call float @llvm.maxnum.f32(float %shifted, float 0.000000e+00)
  ret float %clamped
}

define internal float @square(float %x) {
  %r = fmul float %x, %x
  ret float %r
}

define i32 @factorial(i32 %n) {
entry:
  %small = icmp slt i32 %n, 2
  br i1 %small, label %one, label %more
one:
  ret i32 1
more:
  %m = sub i32 %n, 1
  %f = call i32 @factorial(i32 %m)
  %p = mul i32 %n, %f
  ret i32 %p
}

define float @fast_caller(float %x, i32 %mode) {
  %a = call float @shade(float %x, i32 0)
  %b = call float @shade(float %x, i32 %mode)
  %c = call float @square(float %b)
  %s = fadd float %a, %c
  ret float %s
}

define float @small_caller(float %x, i32 %mode) minsize optsize {
  %b = call float @shade(float %x, i32 %mode)
  ret float %b
}

declare float @llvm.maxnum.f32(float, float)

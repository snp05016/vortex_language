; A 4-wide row of accumulators from a micro-kernel, fully unrolled over two
; steps of k so every index into the local array `acc` is a compile-time
; constant. SROA cuts the array alloca into four independent scalars, which
; is exactly the step full unrolling exists to enable: PassBuilderPipelines.cpp
; runs SROA again after unrolling, with a comment that says it is there to
; delete small arrays.
;
; Run: opt -S -passes=sroa accumulator_sroa.ll
;
; Follows: LLVM 18 SROA.cpp (the file header, AllocaSlices and the handling
; of constant-offset array accesses); PassBuilderPipelines.cpp (SROA after
; full unrolling).

define void @row4(float %a0, float %a1, float %a2, float %a3,
                   float %b0, float %b1,
                   ptr %out0, ptr %out1, ptr %out2, ptr %out3) {
entry:
  %acc = alloca [4 x float]
  %p0 = getelementptr inbounds [4 x float], ptr %acc, i64 0, i64 0
  %p1 = getelementptr inbounds [4 x float], ptr %acc, i64 0, i64 1
  %p2 = getelementptr inbounds [4 x float], ptr %acc, i64 0, i64 2
  %p3 = getelementptr inbounds [4 x float], ptr %acc, i64 0, i64 3
  store float 0.000000e+00, ptr %p0
  store float 0.000000e+00, ptr %p1
  store float 0.000000e+00, ptr %p2
  store float 0.000000e+00, ptr %p3

  ; k = 0
  %t0.0 = fmul float %a0, %b0
  %l0.0 = load float, ptr %p0
  %s0.0 = fadd float %l0.0, %t0.0
  store float %s0.0, ptr %p0
  %t1.0 = fmul float %a1, %b0
  %l1.0 = load float, ptr %p1
  %s1.0 = fadd float %l1.0, %t1.0
  store float %s1.0, ptr %p1
  %t2.0 = fmul float %a2, %b0
  %l2.0 = load float, ptr %p2
  %s2.0 = fadd float %l2.0, %t2.0
  store float %s2.0, ptr %p2
  %t3.0 = fmul float %a3, %b0
  %l3.0 = load float, ptr %p3
  %s3.0 = fadd float %l3.0, %t3.0
  store float %s3.0, ptr %p3

  ; k = 1
  %t0.1 = fmul float %a0, %b1
  %l0.1 = load float, ptr %p0
  %s0.1 = fadd float %l0.1, %t0.1
  store float %s0.1, ptr %p0
  %t1.1 = fmul float %a1, %b1
  %l1.1 = load float, ptr %p1
  %s1.1 = fadd float %l1.1, %t1.1
  store float %s1.1, ptr %p1
  %t2.1 = fmul float %a2, %b1
  %l2.1 = load float, ptr %p2
  %s2.1 = fadd float %l2.1, %t2.1
  store float %s2.1, ptr %p2
  %t3.1 = fmul float %a3, %b1
  %l3.1 = load float, ptr %p3
  %s3.1 = fadd float %l3.1, %t3.1
  store float %s3.1, ptr %p3

  %f0 = load float, ptr %p0
  %f1 = load float, ptr %p1
  %f2 = load float, ptr %p2
  %f3 = load float, ptr %p3
  store float %f0, ptr %out0
  store float %f1, ptr %out1
  store float %f2, ptr %out2
  store float %f3, ptr %out3
  ret void
}

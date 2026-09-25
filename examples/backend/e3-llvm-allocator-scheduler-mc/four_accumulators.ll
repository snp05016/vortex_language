; Sum sixteen f32 values from a[0..15] with four independent running
; accumulators (acc0 to acc3), combined only at the end. The IR lists the
; loads and adds in one fixed order, but most of them do not depend on each
; other, so the machine scheduler has real choices here.
;
; The page compiles this file twice with llc -O2: once with the default
; processor and once with -mcpu=apple-m1. The two scheduling models order the
; loads differently, and that changes how many floating-point registers the
; allocator then needs (eight in one listing, six in the other). Neither
; order changes a single result: every fadd still reads the same operands.
;
; Follows: LLVM Language Reference Manual, "getelementptr" instruction
; (https://llvm.org/docs/LangRef.html#getelementptr-instruction), and
; llvm/include/llvm/CodeGen/MachineScheduler.h, LLVM 18.1.8
; (https://github.com/llvm/llvm-project/blob/llvmorg-18.1.8/llvm/include/llvm/CodeGen/MachineScheduler.h).

define float @four_accumulators(ptr %a) {
entry:
  %v0 = load float, ptr %a
  %g1 = getelementptr float, ptr %a, i32 1
  %v1 = load float, ptr %g1
  %g2 = getelementptr float, ptr %a, i32 2
  %v2 = load float, ptr %g2
  %g3 = getelementptr float, ptr %a, i32 3
  %v3 = load float, ptr %g3
  %g4 = getelementptr float, ptr %a, i32 4
  %v4 = load float, ptr %g4
  %acc0_1 = fadd float %v0, %v4
  %g5 = getelementptr float, ptr %a, i32 5
  %v5 = load float, ptr %g5
  %acc1_1 = fadd float %v1, %v5
  %g6 = getelementptr float, ptr %a, i32 6
  %v6 = load float, ptr %g6
  %acc2_1 = fadd float %v2, %v6
  %g7 = getelementptr float, ptr %a, i32 7
  %v7 = load float, ptr %g7
  %acc3_1 = fadd float %v3, %v7
  %g8 = getelementptr float, ptr %a, i32 8
  %v8 = load float, ptr %g8
  %acc0_2 = fadd float %acc0_1, %v8
  %g9 = getelementptr float, ptr %a, i32 9
  %v9 = load float, ptr %g9
  %acc1_2 = fadd float %acc1_1, %v9
  %g10 = getelementptr float, ptr %a, i32 10
  %v10 = load float, ptr %g10
  %acc2_2 = fadd float %acc2_1, %v10
  %g11 = getelementptr float, ptr %a, i32 11
  %v11 = load float, ptr %g11
  %acc3_2 = fadd float %acc3_1, %v11
  %g12 = getelementptr float, ptr %a, i32 12
  %v12 = load float, ptr %g12
  %acc0_3 = fadd float %acc0_2, %v12
  %g13 = getelementptr float, ptr %a, i32 13
  %v13 = load float, ptr %g13
  %acc1_3 = fadd float %acc1_2, %v13
  %g14 = getelementptr float, ptr %a, i32 14
  %v14 = load float, ptr %g14
  %acc2_3 = fadd float %acc2_2, %v14
  %g15 = getelementptr float, ptr %a, i32 15
  %v15 = load float, ptr %g15
  %acc3_3 = fadd float %acc3_2, %v15
  %pair0 = fadd float %acc0_3, %acc1_3
  %pair1 = fadd float %acc2_3, %acc3_3
  %total = fadd float %pair0, %pair1
  ret float %total
}

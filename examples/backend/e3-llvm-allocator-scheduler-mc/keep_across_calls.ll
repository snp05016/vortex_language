; Ten f32 values are loaded before a loop that calls an outside function,
; and summed only after the loop. Every one of them is live across every
; call, and a call may overwrite any caller-saved register.
;
; AAPCS64 lets a callee change v0-v7 and v16-v31 but makes it keep the low
; 64 bits of v8-v15 (d8-d15). So only eight of the ten values can sit in
; registers that survive the calls; the other two must go to memory, or be
; reloaded, somewhere. The page compares where the greedy allocator puts
; that memory traffic (outside the loop) with where the fast allocator puts
; it (llc -regalloc=fast).
;
; Follows: AAPCS64, "SIMD and Floating-Point registers"
; (https://github.com/ARM-software/abi-aa/blob/main/aapcs64/aapcs64.rst),
; and Olesen, "Greedy Register Allocation in LLVM 3.0"
; (https://blog.llvm.org/2011/09/greedy-register-allocation-in-llvm-30.html).

declare void @tick(i32)

define float @keep_across_calls(ptr %a, i32 %n) {
entry:
  %v0 = load float, ptr %a
  %p1 = getelementptr float, ptr %a, i64 1
  %v1 = load float, ptr %p1
  %p2 = getelementptr float, ptr %a, i64 2
  %v2 = load float, ptr %p2
  %p3 = getelementptr float, ptr %a, i64 3
  %v3 = load float, ptr %p3
  %p4 = getelementptr float, ptr %a, i64 4
  %v4 = load float, ptr %p4
  %p5 = getelementptr float, ptr %a, i64 5
  %v5 = load float, ptr %p5
  %p6 = getelementptr float, ptr %a, i64 6
  %v6 = load float, ptr %p6
  %p7 = getelementptr float, ptr %a, i64 7
  %v7 = load float, ptr %p7
  %p8 = getelementptr float, ptr %a, i64 8
  %v8 = load float, ptr %p8
  %p9 = getelementptr float, ptr %a, i64 9
  %v9 = load float, ptr %p9
  br label %loop

loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  call void @tick(i32 %i)
  %i.next = add i32 %i, 1
  %again = icmp slt i32 %i.next, %n
  br i1 %again, label %loop, label %exit

exit:
  %s1 = fadd float %v0, %v1
  %s2 = fadd float %s1, %v2
  %s3 = fadd float %s2, %v3
  %s4 = fadd float %s3, %v4
  %s5 = fadd float %s4, %v5
  %s6 = fadd float %s5, %v6
  %s7 = fadd float %s6, %v7
  %s8 = fadd float %s7, %v8
  %s9 = fadd float %s8, %v9
  ret float %s9
}

; A loop that sums eight floats and also counts its iterations, though nothing
; reads the count:
;
;   fn total(values: &[f32; 8]) -> f32 {
;       let mut sum: f32 = 0.0;
;       let mut count = 0;
;       for i in 0..8 {
;           sum += values[i];
;           count += 1;
;       }
;       return sum;
;   }
;
; Lowered twice. Under C's rules, count += 1 is a plain addition: signed
; overflow is undefined, so there is nothing to check. Under Vortex's rules it
; is checked: the addition also reports overflow, and a branch calls the
; runtime's report, which never returns. (The bounds check on values[i] is
; left out of both.)
;
; ADCE assumes every instruction dead until an instruction with an effect, or
; a branch that decides whether one runs, needs it. The count's phi and its
; addition only feed each other, so under C's rules nothing live reaches them
; and they go. Under Vortex's rules the report call is live, so the branch
; that guards it is live, so the overflow flag, the addition and the phi are
; live too: the counter stays until a proof removes its check.
;
; Follows: LLVM's pass list (adce), and ADCE.cpp on the release/18.x branch.

declare void @report_overflow(i32) noreturn
declare { i32, i1 } @llvm.sadd.with.overflow.i32(i32, i32)

define float @total_c_rules(ptr %values) {
entry:
  br label %head

head:
  %i = phi i64 [ 0, %entry ], [ %i.next, %body ]
  %sum = phi float [ 0.0, %entry ], [ %sum.next, %body ]
  %count = phi i32 [ 0, %entry ], [ %count.next, %body ]
  %more = icmp ult i64 %i, 8
  br i1 %more, label %body, label %exit

body:
  %p = getelementptr inbounds float, ptr %values, i64 %i
  %v = load float, ptr %p
  %sum.next = fadd float %sum, %v
  %count.next = add nsw i32 %count, 1
  %i.next = add nuw nsw i64 %i, 1
  br label %head

exit:
  ret float %sum
}

define float @total_vortex_rules(ptr %values) {
entry:
  br label %head

head:
  %i = phi i64 [ 0, %entry ], [ %i.next, %step ]
  %sum = phi float [ 0.0, %entry ], [ %sum.next, %step ]
  %count = phi i32 [ 0, %entry ], [ %count.next, %step ]
  %more = icmp ult i64 %i, 8
  br i1 %more, label %body, label %exit

body:
  %p = getelementptr inbounds float, ptr %values, i64 %i
  %v = load float, ptr %p
  %sum.next = fadd float %sum, %v
  %checked = call { i32, i1 } @llvm.sadd.with.overflow.i32(i32 %count, i32 1)
  %count.next = extractvalue { i32, i1 } %checked, 0
  %overflow = extractvalue { i32, i1 } %checked, 1
  br i1 %overflow, label %fail, label %step

step:
  %i.next = add nuw nsw i64 %i, 1
  br label %head

fail:
  call void @report_overflow(i32 %count)
  unreachable

exit:
  ret float %sum
}

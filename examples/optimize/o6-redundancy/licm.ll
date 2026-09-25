; Loop-invariant code motion by LLVM's LICM pass. Each loop is in rotated
; form: the entry tests n > 0 once, and the body, which is also the header,
; runs at least once, so everything in it is guaranteed to execute.
;
; @scale_row computes out[j] = src[row * n + j] * factor[0]. The product
; row * n and the load of factor[0] give the same result on every trip. The
; multiplication cannot fail, so it moves to the new preheader. The load
; moves too, because %out is noalias: the store to out[j] cannot change
; factor[0].
;
; @scale_row_may_alias is the same loop without noalias. The multiplication
; still moves, but the load stays: the store to out[j] might write factor[0].
;
; @sum_into adds x[0..n-1] into acc[0]. Nothing else in the loop can touch
; acc[0], so LICM keeps the running sum in a register: one load before the
; loop, one store after it. The additions happen in the same order, one at a
; time, so every result bit is unchanged.
;
; Follows: LLVM's pass list (licm, loop-simplify) and LICM.cpp, release/18.x
; (file header and isSafeToExecuteUnconditionally).

define void @scale_row(ptr noalias %out, ptr %src, ptr %factor, i64 %row, i64 %n) {
entry:
  %any = icmp sgt i64 %n, 0
  br i1 %any, label %loop, label %done

loop:
  %j = phi i64 [ 0, %entry ], [ %j.next, %loop ]
  %base = mul i64 %row, %n
  %idx = add i64 %base, %j
  %p = getelementptr float, ptr %src, i64 %idx
  %x = load float, ptr %p
  %f = load float, ptr %factor
  %y = fmul float %x, %f
  %q = getelementptr float, ptr %out, i64 %j
  store float %y, ptr %q
  %j.next = add i64 %j, 1
  %more = icmp slt i64 %j.next, %n
  br i1 %more, label %loop, label %done

done:
  ret void
}

define void @scale_row_may_alias(ptr %out, ptr %src, ptr %factor, i64 %row, i64 %n) {
entry:
  %any = icmp sgt i64 %n, 0
  br i1 %any, label %loop, label %done

loop:
  %j = phi i64 [ 0, %entry ], [ %j.next, %loop ]
  %base = mul i64 %row, %n
  %idx = add i64 %base, %j
  %p = getelementptr float, ptr %src, i64 %idx
  %x = load float, ptr %p
  %f = load float, ptr %factor
  %y = fmul float %x, %f
  %q = getelementptr float, ptr %out, i64 %j
  store float %y, ptr %q
  %j.next = add i64 %j, 1
  %more = icmp slt i64 %j.next, %n
  br i1 %more, label %loop, label %done

done:
  ret void
}

define void @sum_into(ptr noalias %acc, ptr %x, i64 %n) {
entry:
  %any = icmp sgt i64 %n, 0
  br i1 %any, label %loop, label %done

loop:
  %k = phi i64 [ 0, %entry ], [ %k.next, %loop ]
  %p = getelementptr float, ptr %x, i64 %k
  %v = load float, ptr %p
  %old = load float, ptr %acc
  %new = fadd float %old, %v
  store float %new, ptr %acc
  %k.next = add i64 %k, 1
  %more = icmp slt i64 %k.next, %n
  br i1 %more, label %loop, label %done

done:
  ret void
}

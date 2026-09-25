; Follows: LLVM, "Convergent Operation Semantics", the subgroupAdd example.
;
; The same function twice. Each arm of a branch calls the same function with
; the same argument, which invites SimplifyCFG to hoist the two calls above
; the branch and delete the branch. @lane_sum is an ordinary function, so it
; does. @wave_sum is marked convergent, standing for an operation that
; combines values across the lanes that execute it together (a subgroup or
; warp sum). Hoisting it would make every lane join one sum instead of two
; separate sums, one per arm, so SimplifyCFG leaves @wave alone.
declare i32 @lane_sum(i32) nounwind willreturn memory(none)
declare i32 @wave_sum(i32) convergent nounwind willreturn memory(none)

define i32 @plain(i32 %d) {
entry:
  %pos = icmp sgt i32 %d, 0
  br i1 %pos, label %gains, label %losses
gains:
  %g = call i32 @lane_sum(i32 %d)
  br label %end
losses:
  %l = call i32 @lane_sum(i32 %d)
  br label %end
end:
  %r = phi i32 [ %g, %gains ], [ %l, %losses ]
  ret i32 %r
}

define i32 @wave(i32 %d) {
entry:
  %pos = icmp sgt i32 %d, 0
  br i1 %pos, label %gains, label %losses
gains:
  %g = call i32 @wave_sum(i32 %d)
  br label %end
losses:
  %l = call i32 @wave_sum(i32 %d)
  br label %end
end:
  %r = phi i32 [ %g, %gains ], [ %l, %losses ]
  ret i32 %r
}

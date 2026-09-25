; Follows: LLVM, StructurizeCFG.cpp (the pass's own description of the shape
; it produces).
;
; A two-way branch on a per-lane value, the shape a divergent "if" compiles
; to: some lanes want %then, some want %else, and both rejoin at %merge.
; StructurizeCFG rewrites it into the nested form AMDGPU's back end needs
; before it can run both arms one after the other under an execution mask:
; the choice between the arms becomes a boolean that flows through a new
; %Flow block.
define i32 @lane_select(i32 %lane) {
entry:
  %take_then = icmp sgt i32 %lane, 0
  br i1 %take_then, label %then, label %else
then:
  br label %merge
else:
  br label %merge
merge:
  %r = phi i32 [ 10, %then ], [ 20, %else ]
  ret i32 %r
}

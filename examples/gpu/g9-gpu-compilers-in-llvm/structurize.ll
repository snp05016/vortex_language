; A two-way branch on a per-lane condition, the shape a divergent "if" in a
; GPU kernel compiles down to: some lanes take %then, some take %else, and
; both sides rejoin at %merge. -passes=structurizecfg rewrites this into the
; single-entry, single-exit nesting that a target with structured control
; flow (SPIR-V) requires, turning the branch into a boolean predicate that
; flows through a new %Flow block instead.
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

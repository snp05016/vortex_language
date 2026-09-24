; Partial redundancy elimination by LLVM's GVN pass. The sum %a + %b is
; computed on the path through %left and again at %join, so the one at %join
; is partially redundant: redundant when control comes from %left, not when
; it comes straight from %entry.
;
; GVN inserts a copy of the addition on the edge from %entry to %join, which
; makes the addition at %join fully redundant, and replaces it with a phi.
; That edge is critical (%entry has two successors and %join two
; predecessors), so no block lies on it; GVN splits it first, creating
; %entry.join_crit_edge. Afterwards every path computes the sum exactly once.
;
; Follows: LLVM's pass list (gvn) and GVN.cpp, release/18.x (performScalarPRE).

declare void @use(i32)

define i32 @diamond(i1 %c, i32 %a, i32 %b) {
entry:
  br i1 %c, label %left, label %join

left:
  %x = add i32 %a, %b
  call void @use(i32 %x)
  br label %join

join:
  %y = add i32 %a, %b
  ret i32 %y
}

; classify branches on its argument; caller always passes the constant 10.
; Neither fact is useful alone: instcombine cannot fold a branch whose
; condition depends on a parameter, and inlining by itself only copies code.
; Put them in one pipeline and the second pass sees what the first exposed:
; once classify's body is inlined into caller, %x is no longer a parameter,
; it is the constant 10, and the inliner's own cleanup folds the branch away.
;
; Run: opt -passes='inline' phase_order.ll
;
; Follows: LLVM's New Pass Manager guide (IR unit hierarchy, PreservedAnalyses)
; and LLVM's Analysis and Transform Passes (the inline entry).

define internal i32 @classify(i32 %x) {
entry:
  %pos = icmp sgt i32 %x, 0
  br i1 %pos, label %then, label %else
then:
  %doubled = mul i32 %x, 2
  br label %join
else:
  %negated = sub i32 0, %x
  br label %join
join:
  %result = phi i32 [ %doubled, %then ], [ %negated, %else ]
  ret i32 %result
}

define i32 @caller() {
entry:
  %r = call i32 @classify(i32 10)
  ret i32 %r
}

; LLVM 18's EarlyCSE pass reuses a value only where the block that computed
; it dominates the block that computes it again.
;
; @reuse computes %x * %y in %entry and again in %then. Every path to %then
; passes through %entry, so the second multiply is replaced by the first and
; deleted. %then then adds 1 to the product, and %join adds 1 to the same
; product again, but the path %entry -> %join skips %then: %then does not
; dominate %join, so %join keeps its own add.
;
; The pass walks the dominator tree and keeps a table of the expressions
; computed so far, forgetting a block's entries when the walk leaves that
; block's subtree. %then and %join are both children of %entry in the tree, so
; nothing %then adds is visible in %join.
;
; Run: opt -S -passes=early-cse early_cse.ll
; Compare: opt -passes='print<domtree>' -disable-output early_cse.ll
;
; Follows: LLVM 18 EarlyCSE.cpp (file header), and the LLVM Language
; Reference, "Well-Formedness" (a definition must dominate its uses).

define i32 @reuse(i1 %c, i32 %x, i32 %y) {
entry:
  %a = mul i32 %x, %y
  br i1 %c, label %then, label %join

then:
  %b = mul i32 %x, %y                     ; same as %a; %entry dominates %then
  %s = add i32 %b, 1
  br label %join

join:
  %m = phi i32 [ %s, %then ], [ %a, %entry ]
  %t = add i32 %a, 1                      ; same as %s; %then does not dominate %join
  %r = mul i32 %m, %t
  ret i32 %r
}

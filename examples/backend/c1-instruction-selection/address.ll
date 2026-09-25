; The address tree from this chapter's C++ examples, as LLVM IR:
; element [i][j] of an i32 array whose rows hold 4 elements.
; The getelementptr scales its index by the element size (4 bytes), which
; plays the part of the tree's "<< 2". Compile it to see which tiles LLVM's
; selector picks:
;   llc -O2 address.ll -o -                                   (host target)
;   llc -O2 -mtriple=x86_64-unknown-linux-gnu address.ll -o - (x86-64)
; Follows: LLVM Language Reference Manual, "getelementptr" and "load".

define i32 @element(ptr %a, i64 %i, i64 %j) {
  %row = mul i64 %i, 4
  %index = add i64 %row, %j
  %p = getelementptr i32, ptr %a, i64 %index
  %v = load i32, ptr %p, align 4
  ret i32 %v
}

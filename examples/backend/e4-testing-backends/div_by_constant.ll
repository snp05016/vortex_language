; A regression test in the style of LLVM's own llc tests. The RUN lines are
; for lit; the CHECK lines are for FileCheck. Both are comments to llc.
;
; RUN: llc -O2 -mtriple=aarch64-linux-gnu < %s | FileCheck %s
; RUN: llc -O2 -mtriple=arm64-apple-macos < %s | FileCheck %s

; Unsigned division by 7 must not use the divide instruction: the back end
; should multiply by a "magic" reciprocal and keep the high half instead.
; The CHECK-NOT lines sit on both sides of the multiply, because a CHECK-NOT
; only covers the gap between the matches around it.
define i32 @div7(i32 %x) {
; CHECK-LABEL: div7:
; CHECK-NOT:     udiv
; CHECK:         umull [[PROD:x[0-9]+]], w0, {{w[0-9]+}}
; CHECK-NEXT:    lsr {{x[0-9]+}}, [[PROD]], #32
; CHECK-NOT:     udiv
; CHECK:         ret
  %q = udiv i32 %x, 7
  ret i32 %q
}

; A clamp needs no branch. The pattern names the temporary register [[T]]
; once and then demands that the same register is used afterwards, without
; saying which register the allocator must pick.
define i32 @clamp(i32 %x, i32 %lo, i32 %hi) {
; CHECK-LABEL: clamp:
; CHECK:         cmp w0, w1
; CHECK-NEXT:    csel [[T:w[0-9]+]], w1, w0, lt
; CHECK-NEXT:    cmp [[T]], w2
; CHECK-NEXT:    csel w0, w2, [[T]], gt
; CHECK-NEXT:    ret
  %below = icmp slt i32 %x, %lo
  %a = select i1 %below, i32 %lo, i32 %x
  %above = icmp sgt i32 %a, %hi
  %r = select i1 %above, i32 %hi, i32 %a
  ret i32 %r
}

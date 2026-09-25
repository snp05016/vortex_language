; A front end's shortcut to SSA: give each mutable variable a stack slot (an
; alloca in the entry block), read it with loads and write it with stores, and
; let LLVM's mem2reg pass build the SSA form, phis included.
;
; @sum_below adds 0 + 1 + ... + (n - 1) into %total. mem2reg promotes %i and
; %total, placing one phi for each at the loop header %test. It leaves %seen in
; memory: its address is passed to @update, so a call could read or write it,
; and mem2reg promotes only slots whose uses are direct loads and stores.
;
; Run: opt -S -passes=mem2reg promote.ll
;
; Follows: LLVM Kaleidoscope tutorial, chapter 7, section 7.3 (the conditions
; for mem2reg); LLVM Frontend Performance Tips, "Use of allocas".

declare void @update(ptr)

define i32 @sum_below(i32 %n) {
entry:
  %i = alloca i32
  %total = alloca i32
  %seen = alloca i32
  store i32 0, ptr %i
  store i32 0, ptr %total
  store i32 0, ptr %seen
  call void @update(ptr %seen)
  br label %test

test:
  %i.now = load i32, ptr %i
  %more = icmp slt i32 %i.now, %n
  br i1 %more, label %body, label %done

body:
  %t.old = load i32, ptr %total
  %i.old = load i32, ptr %i
  %t.new = add i32 %t.old, %i.old
  store i32 %t.new, ptr %total
  %i.new = add i32 %i.old, 1
  store i32 %i.new, ptr %i
  br label %test

done:
  %result = load i32, ptr %total
  %extra = load i32, ptr %seen
  %answer = add i32 %result, %extra
  ret i32 %answer
}

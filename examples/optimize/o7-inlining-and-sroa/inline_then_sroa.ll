; Inlining first, then SROA. @walk keeps a 2D position in a stack slot and
; moves it with @add, which takes its operands and its result by address, the
; way a front end passes struct values. While the call is there, the slots'
; addresses escape into it and SROA must leave them alone. Inlining puts
; @add's loads and stores into @walk, every use of the slots becomes visible,
; and SROA turns the slots into values: the loop carries x and y as two phis.
;
; Run: opt -S -passes=inline,sroa inline_then_sroa.ll
; With -passes=sroa alone, only %i is promoted: the three struct slots and the
; call stay as they are.
;
; The noalias.scope.decl calls in the output are what is left of the noalias
; promises on @add's parameters: the inliner turned each one into a scope.
;
; Follows: LLVM 18 InlineFunction.cpp (static allocas moved to the caller's
; entry block, AddAliasScopeMetadata) and SROA.cpp; LangRef, "'noalias' and
; 'alias.scope' Metadata".

%Vec2 = type { float, float }

define internal void @add(ptr noalias sret(%Vec2) %out,
                          ptr noalias readonly %a, ptr noalias readonly %b) {
  %a.x = getelementptr inbounds %Vec2, ptr %a, i32 0, i32 0
  %a.y = getelementptr inbounds %Vec2, ptr %a, i32 0, i32 1
  %b.x = getelementptr inbounds %Vec2, ptr %b, i32 0, i32 0
  %b.y = getelementptr inbounds %Vec2, ptr %b, i32 0, i32 1
  %ax = load float, ptr %a.x
  %ay = load float, ptr %a.y
  %bx = load float, ptr %b.x
  %by = load float, ptr %b.y
  %x = fadd float %ax, %bx
  %y = fadd float %ay, %by
  %out.x = getelementptr inbounds %Vec2, ptr %out, i32 0, i32 0
  %out.y = getelementptr inbounds %Vec2, ptr %out, i32 0, i32 1
  store float %x, ptr %out.x
  store float %y, ptr %out.y
  ret void
}

define float @walk(i32 %steps) {
entry:
  %pos = alloca %Vec2
  %step = alloca %Vec2
  %next = alloca %Vec2
  %i = alloca i32
  %pos.x = getelementptr inbounds %Vec2, ptr %pos, i32 0, i32 0
  %pos.y = getelementptr inbounds %Vec2, ptr %pos, i32 0, i32 1
  %step.x = getelementptr inbounds %Vec2, ptr %step, i32 0, i32 0
  %step.y = getelementptr inbounds %Vec2, ptr %step, i32 0, i32 1
  store float 0.000000e+00, ptr %pos.x
  store float 0.000000e+00, ptr %pos.y
  store float 1.000000e+00, ptr %step.x
  store float 5.000000e-01, ptr %step.y
  store i32 0, ptr %i
  br label %test
test:
  %iv = load i32, ptr %i
  %more = icmp slt i32 %iv, %steps
  br i1 %more, label %body, label %done
body:
  call void @add(ptr sret(%Vec2) %next, ptr %pos, ptr %step)
  call void @llvm.memcpy.p0.p0.i64(ptr %pos, ptr %next, i64 8, i1 false)
  %iv.next = add nsw i32 %iv, 1
  store i32 %iv.next, ptr %i
  br label %test
done:
  %fx = load float, ptr %pos.x
  %fy = load float, ptr %pos.y
  %sum = fadd float %fx, %fy
  ret float %sum
}

declare void @llvm.memcpy.p0.p0.i64(ptr, ptr, i64, i1)

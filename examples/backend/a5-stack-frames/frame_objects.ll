; Stack objects in LLVM IR carry a size and an alignment, never an offset.
; Where each one lands in the frame is decided by the back end, after
; register allocation, when it knows everything else the frame must hold.
;
; @mix has three small objects of mixed alignment, so the back end has to
; pad between them. @tile has one 16 KiB object, the size of a 64 x 64 f32
; array, larger than the 4 KiB stack-probe interval LangRef gives as the
; default, so the "probe-stack" attribute makes the back end touch the new
; stack one 4 KiB step at a time instead of moving sp once.
; "frame-pointer"="non-leaf" asks for a frame record in every function that
; calls another, the rule Apple's platforms follow.
;
; opt only verifies this module; the chapter shows what `llc -O2` builds.
;
; Follows: LLVM Language Reference Manual, "'alloca' Instruction" and the
; "probe-stack", "stack-probe-size" and "frame-pointer" function attributes.

declare void @fill(ptr)

define i32 @mix(i32 %a, i32 %b, i32 %c) {
entry:
  %pair = alloca [2 x i32], align 4
  %single = alloca i32, align 4
  %wide = alloca i64, align 8
  %p1 = getelementptr inbounds [2 x i32], ptr %pair, i64 0, i64 1
  store i32 %a, ptr %pair, align 4
  store i32 %b, ptr %p1, align 4
  store i32 %c, ptr %single, align 4
  %c64 = sext i32 %c to i64
  store i64 %c64, ptr %wide, align 8
  %v0 = load i32, ptr %pair, align 4
  %v1 = load i32, ptr %p1, align 4
  %v2 = load i32, ptr %single, align 4
  %v3 = load i64, ptr %wide, align 8
  %v3lo = trunc i64 %v3 to i32
  %s0 = add i32 %v0, %v1
  %s1 = add i32 %s0, %v2
  %s2 = add i32 %s1, %v3lo
  ret i32 %s2
}

define float @tile(i64 %i) "frame-pointer"="non-leaf" "probe-stack"="inline-asm" {
entry:
  %t = alloca [64 x [64 x float]], align 16
  call void @fill(ptr %t)
  %p = getelementptr inbounds float, ptr %t, i64 %i
  %v = load float, ptr %p, align 4
  ret float %v
}

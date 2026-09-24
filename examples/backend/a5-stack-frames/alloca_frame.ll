; Three local allocations of different sizes, the shape a front end emits
; before register allocation has decided where anything lives: a 2-element
; array, a single word and a doubleword, each an `alloca` with no address
; that escapes the function. Nothing here says where they end up in the
; frame, or in what order; that is the back end's decision, made only after
; register allocation knows which values could not stay in registers.
;
; `opt -passes=verify` only checks the module; it does not move the allocas.
; The chapter separately runs `llc -O2` on this file and quotes the frame
; the AArch64 back end actually builds for it.
;
; Follows: LLVM Language Reference, section "'alloca' Instruction"; AAPCS64,
; "Stack constraints at a public interface".

define i32 @mix(i32 %a, i32 %b, i32 %c) {
entry:
  %pair = alloca [2 x i32], align 4
  %single = alloca i32, align 4
  %wide = alloca i64, align 8
  %p0 = getelementptr [2 x i32], ptr %pair, i64 0, i64 0
  %p1 = getelementptr [2 x i32], ptr %pair, i64 0, i64 1
  store i32 %a, ptr %p0
  store i32 %b, ptr %p1
  store i32 %c, ptr %single
  %c64 = sext i32 %c to i64
  store i64 %c64, ptr %wide
  %v0 = load i32, ptr %p0
  %v1 = load i32, ptr %p1
  %v2 = load i32, ptr %single
  %v3 = load i64, ptr %wide
  %v3trunc = trunc i64 %v3 to i32
  %s0 = add i32 %v0, %v1
  %s1 = add i32 %s0, %v2
  %s2 = add i32 %s1, %v3trunc
  ret i32 %s2
}

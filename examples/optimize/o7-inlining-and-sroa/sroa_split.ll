; SROA, scalar replacement of aggregates, cuts a stack slot that holds a struct
; or an array into one slot for each piece its uses touch, then promotes each
; piece to an SSA value, as mem2reg would. It leaves a slot alone when it
; cannot see every use of the slot at a known offset.
;
; Run: opt -S -passes=sroa sroa_split.ll
;
;   @length_squared  A two-float struct written and read field by field. Both
;                    fields become plain values and the slot disappears.
;   @copy_then_read  The struct is copied into a second slot with memcpy, the
;                    way a front end copies a value. The copy is split too.
;   @pick            A four-float array read at an index known only at run
;                    time. No offset is known, so the array stays in memory.
;   @shown           The struct's address is passed to a function that might
;                    keep it or write through it, so the struct stays.
;
; Follows: LLVM 18 SROA.cpp (the file header, AllocaSlices and SliceBuilder);
; LLVM's pass documentation (sroa); Frontend Performance Tips (allocas).

%Vec2 = type { float, float }

declare void @inspect(ptr)
declare void @llvm.memcpy.p0.p0.i64(ptr, ptr, i64, i1)

define float @length_squared(float %x, float %y) {
entry:
  %v = alloca %Vec2
  %v.x = getelementptr inbounds %Vec2, ptr %v, i32 0, i32 0
  %v.y = getelementptr inbounds %Vec2, ptr %v, i32 0, i32 1
  store float %x, ptr %v.x
  store float %y, ptr %v.y
  %a = load float, ptr %v.x
  %b = load float, ptr %v.y
  %aa = fmul float %a, %a
  %bb = fmul float %b, %b
  %sum = fadd float %aa, %bb
  ret float %sum
}

define float @copy_then_read(float %x, float %y) {
entry:
  %v = alloca %Vec2
  %w = alloca %Vec2
  %v.x = getelementptr inbounds %Vec2, ptr %v, i32 0, i32 0
  %v.y = getelementptr inbounds %Vec2, ptr %v, i32 0, i32 1
  store float %x, ptr %v.x
  store float %y, ptr %v.y
  call void @llvm.memcpy.p0.p0.i64(ptr %w, ptr %v, i64 8, i1 false)
  %w.y = getelementptr inbounds %Vec2, ptr %w, i32 0, i32 1
  %r = load float, ptr %w.y
  ret float %r
}

define float @pick(i64 %i) {
entry:
  %a = alloca [4 x float]
  %a.1 = getelementptr inbounds [4 x float], ptr %a, i64 0, i64 1
  %a.2 = getelementptr inbounds [4 x float], ptr %a, i64 0, i64 2
  %a.3 = getelementptr inbounds [4 x float], ptr %a, i64 0, i64 3
  store float 1.000000e+00, ptr %a
  store float 2.000000e+00, ptr %a.1
  store float 3.000000e+00, ptr %a.2
  store float 4.000000e+00, ptr %a.3
  %a.i = getelementptr inbounds [4 x float], ptr %a, i64 0, i64 %i
  %r = load float, ptr %a.i
  ret float %r
}

define float @shown(float %x, float %y) {
entry:
  %v = alloca %Vec2
  %v.x = getelementptr inbounds %Vec2, ptr %v, i32 0, i32 0
  %v.y = getelementptr inbounds %Vec2, ptr %v, i32 0, i32 1
  store float %x, ptr %v.x
  store float %y, ptr %v.y
  call void @inspect(ptr %v)
  %r = load float, ptr %v.x
  ret float %r
}

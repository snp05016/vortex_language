; Store-to-load forwarding asks one alias question. Each function stores 1,
; then stores 2 somewhere else, then loads from where the 1 went. The load can
; become the constant 1 only if the second store cannot write any of the
; loaded bytes.
;
; EarlyCSE, run with MemorySSA, asks MemorySSA's walker for the load's
; clobber: the nearest store above it that may write the loaded bytes. If
; that store is the one that wrote 1, the load is replaced.
;
;   @unknown     two pointer parameters: they may alias, the load stays
;   @neighbours  a[0] and a[1], 4 bytes each: no alias, by offsets and sizes
;   @local       a stack slot whose address never escapes: no alias
;   @promised    %p is noalias: the caller promises no overlap
;   @overlap     bytes 0-3 and bytes 2-3: they partly alias, the load stays
;
; Run: opt -S -passes='early-cse<memssa>' forwarding.ll
;
; Follows: LLVM Alias Analysis Infrastructure (the alias responses and the
; basic-aa facts); EarlyCSE.cpp, release/18.x (isSameMemGeneration).

define i32 @unknown(ptr %p, ptr %q) {
  store i32 1, ptr %p
  store i32 2, ptr %q
  %v = load i32, ptr %p
  ret i32 %v
}

define i32 @neighbours(ptr %a) {
  %second = getelementptr inbounds i32, ptr %a, i64 1
  store i32 1, ptr %a
  store i32 2, ptr %second
  %v = load i32, ptr %a
  ret i32 %v
}

define i32 @local(ptr %q) {
  %slot = alloca i32
  store i32 1, ptr %slot
  store i32 2, ptr %q
  %v = load i32, ptr %slot
  ret i32 %v
}

define i32 @promised(ptr noalias %p, ptr %q) {
  store i32 1, ptr %p
  store i32 2, ptr %q
  %v = load i32, ptr %p
  ret i32 %v
}

define i32 @overlap(ptr %a) {
  %middle = getelementptr inbounds i8, ptr %a, i64 2
  store i32 1, ptr %a
  store i16 2, ptr %middle
  %v = load i32, ptr %a
  ret i32 %v
}

; caller calls internal_helper, defined in this same module; internal_helper
; calls external_sink, declared but defined elsewhere. Both calls compile to
; a `bl` to a symbol. Assemble this and disassemble the object file (see the
; page) to see which calls the MC layer resolves immediately and which it
; leaves as a fixup for the linker.
;
; Follows: LLVM Language Reference Manual, "call" instruction
; (https://llvm.org/docs/LangRef.html#call-instruction).

declare void @external_sink(i32)

define void @internal_helper(i32 %x) {
entry:
  call void @external_sink(i32 %x)
  ret void
}

define void @caller(i32 %n) {
entry:
  call void @internal_helper(i32 %n)
  ret void
}

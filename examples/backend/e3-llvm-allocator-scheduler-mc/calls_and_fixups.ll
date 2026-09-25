; One branch and three calls, each with a target the assembler knows about
; to a different degree:
;   - the cbz in caller jumps to a label in the same function;
;   - local_helper has internal linkage: defined here, invisible outside;
;   - shared_helper is defined here but global, so other files may call it;
;   - external_sink is only declared: its address comes from another file.
; The MC layer encodes every one of them with a fixup. The page shows which
; fixups the assembler resolves itself and which it leaves in the object
; file as relocations, for Mach-O and for ELF.
;
; Follows: LLVM, "The LLVM Target-Independent Code Generator", "The MC
; Layer" (https://llvm.org/docs/CodeGenerator.html#the-mc-layer), and
; AAELF64, relocation R_AARCH64_CALL26
; (https://github.com/ARM-software/abi-aa/blob/main/aaelf64/aaelf64.rst).

declare void @external_sink(i32)

define internal void @local_helper(i32 %x) {
entry:
  call void @external_sink(i32 %x)
  ret void
}

define void @shared_helper(i32 %x) {
entry:
  call void @external_sink(i32 %x)
  ret void
}

define void @caller(i32 %n) {
entry:
  %skip = icmp eq i32 %n, 0
  br i1 %skip, label %done, label %work

work:
  call void @local_helper(i32 %n)
  call void @shared_helper(i32 %n)
  br label %done

done:
  ret void
}

; Follows: LLVM, "Source Level Debugging with LLVM" (debug records and
; metadata), https://llvm.org/docs/SourceLevelDebugging.html
;
; A toy calculator language compiled `twice(x) = x + x` on line 3 of
; calc.toy. The code is ordinary IR; everything after `!dbg` is metadata
; that the code generator turns into .loc directives and DWARF DIEs.
; Deleting the !dbg attachments and the metadata leaves the same machine code
; for this function (compare llc output with and without them).

define i32 @twice(i32 %x) !dbg !4 {
entry:
  %sum = add i32 %x, %x, !dbg !7
  ret i32 %sum, !dbg !8
}

!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!2, !3}

!0 = distinct !DICompileUnit(language: DW_LANG_C11, file: !1, producer: "toy calc", isOptimized: false, runtimeVersion: 0, emissionKind: LineTablesOnly)
!1 = !DIFile(filename: "calc.toy", directory: "/toy")
!2 = !{i32 7, !"Dwarf Version", i32 5}
!3 = !{i32 2, !"Debug Info Version", i32 3}
!4 = distinct !DISubprogram(name: "twice", scope: !1, file: !1, line: 3, type: !5, scopeLine: 3, spFlags: DISPFlagDefinition, unit: !0)
!5 = !DISubroutineType(types: !6)
!6 = !{}
!7 = !DILocation(line: 3, column: 14, scope: !4)
!8 = !DILocation(line: 3, column: 1, scope: !4)

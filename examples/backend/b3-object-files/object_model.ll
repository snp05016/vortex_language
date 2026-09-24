; A tiny "event counter" module: one global variable, one defined function,
; one external function it calls. It is not part of Vortex; it exists only
; to show what an object file's symbol table must record.
;
; Once assembled, this module needs exactly three symbol-table entries:
;   @event_count      defined, in a data section (it has an initial value)
;   @bump_and_report   defined, in the text section (it is code)
;   @report            undefined: a hole for the linker to fill, the same
;                       kind of hole `bl _vx_log_i32` leaves in relocations.s
;
; Follows: LLVM Language Reference Manual, "Global Variables" and
; "Functions" (https://llvm.org/docs/LangRef.html#global-variables,
; https://llvm.org/docs/LangRef.html#functions).

@event_count = global i32 0

declare void @report(i32)

define void @bump_and_report() {
entry:
  %old = load i32, ptr @event_count
  %new = add i32 %old, 1
  store i32 %new, ptr @event_count
  call void @report(i32 %new)
  ret void
}

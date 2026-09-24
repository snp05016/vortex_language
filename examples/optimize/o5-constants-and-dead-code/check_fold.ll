; A lookup helper after inlining, in the shape a Vortex compiler that places
; checks inline might give it: element(&values, false) copied into its caller.
;
;   fn element(values: &[f32; 64], from_end: bool) -> f32 {
;       let mut index = 0;
;       if from_end { index = 63; }
;       return values[index];
;   }
;
; The bounds check is an ordinary branch. Compared as unsigned numbers, one
; comparison covers both halves of 0 <= index < 64, because a negative i32 reads
; as a large unsigned value. A failure calls the runtime's report, which never
; returns.
;
; SCCP marks edges executable only when a branch can take them. The branch on
; the constant false takes one edge, so the phi sees only 0; the check then
; compares 0 with 64 and takes one edge too. The unused block, the phi, the
; check and the report are deleted, and the load reads element 0.
;
; Follows: LLVM's pass list (sccp), and Wegman and Zadeck (1991), section 3.4.

declare void @report_bounds(i32, i32) noreturn

define float @first(ptr %values) {
entry:
  br i1 false, label %from_end, label %join

from_end:
  br label %join

join:
  %index = phi i32 [ 0, %entry ], [ 63, %from_end ]
  %in_bounds = icmp ult i32 %index, 64
  br i1 %in_bounds, label %read, label %fail

read:
  %offset = zext i32 %index to i64
  %p = getelementptr inbounds float, ptr %values, i64 %offset
  %x = load float, ptr %p
  ret float %x

fail:
  call void @report_bounds(i32 %index, i32 64)
  unreachable
}

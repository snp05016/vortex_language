; A three-way clamp: one comparison feeding a select, then a second
; comparison feeding a second select. Small enough to read as one
; instruction selection problem, general enough to be a real building
; block (bounds checks are also compare-then-select).
define i32 @clampi32(i32 %x, i32 %lo, i32 %hi) {
entry:
  %below = icmp slt i32 %x, %lo
  %raised = select i1 %below, i32 %lo, i32 %x
  %above = icmp sgt i32 %raised, %hi
  %clamped = select i1 %above, i32 %hi, i32 %raised
  ret i32 %clamped
}

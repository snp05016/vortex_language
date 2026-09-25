; Clamp x into [lo, hi]: a comparison feeding a select, then a second
; comparison feeding a second select. Small enough to follow through every
; stage of llc by hand, and branch-free, so each stage shows one block.
define i32 @clampi32(i32 %x, i32 %lo, i32 %hi) {
entry:
  %below = icmp slt i32 %x, %lo
  %raised = select i1 %below, i32 %lo, i32 %x
  %above = icmp sgt i32 %raised, %hi
  %clamped = select i1 %above, i32 %hi, i32 %raised
  ret i32 %clamped
}

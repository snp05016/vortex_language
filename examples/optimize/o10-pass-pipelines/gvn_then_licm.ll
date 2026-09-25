; Two passes, first order: GVN, then LICM. licm_then_gvn.ll holds the same
; function and runs the same two passes the other way round.
;
; row_total adds up row %row of a table 64 columns wide, then writes the total
; into the row's first cell. %row * 64 is computed twice: in the loop body,
; where it gives the same value on every trip, and after the loop.
;
; GVN runs first. The body does not dominate the exit, because the loop may
; run zero times, so %start.again is not redundant on every path and GVN
; changes nothing. LICM runs second and hoists %start into entry, the block
; that runs once before the loop. Nothing runs after LICM, so the exit keeps
; its own multiplication. The loop pass adaptor also put the loop into LCSSA
; form on the way in, which is where %total.lcssa comes from.
;
; Follows: LLVM's New Pass Manager guide (Invoking opt) and LLVM's Analysis
; and Transform Passes (gvn, licm).

define void @row_total(ptr %table, i32 %row, i32 %n) {
entry:
  br label %header
header:
  %col = phi i32 [ 0, %entry ], [ %col.next, %body ]
  %total = phi i32 [ 0, %entry ], [ %total.next, %body ]
  %more = icmp slt i32 %col, %n
  br i1 %more, label %body, label %exit
body:
  %start = mul i32 %row, 64
  %index = add i32 %start, %col
  %cell = getelementptr i32, ptr %table, i32 %index
  %value = load i32, ptr %cell
  %total.next = add i32 %total, %value
  %col.next = add i32 %col, 1
  br label %header
exit:
  %start.again = mul i32 %row, 64
  %first = getelementptr i32, ptr %table, i32 %start.again
  store i32 %total, ptr %first
  ret void
}

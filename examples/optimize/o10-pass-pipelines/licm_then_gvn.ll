; Two passes, second order: LICM, then GVN. gvn_then_licm.ll holds the same
; function and runs the same two passes the other way round.
;
; LICM runs first and hoists %start = %row * 64 into entry. Entry dominates
; every block of the function, the exit included, so when GVN runs second,
; %start.again computes a value that every path to it has already computed:
; it is fully redundant, and GVN replaces it with %start. GVN also folds the
; one-input phi that LCSSA form added at the exit. One multiplication is
; left instead of two, from the same two passes.
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

; A pass test: four divisions by 2 go through InstCombine, and the checked
; output pins down which ones may become shifts. Follows: LLVM Language
; Reference, "sdiv", "udiv", "ashr" and "lshr" (see .toml).

; x could be negative and odd, so sdiv must stay: -3 / 2 is -1, -3 >> 1 is -2.
define i32 @half(i32 %x) {
  %q = sdiv i32 %x, 2
  ret i32 %q
}

; With "exact", a division that would round gives poison, so the shift only
; has to agree on the inputs where nothing rounds.
define i32 @half_exact(i32 %x) {
  %q = sdiv exact i32 %x, 2
  ret i32 %q
}

; After the mask, %m is between 0 and 1023, never negative.
define i32 @half_masked(i32 %x) {
  %m = and i32 %x, 1023
  %q = sdiv i32 %m, 2
  ret i32 %q
}

; Unsigned division by 2 is always a logical shift right.
define i32 @half_unsigned(i32 %x) {
  %q = udiv i32 %x, 2
  ret i32 %q
}

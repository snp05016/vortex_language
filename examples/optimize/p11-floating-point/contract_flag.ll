; Whether a multiply and an add may fuse into one rounding step is controlled
; entirely by the `contract` fast-math flag on the fadd: not by the
; optimization level, and not by any default the backend chooses on its own.
;
; @strict keeps the multiply and the add as two separate, independently
; rounded operations. @contracted marks the add `contract`, which permits
; (but does not force) fusing it with the multiply that feeds it into one
; fused multiply-add, with a single rounding.
;
; Run: opt -S -passes=verify contract_flag.ll
; (then llc -O2: @strict lowers to fmul+fadd, @contracted to one fmadd)
;
; Follows: LLVM Language Reference, "Fast-Math Flags" (the `contract` flag).
; <https://llvm.org/docs/LangRef.html#fast-math-flags>

define float @strict(float %a, float %b, float %c) {
  %p = fmul float %a, %b
  %s = fadd float %p, %c
  ret float %s
}

define float @contracted(float %a, float %b, float %c) {
  %p = fmul float %a, %b
  %s = fadd contract float %p, %c
  ret float %s
}

; Four independent, isomorphic scalar additions in straight-line code, with
; no loop: r[k] = a[k] + b[k] for k = 0 to 3. The loop vectorizer has nothing
; to widen here. The SLP (superword-level parallelism) vectorizer looks for
; statements that apply the same operations in the same order, starting from
; loads and stores to adjacent addresses, and packs them into one vector
; operation.
;
; Run: opt -S -passes=slp-vectorizer slp_pack.ll
;
; Follows: Larsen and Amarasinghe, "Exploiting Superword Level Parallelism
; with Multimedia Instruction Sets", sections 2.1 and 3 (isomorphic
; statements, adjacent memory references as seeds); LLVM, "Auto-Vectorization
; in LLVM", section "The SLP Vectorizer". The function is original.

target triple = "arm64-apple-macosx"

define void @add_point4(ptr noalias %r, ptr noalias readonly %a, ptr noalias readonly %b) {
entry:
  %a0p = getelementptr inbounds float, ptr %a, i64 0
  %a1p = getelementptr inbounds float, ptr %a, i64 1
  %a2p = getelementptr inbounds float, ptr %a, i64 2
  %a3p = getelementptr inbounds float, ptr %a, i64 3
  %b0p = getelementptr inbounds float, ptr %b, i64 0
  %b1p = getelementptr inbounds float, ptr %b, i64 1
  %b2p = getelementptr inbounds float, ptr %b, i64 2
  %b3p = getelementptr inbounds float, ptr %b, i64 3
  %a0 = load float, ptr %a0p, align 4
  %a1 = load float, ptr %a1p, align 4
  %a2 = load float, ptr %a2p, align 4
  %a3 = load float, ptr %a3p, align 4
  %b0 = load float, ptr %b0p, align 4
  %b1 = load float, ptr %b1p, align 4
  %b2 = load float, ptr %b2p, align 4
  %b3 = load float, ptr %b3p, align 4
  %r0 = fadd float %a0, %b0
  %r1 = fadd float %a1, %b1
  %r2 = fadd float %a2, %b2
  %r3 = fadd float %a3, %b3
  %r0p = getelementptr inbounds float, ptr %r, i64 0
  %r1p = getelementptr inbounds float, ptr %r, i64 1
  %r2p = getelementptr inbounds float, ptr %r, i64 2
  %r3p = getelementptr inbounds float, ptr %r, i64 3
  store float %r0, ptr %r0p, align 4
  store float %r1, ptr %r1p, align 4
  store float %r2, ptr %r2p, align 4
  store float %r3, ptr %r3p, align 4
  ret void
}

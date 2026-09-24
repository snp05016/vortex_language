; A stand-in "kernel" body: one pointer into device global memory
; (addrspace 1, the NVPTX convention for global memory) and one already
; staged into on-chip shared memory (addrspace 3). @barrier is declared
; convergent: LLVM must not move this call across a branch that only some
; lanes take, whichever pass runs next.
declare void @barrier() convergent

define i32 @stage_and_combine(ptr addrspace(1) %g, ptr addrspace(3) %s) {
entry:
  %gv = load i32, ptr addrspace(1) %g
  store i32 %gv, ptr addrspace(3) %s
  call void @barrier()
  %sv = load i32, ptr addrspace(3) %s
  %sum = add i32 %gv, %sv
  ret i32 %sum
}

; Follows: LLVM Language Reference Manual, "Pointer Type" and "Data Layout";
; LLVM, "User Guide for AMDGPU Backend", "Address Spaces".
;
; Two pointers into two memories. On AMDGPU, address space 1 is global memory
; and address space 3 is local memory (LDS, the on-chip scratchpad a work-group
; shares), and a local address is 32 bits wide. The data layout string below
; carries only that last fact, "p3:32:32": pointers in address space 3 are 32
; bits. instcombine reads it and does address arithmetic on the shared pointer
; in 32 bits, while the global pointer keeps 64. The IR never says "global" or
; "shared"; the number, the data layout and the back end say it together.
target datalayout = "p3:32:32"

define i64 @gap(ptr addrspace(1) %g, ptr addrspace(3) %s) {
  %gi = ptrtoint ptr addrspace(1) %g to i64
  %si = ptrtoint ptr addrspace(3) %s to i64
  %d = sub i64 %gi, %si
  ret i64 %d
}

define ptr addrspace(3) @element(ptr addrspace(3) %s, i64 %i) {
  %p = getelementptr inbounds float, ptr addrspace(3) %s, i64 %i
  ret ptr addrspace(3) %p
}

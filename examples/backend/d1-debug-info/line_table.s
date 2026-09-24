    .file 1 "clamp.c"
    .globl _clamp
    .p2align 2
_clamp:
    .loc 1 10 5
    cmp     w0, w1
    b.ge    1f
    .loc 1 11 9
    mov     w0, w1
    b       3f
1:
    .loc 1 13 5
    cmp     w0, w2
    b.le    2f
    .loc 1 14 9
    mov     w0, w2
    b       3f
2:
    .loc 1 16 5
3:
    ret

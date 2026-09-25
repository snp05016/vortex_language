// A toy-language function, expanded one template at a time by hand:
//
//     fn sum_to(n) { let s = 0; let i = 1; while i <= n { s = s + i; i = i + 1; } return s; }
//
// Every variable, constant and intermediate result has its own 4-byte slot:
//   0 n    1 s    2 i    3 const 0    4 const 1    5 i <= n
//   6 s + i    7 const 1    8 i + 1
// Nine slots are 36 bytes, rounded to a 48-byte frame. The loop is the
// while template: a label at the top, the condition's template, a branch
// out when the condition slot holds 0, the body, and a branch back.
// Label names get a number from a counter, so a second loop would get
// Lwhile_top1 and Lwhile_end1. Mach-O keeps names that start with L out of
// the symbol table; ELF uses a .L prefix for the same purpose.
//
// Follows: Ghuloum, "An Incremental Approach to Compiler Construction"
// (conditionals with fresh labels, variables in stack locations).

	.section	__TEXT,__text,regular,pure_instructions
	.globl	_sum_to
	.p2align	2
_sum_to:
	sub	sp, sp, #48
	str	w0, [sp, #0]         // the argument arrives in w0: store it at once

	mov	w0, #0               // let s = 0
	str	w0, [sp, #12]
	ldr	w0, [sp, #12]
	str	w0, [sp, #4]
	mov	w0, #1               // let i = 1
	str	w0, [sp, #16]
	ldr	w0, [sp, #16]
	str	w0, [sp, #8]

Lwhile_top0:
	ldr	w0, [sp, #8]         // i <= n -> slot 5, as 0 or 1
	ldr	w1, [sp, #0]
	cmp	w0, w1
	cset	w0, le
	str	w0, [sp, #20]
	ldr	w0, [sp, #20]        // leave the loop when the condition is 0
	cmp	w0, #0
	b.eq	Lwhile_end0

	ldr	w0, [sp, #4]         // s + i -> slot 6, then s = slot 6
	ldr	w1, [sp, #8]
	add	w0, w0, w1
	str	w0, [sp, #24]
	ldr	w0, [sp, #24]
	str	w0, [sp, #4]
	mov	w0, #1               // i + 1 -> slot 8, then i = slot 8
	str	w0, [sp, #28]
	ldr	w0, [sp, #8]
	ldr	w1, [sp, #28]
	add	w0, w0, w1
	str	w0, [sp, #32]
	ldr	w0, [sp, #32]
	str	w0, [sp, #8]
	b	Lwhile_top0

Lwhile_end0:
	ldr	w0, [sp, #4]         // return s
	add	sp, sp, #48
	ret

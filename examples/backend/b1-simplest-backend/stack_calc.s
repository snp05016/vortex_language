// (7 - 2) * 3 as the macro expander in macro_expansion.cpp prints it, with
// the two pieces the expander leaves out: a prologue that claims the frame
// and an epilogue that returns the result slot in w0 and gives it back.
// Five 4-byte slots make 20 bytes; sp must stay a multiple of 16, so the
// frame is 32. A leaf with no calls needs no frame record, and Apple's
// red zone could hold these slots, but the simplest back end claims its
// frame explicitly in every function, so there is one rule, not two.
//
// Follows: Apple, "Writing ARM64 code for Apple platforms" (frame record,
// red zone, 16-byte sp alignment) and the OS X Assembler Reference
// (directives, the leading underscore on C names).

	.section	__TEXT,__text,regular,pure_instructions
	.globl	_stack_calc
	.p2align	2
_stack_calc:
	sub	sp, sp, #32          // prologue: 20 bytes of slots, rounded to 32

	mov	w0, #7               // Const 7  -> slot 0
	str	w0, [sp, #0]
	mov	w0, #2               // Const 2  -> slot 1
	str	w0, [sp, #4]
	ldr	w0, [sp, #0]         // Sub(slot 0, slot 1) -> slot 2
	ldr	w1, [sp, #4]
	sub	w0, w0, w1
	str	w0, [sp, #8]
	mov	w0, #3               // Const 3  -> slot 3
	str	w0, [sp, #12]
	ldr	w0, [sp, #8]         // Mul(slot 2, slot 3) -> slot 4
	ldr	w1, [sp, #12]
	mul	w0, w0, w1
	str	w0, [sp, #16]

	ldr	w0, [sp, #16]        // epilogue: the result slot goes to w0
	add	sp, sp, #32
	ret

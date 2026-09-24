// A hand-written, macOS-flavoured AArch64 function built the way
// macro_expansion.cpp would generate it: every intermediate value gets its
// own stack slot, w0 and w1 are the only registers ever touched, and the
// function computes the same expression, (7 - 2) * 3, returning 15 in w0.
// A leaf function like this could use the red zone instead, but the
// simplest back end always allocates its frame explicitly.
//
// Follows: Apple, "OS X Assembler Reference" (directives and the leading
// underscore), and AAPCS64 (16-byte stack alignment).

	.section	__TEXT,__text,regular,pure_instructions
	.globl	_stack_calc
	.p2align	2
_stack_calc:
	sub	sp, sp, #16

	mov	w0, #7
	str	w0, [sp, #0]
	mov	w0, #2
	str	w0, [sp, #4]

	ldr	w0, [sp, #0]
	ldr	w1, [sp, #4]
	sub	w0, w0, w1
	str	w0, [sp, #8]

	mov	w0, #3
	str	w0, [sp, #12]

	ldr	w0, [sp, #8]
	ldr	w1, [sp, #12]
	mul	w0, w0, w1

	add	sp, sp, #16
	ret

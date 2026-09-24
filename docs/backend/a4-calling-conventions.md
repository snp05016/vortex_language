# A4. Calling conventions and ABIs

<p class="page-intro">A calling convention is the contract that lets code compiled independently, on different days, by different compilers, call into each other correctly. This chapter reads AAPCS64, Apple's arm64 deviations from it, and the SysV AMD64 psABI, and traces one call through each.</p>

<p class="vx-meta" markdown="1">Level: Intermediate · Reading time: about 25 minutes · Builds on: [A2. Reading and writing AArch64 assembly](a2-aarch64-assembly.md), [A3. Floats and vectors in registers](a3-floats-and-vectors.md).</p>

???+ remember "Before you start, remember"

    ??? question "What does a function receive when it is called with `&mut values`?"

        Access to the caller's own array, not a copy: writes through the
        parameter change the caller's storage. In machine terms, the
        function receives an address.

        Introduced in [8. Data in memory](../compiler/guide/stage-8-data-in-memory.md#references-and-passing-without-copying). Decision: [record 40](../decisions/references.md#d40).

    ??? question "What decides whether an AArch64 instruction works on 32 or 64 bits?"

        The register names: a `w` prefix selects the low 32 bits, an `x`
        prefix the full 64. Writing a `w` register zeroes the upper half of
        its `x` register.

        Introduced in [A2. Reading and writing AArch64 assembly](a2-aarch64-assembly.md).

    ??? question "How does code reach a global symbol on AArch64?"

        Two instructions: one finds the symbol's 4 KiB page relative to the
        program counter, the other adds the low 12 bits. Apple's assembler
        spells them `adrp`/`add ... @PAGEOFF`; ELF spells the second one
        with `:lo12:`.

        Introduced in [A2. Reading and writing AArch64 assembly](a2-aarch64-assembly.md).

    ??? question "What must a Vortex compiler prove about every array access before it runs?"

        That the index is in bounds: for an axis of extent `n`, every
        mathematical integer index must satisfy `0 <= i < n`, checked at
        runtime when it cannot be proven at compile time.

        Introduced in [9. Runtime safety](../compiler/guide/stage-9-runtime-safety.md#array-bounds).

!!! goals "In this chapter"

    - Explain why a calling convention exists and what happens when caller and callee disagree about one.
    - Trace a call's arguments and result through AAPCS64's registers, and recognize when an argument spills to the stack.
    - Recognize which registers a callee may clobber and which it must restore, on AArch64 and on x86-64, and say why that differs.
    - Name three ways Apple's arm64 ABI deviates from AAPCS64 and explain why each one matters to a compiler.
    - State the stack-alignment rule at a call on both ISAs and why `bl` and `call` need different bookkeeping to satisfy it.

## A contract with no compiler in the room

[A2](a2-aarch64-assembly.md) showed instructions that read and write registers
and memory, and [A3](a3-floats-and-vectors.md) added the floating-point and
vector registers. Neither chapter said which register holds a function's
first argument. That question has no single answer at the instruction-set
level: `bl multiply` is just "jump here and remember where to come back". The
answer comes from a separate document, agreed on by every compiler, linker
and hand-written assembly routine for a platform, called a **calling
convention** or, together with the rest of the platform's contract (object
file layout, name mangling, stack layout), an **application binary
interface (ABI)**.

The reason the contract has to be explicit is that the two sides of a call
are usually compiled independently. Vortex's runtime (the small library that
implements `print`, a failed bounds check, and anything else a compiled
program calls into) might be built by one version of clang, on one day,
while the Vortex compiler that generates the call is a different program
entirely, built on a different day, possibly for a different Vortex version.
Neither piece of code re-reads the other's source. If the caller put its
first argument in `x3` and the callee expected `x0`, the program would still
assemble, still link, and still crash, or worse, run and produce a wrong
answer. A calling convention removes that ambiguity: for a given function
signature, it fixes exactly where each argument goes, where the result
comes back, and which registers a callee is free to overwrite versus which
it must leave as it found them.

Consider the matmul kernel from [stage 10](../compiler/guide/stage-10-matrix-multiplication.md#the-program-the-milestone-asks-for):

```vortex
// items: valid
fn multiply(a: &[f32; 2, 3], b: &[f32; 3, 2], c: &mut [f32; 2, 2]) {
}
```

Three references, no return value. [Record 40](../decisions/references.md#d40)
already told you that a reference is passed as the address of its referent,
so at the machine level this call passes three addresses. AAPCS64, which the
next section reads in full, says that the first eight
integer-or-address arguments to a function go in registers `x0` through
`x7`, in order. A call to `multiply` therefore compiles, on any AAPCS64
target, to code that loads `&a` into `x0`, `&b` into `x1`, `&c` into `x2`,
then branches:

```text
adrp x0, a@PAGE
add  x0, x0, a@PAGEOFF
adrp x1, b@PAGE
add  x1, x1, b@PAGEOFF
adrp x2, c@PAGE
add  x2, x2, c@PAGEOFF
bl   _multiply
```

Nothing here is specific to Vortex or to `multiply`: any AAPCS64-conforming
compiler would place these three addresses the same way, which is exactly
the point. The rest of this chapter builds up the general rule this example
is an instance of, then looks at where three real ABIs disagree with it and
with each other.

## Where arguments go: two counters, not one

**AAPCS64** is the Arm Architecture Procedure Call Standard for 64-bit Arm
(A64), the base calling convention that Linux, Android and (with the
deviations in the next section) Apple's platforms build on.[^aapcs64] Its
core rule for scalar arguments (as opposed to structs and arrays passed by
value, which it classifies field by field and which this chapter does not
cover in full) is: keep two independent counters, one for
**general-purpose (GP)** arguments (integers, pointers, and Vortex
references, since a reference is an address) and one for
**floating-point/SIMD** arguments. Each argument advances only its own
counter.

- The first eight GP arguments go in `x0`-`x7`.
- The first eight floating-point or vector arguments go in `v0`-`v7` (as
  `s0`-`s7` when the value is an `f32`, `d0`-`d7` for an `f64`; [A3](a3-floats-and-vectors.md)
  covers these views of the same registers).
- Once a counter's eight registers are used up, every further argument of
  that kind goes on the stack, packed in order.
- The return value comes back the same way: in `x0` (and `x1` for a
  128-bit or two-word result) for a GP result, in `v0` for a floating-point
  one.

`examples/backend/a4-calling-conventions/classify_arguments.cpp` models
exactly this: two independent counters, spilling to the stack once a bank
is exhausted. It runs the same ten-argument call (three addresses, two `f32`
scale factors, five more integers) through AAPCS64 and, for contrast, the
x86-64 convention from the next section:

--8<-- "includes/examples/backend/a4-calling-conventions/classify_arguments.cpp.md"

Both conventions receive the same ten arguments in the same order, and both
run out of floating-point registers at the same point (there are only two
`f32` arguments here, so neither ABI even gets close to its FP limit). But
AAPCS64's eight GP registers swallow all eight GP arguments, while SysV
AMD64, which the next section reads, has only six and spills the last two
to the stack. Nothing about the *number* of arguments changed; only the
size of one register bank did.

??? check "Why does an `f64` argument never take an integer register's slot?"

    Because AAPCS64 (and every ABI in this chapter) keeps a separate
    counter for floating-point and vector arguments. Passing seven `f64`
    values and one `i32` puts the `i32` in `x0`, not `x7`, no matter what
    order the arguments appear in the function's signature.

## Caller-saved and callee-saved registers

A **caller-saved** (also called **volatile**) register is one a callee may
overwrite freely; if the caller needs that register's value after the call,
the caller must have saved it first, typically by spilling it to the stack.
A **callee-saved** (**non-volatile**) register is the opposite promise: the
callee must restore it to its original value before returning, so the
caller can rely on it surviving the call untouched. Both kinds exist because
neither extreme works well alone: if every register were caller-saved, a
caller would spill everything before every call; if every register were
callee-saved, a callee would spill everything in its prologue whether it
needed to or not. Splitting the register file lets each side save only
what it actually uses.

AAPCS64 makes `x19`-`x28` callee-saved among the general-purpose registers,
and `v8`-`v15` callee-saved among the floating-point ones, but with a catch
worth remembering: **only the low 64 bits of `v8`-`v15`** are protected.[^aapcs64]
A callee is free to clobber the upper 64 bits of those registers (the part
a 128-bit vector operation would use). A caller that keeps a 128-bit vector
value in `v9` across a call is trusting the ABI for nothing: the top half is
not part of the promise.

Now put a floating-point accumulator across a call, the way the matmul
kernel's inner loop would if it needed to call the runtime partway through
(to report a bounds-check failure, say, or to print an intermediate value
for debugging). On AArch64, the accumulator can live in `d8` through `d15`
and survive the call for free: the callee-saved rule protects it. Look at
the SysV AMD64 psABI, the calling convention for Linux and other System V
targets on x86-64,[^sysv] and the picture changes: **no XMM register is
callee-saved at all**. Every floating-point value that must survive a call
on x86-64 has to be spilled to memory first, whether it fits the pattern
AAPCS64 would protect for free or not. A kernel that calls into the runtime
inside its innermost loop pays a cost on x86-64 that it does not pay on
AArch64, purely because the two ABIs drew the caller-saved/callee-saved
line in different places.

<figure class="vx-figure" role="img" aria-label="Three argument banks (AAPCS64, Apple arm64, SysV AMD64) filling with the same ten arguments: three addresses, two f32 scale factors, five more integers. AAPCS64 and Apple arm64 fit all eight general-purpose arguments into x0 through x7. SysV AMD64 has only six general-purpose registers, so p3 and p4 spill to the stack; Apple's column also shows a variadic f64 argument landing on the stack rather than in a register, while AAPCS64 and SysV would put it in a register.">
<svg viewBox="0 0 720 380" xmlns="http://www.w3.org/2000/svg">
<text class="vx-text-accent" x="70" y="24" text-anchor="middle" font-weight="600">AAPCS64</text>
<text class="vx-text-accent" x="360" y="24" text-anchor="middle" font-weight="600">Apple arm64</text>
<text class="vx-text-accent" x="650" y="24" text-anchor="middle" font-weight="600">SysV AMD64</text>

<!-- AAPCS64 column: x0-x7 all used, v0-v1 used -->
<g>
<rect class="vx-box" x="10" y="40" width="120" height="22" rx="3"/>
<text class="vx-mono" x="70" y="55" text-anchor="middle">a -&gt; x0</text>
<rect class="vx-box" x="10" y="66" width="120" height="22" rx="3"/>
<text class="vx-mono" x="70" y="81" text-anchor="middle">b -&gt; x1</text>
<rect class="vx-box" x="10" y="92" width="120" height="22" rx="3"/>
<text class="vx-mono" x="70" y="107" text-anchor="middle">c -&gt; x2</text>
<rect class="vx-box" x="10" y="118" width="120" height="22" rx="3"/>
<text class="vx-mono" x="70" y="133" text-anchor="middle">p0 -&gt; x3</text>
<rect class="vx-box" x="10" y="144" width="120" height="22" rx="3"/>
<text class="vx-mono" x="70" y="159" text-anchor="middle">p1 -&gt; x4</text>
<rect class="vx-box" x="10" y="170" width="120" height="22" rx="3"/>
<text class="vx-mono" x="70" y="185" text-anchor="middle">p2 -&gt; x5</text>
<rect class="vx-box" x="10" y="196" width="120" height="22" rx="3"/>
<text class="vx-mono" x="70" y="211" text-anchor="middle">p3 -&gt; x6</text>
<rect class="vx-box" x="10" y="222" width="120" height="22" rx="3"/>
<text class="vx-mono" x="70" y="237" text-anchor="middle">p4 -&gt; x7</text>
<rect class="vx-box-accent" x="10" y="254" width="120" height="22" rx="3"/>
<text class="vx-mono" x="70" y="269" text-anchor="middle">alpha -&gt; v0</text>
<rect class="vx-box-accent" x="10" y="280" width="120" height="22" rx="3"/>
<text class="vx-mono" x="70" y="295" text-anchor="middle">beta -&gt; v1</text>
<rect class="vx-box-accent" x="10" y="312" width="120" height="22" rx="3" fill="none" stroke-dasharray="3 2"/>
<text class="vx-mono" x="70" y="327" text-anchor="middle">variadic f64 -&gt; v2</text>
</g>

<!-- Apple column: same register placement, but variadic float forced to stack -->
<g>
<rect class="vx-box" x="300" y="40" width="120" height="22" rx="3"/>
<text class="vx-mono" x="360" y="55" text-anchor="middle">a -&gt; x0</text>
<rect class="vx-box" x="300" y="66" width="120" height="22" rx="3"/>
<text class="vx-mono" x="360" y="81" text-anchor="middle">b -&gt; x1</text>
<rect class="vx-box" x="300" y="92" width="120" height="22" rx="3"/>
<text class="vx-mono" x="360" y="107" text-anchor="middle">c -&gt; x2</text>
<rect class="vx-box" x="300" y="118" width="120" height="22" rx="3"/>
<text class="vx-mono" x="360" y="133" text-anchor="middle">p0 -&gt; x3</text>
<rect class="vx-box" x="300" y="144" width="120" height="22" rx="3"/>
<text class="vx-mono" x="360" y="159" text-anchor="middle">p1 -&gt; x4</text>
<rect class="vx-box" x="300" y="170" width="120" height="22" rx="3"/>
<text class="vx-mono" x="360" y="185" text-anchor="middle">p2 -&gt; x5</text>
<rect class="vx-box" x="300" y="196" width="120" height="22" rx="3"/>
<text class="vx-mono" x="360" y="211" text-anchor="middle">p3 -&gt; x6</text>
<rect class="vx-box" x="300" y="222" width="120" height="22" rx="3"/>
<text class="vx-mono" x="360" y="237" text-anchor="middle">p4 -&gt; x7</text>
<rect class="vx-box-accent" x="300" y="254" width="120" height="22" rx="3"/>
<text class="vx-mono" x="360" y="269" text-anchor="middle">alpha -&gt; v0</text>
<rect class="vx-box-accent" x="300" y="280" width="120" height="22" rx="3"/>
<text class="vx-mono" x="360" y="295" text-anchor="middle">beta -&gt; v1</text>
<rect class="vx-box-bad" x="300" y="312" width="120" height="22" rx="3"/>
<text class="vx-mono" x="360" y="327" text-anchor="middle">variadic f64 -&gt; [sp]</text>
<text class="vx-text-muted" x="360" y="345" text-anchor="middle" font-size="11">every variadic arg: stack</text>
</g>

<!-- SysV column: only 6 GP regs, p3/p4 spill -->
<g>
<rect class="vx-box" x="590" y="40" width="120" height="22" rx="3"/>
<text class="vx-mono" x="650" y="55" text-anchor="middle">a -&gt; rdi</text>
<rect class="vx-box" x="590" y="66" width="120" height="22" rx="3"/>
<text class="vx-mono" x="650" y="81" text-anchor="middle">b -&gt; rsi</text>
<rect class="vx-box" x="590" y="92" width="120" height="22" rx="3"/>
<text class="vx-mono" x="650" y="107" text-anchor="middle">c -&gt; rdx</text>
<rect class="vx-box" x="590" y="118" width="120" height="22" rx="3"/>
<text class="vx-mono" x="650" y="133" text-anchor="middle">p0 -&gt; rcx</text>
<rect class="vx-box" x="590" y="144" width="120" height="22" rx="3"/>
<text class="vx-mono" x="650" y="159" text-anchor="middle">p1 -&gt; r8</text>
<rect class="vx-box" x="590" y="170" width="120" height="22" rx="3"/>
<text class="vx-mono" x="650" y="185" text-anchor="middle">p2 -&gt; r9</text>
<rect class="vx-box-bad" x="590" y="196" width="120" height="22" rx="3"/>
<text class="vx-mono" x="650" y="211" text-anchor="middle">p3 -&gt; [sp+0]</text>
<rect class="vx-box-bad" x="590" y="222" width="120" height="22" rx="3"/>
<text class="vx-mono" x="650" y="237" text-anchor="middle">p4 -&gt; [sp+8]</text>
<rect class="vx-box-accent" x="590" y="254" width="120" height="22" rx="3"/>
<text class="vx-mono" x="650" y="269" text-anchor="middle">alpha -&gt; xmm0</text>
<rect class="vx-box-accent" x="590" y="280" width="120" height="22" rx="3"/>
<text class="vx-mono" x="650" y="295" text-anchor="middle">beta -&gt; xmm1</text>
<rect class="vx-box-accent" x="590" y="312" width="120" height="22" rx="3" fill="none" stroke-dasharray="3 2"/>
<text class="vx-mono" x="650" y="327" text-anchor="middle">variadic f64 -&gt; xmm2</text>
<text class="vx-text-muted" x="650" y="345" text-anchor="middle" font-size="11">only 6 GP registers</text>
</g>
</svg>
<figcaption>The same call, three ABIs. AAPCS64 and Apple arm64 fit every
general-purpose argument in x0-x7; SysV AMD64's six general-purpose
registers force the last two into stack slots instead. A variadic
floating-point argument (dashed outline) rides a normal register on AAPCS64
and SysV, but Apple's ABI always sends it to the stack.</figcaption>
</figure>

## Apple's arm64 deviations

AAPCS64 explicitly allows a platform to specify further rules on top of it,
and Apple's "Writing ARM64 code for Apple platforms" does exactly that for
macOS and iOS.[^apple] A compiler that only knows the standard rule and
targets Apple's platform will miscompile. The deviations that matter most to
a back end:

- **`x18` is reserved.** AAPCS64 leaves `x18` as a platform register; Apple
  claims it for its own thread-local storage and forbids general use. A
  register allocator targeting Apple's arm64 must never hand `x18` to
  ordinary code.
- **`x29` must always hold a valid frame record.** [A5](a5-stack-frames.md)
  covers frame records in full; the Apple-specific point here is that this
  is not optional the way it is under the standard rule, because system
  tools that walk the stack (crash reporters, `lldb bt`, profilers) depend
  on it.
- **Every variadic argument goes on the stack**, even ones that would fit
  in a register under the fixed part of the signature. Apple's own text
  states that `va_list` is simply `char *`, one byte past the last named
  argument. Vortex's `print` is a good stand-in for the resulting
  variation: `print("rows:", 128)` compiles into a call with variable
  argument count and type. If that call ultimately reaches a variadic C
  runtime function, `print(1.5)`'s single `f64` argument lands in a
  register on Linux arm64 and on x86-64, but on the stack on Apple's arm64:
  same source line, three different machine states, because the ABI, not
  the value, decided.
- **The caller extends narrow arguments**, not the callee. The standard
  rule says a value narrower than 32 bits travels in a 32-bit container
  without saying who fills the bits above it; Apple resolves that ambiguity
  the opposite way from what many compiler writers expect: the *caller*
  must sign- or zero-extend the value before the call, so a callee compiled
  for Apple's platform is entitled to trust the full register.
  `examples/backend/a4-calling-conventions/narrow_extension.cpp` makes the
  hazard concrete: under the standard rule, an unmasked read of a narrow
  argument can pick up leftover bits from whatever last touched that
  register; under Apple's rule, because the caller is required to extend
  the value, the same unmasked read happens to come out correct:

  --8<-- "includes/examples/backend/a4-calling-conventions/narrow_extension.cpp.md"

  The lesson is not "Apple's rule is safer to ignore": it is that a callee
  must extend narrow values itself unless it knows, from the target it was
  compiled for, that the caller already did. Assuming the wrong direction
  produces a bug that only shows up as a wrong value, never as a crash.
- **A 128-byte red zone**, stack space below the stack pointer that a leaf
  function may use without adjusting the stack pointer first. [A5](a5-stack-frames.md)
  covers what a red zone buys a compiler and what it costs.
- **Stack-passed arguments are packed at their natural alignment**, rather
  than each rounded up to an 8-byte slot the way the standard rule and SysV
  AMD64 both do. Two adjacent `i32` stack arguments take 8 bytes together
  on Apple's arm64, not 16.

??? check "Why can't a Vortex compiler use `x18` as a spare scratch register on Apple's arm64, even though AAPCS64 allows it?"

    Because Apple's platform-specific rule overrides the standard one for
    any code that runs on that platform: the OS reserves `x18` for its own
    thread-local state, and code that writes it can corrupt state the
    kernel or system libraries depend on. AAPCS64 permits platforms to add
    exactly this kind of restriction, and Apple's document is where this
    one is written down.

## A third convention: SysV AMD64

x86-64 code on Linux and most other non-Windows x86-64 systems follows the
**SysV AMD64 psABI**.[^sysv] Its shape differs from AAPCS64 in more than
register names:

- Integer and pointer arguments go, in order, in `rdi`, `rsi`, `rdx`,
  `rcx`, `r8`, `r9`: six registers, not eight.
- Floating-point arguments go in `xmm0`-`xmm7`: eight registers, matching
  AAPCS64's count even though the integer count differs.
- The result comes back in `rax` (and `rdx` for a wider integer result) or
  `xmm0`.
- For a call to a variadic function, `al` must hold the count of vector
  registers used by the call, so the callee (which may not have seen the
  argument types) knows how many to save if it needs to.
- No XMM register is callee-saved, as the earlier section on
  caller-saved and callee-saved registers already used to make its point.
  The callee-saved general-purpose registers are `rbx`, `rbp`, and
  `r12`-`r15`.
- Like both AArch64 ABIs, SysV AMD64 gives leaf functions a 128-byte red
  zone.

`examples/backend/a4-calling-conventions/stack_alignment.cpp` gets at a
subtler difference: **where the call instruction itself keeps the return
address.** AArch64's `bl` writes the return address into the link register
(`x30`) and never touches the stack pointer. x86-64's `call` pushes eight
bytes of return address onto the stack. Both ABIs require the stack pointer
to be 16-byte aligned at the point of the call, but because `call` moves the
stack pointer and `bl` does not, "aligned at the call" and "aligned at the
callee's entry" are the same fact on AArch64 and different facts on x86-64:

--8<-- "includes/examples/backend/a4-calling-conventions/stack_alignment.cpp.md"

Its output shows the invariant surviving 0 or 16 bytes of stack arguments
and breaking at 8 or 24: an odd number of 8-byte stack slots throws the
16-byte alignment off, on both ISAs, unless the compiler pads the outgoing
argument area to a multiple of 16 bytes. This is why a compiler's stack-
argument layout code carries alignment padding logic even when every
individual argument is a natural machine word wide.

??? check "A function takes nine `f64` arguments and nothing else. How many end up on the stack, on SysV AMD64?"

    One. SysV AMD64 has eight floating-point argument registers (`xmm0`
    through `xmm7`), so the first eight `f64` arguments fill them and the
    ninth goes on the stack. AAPCS64 has the same count, so the answer
    would be the same there.

## Private conventions and Windows, briefly

AAPCS64 explicitly permits a **private calling convention** across an
interface that is never exposed to code outside the group of compilation
units using it, as long as the interface's owner documents the convention
and holds to it consistently. Nothing requires every function boundary to
use the platform's public ABI: it only has to, at the points where
independently-compiled or externally-visible code meets. Go takes this
option for its own internal calls, keeping arguments and results in
registers on its own schedule (a convention it documents but reserves the
right to change between releases) while still using the platform ABI, `cgo`,
wherever Go code calls into or is called from C.[^go-abi]

Windows x64 is worth a name-check even though this chapter does not read
its text closely: `rcx`, `rdx`, `r8`, `r9`, `xmm0`-`xmm3` by argument
position rather than by kind (so a signature `(int, double)` puts the
`double` in the *second* register slot, `xmm1`, not in `xmm0`), a 32-byte
"shadow store" the caller always allocates regardless of how many
arguments are used, and `xmm6`-`xmm15` callee-saved, unlike SysV's none at
all.[^msvc]

!!! vortex "Exercise"

    **Write Vortex's own ABI table.** For every Vortex scalar type
    (`i32`, `i64`, `usize`, `f32`, `f64`, `bool`, `char`) and for a
    reference (`&T` or `&mut T`), and for each of AAPCS64, Apple's arm64
    and SysV AMD64, record: which register bank it uses, which register it
    lands in for a short argument list, and whether the *caller* or the
    *callee* is responsible for extending it if it is narrower than a
    machine word. Do not write any code yet: this is a table, checked by
    hand against the three sources this chapter cites, that your later
    code generator will implement against.

    Then answer, and write down your answer in your compiler's
    architecture notes: **should a call from one Vortex function to
    another use the platform's public ABI, or a private convention?**
    AAPCS64 permits the private option wherever the interface is not
    publicly visible, and section "Private conventions and Windows,
    briefly" showed Go choosing it for exactly that reason. Vortex-to-
    runtime calls (`print`, a bounds-check failure) cross a boundary a
    debugger and a crash report need to understand, so the public ABI is
    the safer default there regardless of what you decide for
    Vortex-to-Vortex calls.

    Do not implement calls yet. The test that proves this exercise is
    done is a table and a documented decision, not a compiler change:
    when you do reach code generation, write a small test per Vortex type
    that confirms your emitted code places that type's arguments and
    results exactly where your table says, on every target you support.

## Key ideas

!!! recap "You can now answer"

    - **Why can't a caller and a callee agree on where arguments go just by both being compiled correctly?** Because "correctly" only has meaning relative to a convention; without one, nothing connects the caller's choice of register to the callee's expectation, even though each side's own code type-checks and compiles.
    - **Why does AAPCS64 keep two separate counters for arguments instead of one?** Because general-purpose and floating-point values use separate register banks; a function can receive eight integer arguments and eight `f64` arguments in registers at once, sixteen values, because neither counter had to share with the other.
    - **Why does an FP accumulator live across a call for free on AArch64 but not on x86-64?** AAPCS64 makes `v8`-`v15` (their low 64 bits) callee-saved; SysV AMD64 makes no XMM register callee-saved at all, so any floating-point value that must survive a call on x86-64 has to be spilled to memory first.
    - **Name one Apple arm64 rule that inverts the standard AAPCS64 rule.** Narrow-argument extension: the standard rule leaves it unspecified (in practice, callee-extends in most implementations); Apple requires the caller to extend before the call.
    - **Why do `bl` and `call` need different stack-alignment bookkeeping for the same 16-byte rule?** `bl` leaves the stack pointer untouched and keeps the return address in a register, so "aligned before the call" and "aligned at the callee's entry" are the same state; `call` pushes an 8-byte return address, so the callee's entry is always 8 bytes off from where it was at the call site, and the ABI states its alignment rule in terms of that offset.
    - **When is a private calling convention allowed instead of the platform ABI?** Across any interface that is not publicly visible outside the group of compilation units using it, as long as its owner documents the convention and applies it consistently; a call to a platform runtime function crosses a public interface and must use the public ABI.

## Where this comes back

!!! next "You will use this again in"

    - [A5. Stack frames](a5-stack-frames.md): *callee-saved registers*, *the red zone*, *the frame record*
    - [B1. The simplest back end that works](b1-simplest-backend.md): *argument registers*, *the prologue that saves what the callee promised to save*
    - [C1. Instruction selection](c1-instruction-selection.md): *which registers are available to allocate versus reserved by the ABI*
    - [D2. JIT compilation](d2-jit.md): *calling into and out of generated code at a public interface*

## Sources and further reading

[^aapcs64]: Arm, "Procedure Call Standard for the Arm 64-bit Architecture (AAPCS64)", 2025Q4 release. <https://github.com/ARM-software/abi-aa/blob/main/aapcs64/aapcs64.rst>
[^apple]: Apple, "Writing ARM64 code for Apple platforms". <https://developer.apple.com/documentation/xcode/writing-arm64-code-for-apple-platforms>
[^sysv]: x86-64 psABI (SysV AMD64 ABI) working group, "System V Application Binary Interface, AMD64 Architecture Processor Supplement". <https://gitlab.com/x86-psABIs/x86-64-ABI>
[^msvc]: Microsoft, "x64 calling convention". <https://learn.microsoft.com/en-us/cpp/build/x64-calling-convention>
[^go-abi]: The Go Authors, "Go internal ABI specification". <https://github.com/golang/go/blob/master/src/cmd/compile/abi-internal.md>

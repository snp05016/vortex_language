# A3. Floats and vectors in registers

<p class="page-intro">This chapter teaches the second half of the AArch64 register set: the 32 registers that hold floating-point numbers and vectors, the scalar instructions that work on them, the fused multiply-add and why a compiler may not form it on its own, how comparisons and conversions leave the floating-point side, and a first look at lanes. Vortex's showcase kernel is <code>f32</code> arithmetic, and its rules forbid fusion and demand exact NaN behavior, so every one of these instructions is one its back end must choose with care.</p>

<p class="vx-meta" markdown="1">Level: Foundations · Reading time: about 40 minutes · Builds on: [A2. Reading and writing AArch64 assembly](a2-aarch64-assembly.md)</p>

???+ remember "Before you start, remember"

    ??? question "What happens to the upper 32 bits of `x0` when an instruction writes `w0`?"

        They become zero. The register name chooses the size of the
        operation, and a 32-bit write clears the rest of the 64-bit register.

        Introduced in [A2. Reading and writing AArch64 assembly](a2-aarch64-assembly.md#registers-by-name).

    ??? question "Which flags does the condition `lt` test, and what does it mean after `cmp a, b` on integers?"

        It holds when N and V differ, which after an integer comparison
        means "a is less than b as signed numbers".

        Introduced in [A2. Reading and writing AArch64 assembly](a2-aarch64-assembly.md#flags-and-conditions).

    ??? question "What may a Vortex compiler not do to `sum + a * b` when the values are `f32`?"

        Fuse the multiply and the add into one operation, reorder or
        reassociate the arithmetic, compute in a wider format, or flush
        subnormal values to zero. Each operation is one IEEE 754 operation,
        rounded to nearest with ties to even.

        Introduced in [record 56](../decisions/numbers.md#d56).

    ??? question "What do `nan < 1.0` and `nan != nan` evaluate to in Vortex?"

        `false` and `true`. NaN is unordered: every ordered comparison and
        `==` with a NaN operand is false, and `!=` is true.

        Introduced in [record 24](../decisions/numbers.md#d24).

    ??? question "What must a Vortex cast `i32(x)` do when `x` is an `f32` NaN or infinity?"

        Stop the program with a runtime error. A float-to-integer cast
        truncates toward zero, and it fails for NaN, an infinity, or a result
        that does not fit.

        Introduced in [9. Runtime safety](../compiler/guide/stage-9-runtime-safety.md#invalid-numeric-casts). Decision: [record 27](../decisions/numbers.md#d27).

!!! goals "In this chapter"

    - Name the views `q`, `d`, `s`, `h` and `b` of a SIMD&FP register, and predict what a scalar write does to the rest of it.
    - Read and write the scalar floating-point instructions a compiler emits for `f32` and `f64` code, including loads, stores and conversions.
    - Explain what a fused multiply-add computes, recognize the six mnemonics that mean contraction, and find the setting that produced them.
    - Choose the condition code that gives a floating-point comparison its IEEE 754 meaning when a NaN is involved.
    - Read a vector instruction's arrangement specifier, and explain why a floating-point sum spread across lanes can give a different answer.

[A2](a2-aarch64-assembly.md) read integer code: 31 general-purpose registers,
loads and stores, flags and branches. Integer code alone cannot run the
program Vortex exists for. The matrix multiplication of
[stage 10](../compiler/guide/stage-10-matrix-multiplication.md#the-program-the-milestone-asks-for)
spends its time multiplying and adding `f32` values, and on AArch64 those
values never touch `x0` to `x30`. They live in a second register file, with
its own instructions, its own loads and stores, and its own ways of
reporting to the integer side.

This is also where a back end is most likely to change a program's answer
without anyone noticing. An integer addition has one correct result. A
floating-point expression has one correct result *for each order and
grouping of its roundings*, and a compiler that is allowed to pick among them
will. Vortex does not allow it ([record 56](../decisions/numbers.md#d56)), so
its back end must know exactly which instructions keep the order and which
quietly change it.

The chapter works from four small programs that run hand-written assembly
inside a C++ harness, as A2's did, set beside the listings clang writes for
the same jobs.

## A second register file

AArch64 has 32 more registers, `v0` to `v31`, used by floating-point and
SIMD instructions. **SIMD** (single instruction, multiple data) means one
instruction that does the same operation on several values at once; Arm's
name for its SIMD instructions is **Neon**. Each `v` register is 128 bits
wide[^aapcs64][^neon-regs]. They are separate from the general-purpose
registers: `v0` has nothing to do with `x0`.

As with `w0` and `x0`, the name you write sets the size of the operation.
`q0` names all 128 bits of `v0`, `d0` its low 64 bits, `s0` its low 32 bits,
`h0` its low 16 bits and `b0` its low byte. These are not different
registers: the call standard notes that `q1`, `d1` and `s1` "all refer to
the same entry in the register bank"[^aapcs64]. An `f32` value lives in an `s`
register and an `f64` in a `d` register, so `fadd s0, s1, s2` is a
single-precision addition and `fadd d0, d1, d2` a double-precision
one[^arm-fp]. Figure 1 draws the views.

<figure class="vx-figure">
<svg viewBox="0 0 760 330" role="img" aria-label="The views of one 128-bit SIMD and floating-point register" aria-describedby="a3-views-desc">
<title id="a3-views-title">The views of one 128-bit SIMD and floating-point register</title>
<desc id="a3-views-desc">Register v0 drawn as a 128-bit bar, bit 127 on the left and bit 0 on the right. Nested bars below it, all aligned to the right edge: q0 covers all 128 bits, d0 the low 64, s0 the low 32, h0 the low 16 and b0 the low 8. Below them the same register split into lanes: v0.4s as four 32-bit lanes numbered 3, 2, 1, 0 from left to right, and v0.2d as two 64-bit lanes numbered 1 and 0. Lane 0 of v0.4s is the same bits as s0, and lane 0 of v0.2d the same bits as d0. A bracket over bits 32 to 127 says that writing s0 sets these bits to zero.</desc>
<text class="vx-text-muted" x="80" y="22">bit 127</text>
<text class="vx-text-muted" x="720" y="22" text-anchor="end">bit 0</text>
<path class="vx-line" d="M80 34 L80 28 L558 28 L558 34"/>
<text class="vx-text-accent" x="319" y="46" text-anchor="middle">a write to s0 sets these 96 bits to zero</text>
<g class="vx-seq" style="--vx-i: 0; --vx-n: 5">
<text class="vx-mono" x="66" y="76" text-anchor="end">q0</text>
<rect class="vx-box" x="80" y="58" width="640" height="26"/>
<text class="vx-text-muted" x="400" y="76" text-anchor="middle">128 bits</text>
</g>
<g class="vx-seq" style="--vx-i: 1; --vx-n: 5">
<text class="vx-mono" x="66" y="110" text-anchor="end">d0</text>
<rect class="vx-box" x="400" y="92" width="320" height="26"/>
<text class="vx-text-muted" x="560" y="110" text-anchor="middle">64 bits: an f64</text>
</g>
<g class="vx-seq" style="--vx-i: 2; --vx-n: 5">
<text class="vx-mono" x="66" y="144" text-anchor="end">s0</text>
<rect class="vx-box-accent" x="560" y="126" width="160" height="26"/>
<text class="vx-text-muted" x="640" y="144" text-anchor="middle">32 bits: an f32</text>
</g>
<g class="vx-seq" style="--vx-i: 3; --vx-n: 5">
<text class="vx-mono" x="66" y="178" text-anchor="end">h0</text>
<rect class="vx-box" x="640" y="160" width="80" height="26"/>
<text class="vx-text-muted" x="680" y="178" text-anchor="middle">16</text>
</g>
<g class="vx-seq" style="--vx-i: 4; --vx-n: 5">
<text class="vx-mono" x="66" y="212" text-anchor="end">b0</text>
<rect class="vx-box" x="680" y="194" width="40" height="26"/>
<text class="vx-text-muted" x="700" y="212" text-anchor="middle">8</text>
</g>
<line class="vx-line" x1="80" y1="234" x2="720" y2="234"/>
<text class="vx-mono" x="66" y="264" text-anchor="end">v0.4s</text>
<rect class="vx-box" x="80" y="246" width="160" height="26"/>
<rect class="vx-box" x="240" y="246" width="160" height="26"/>
<rect class="vx-box" x="400" y="246" width="160" height="26"/>
<rect class="vx-box-accent" x="560" y="246" width="160" height="26"/>
<text class="vx-text-muted" x="160" y="264" text-anchor="middle">lane 3</text>
<text class="vx-text-muted" x="320" y="264" text-anchor="middle">lane 2</text>
<text class="vx-text-muted" x="480" y="264" text-anchor="middle">lane 1</text>
<text class="vx-text" x="640" y="264" text-anchor="middle">lane 0 = s0</text>
<text class="vx-mono" x="66" y="306" text-anchor="end">v0.2d</text>
<rect class="vx-box" x="80" y="288" width="320" height="26"/>
<rect class="vx-box" x="400" y="288" width="320" height="26"/>
<text class="vx-text-muted" x="240" y="306" text-anchor="middle">lane 1</text>
<text class="vx-text" x="560" y="306" text-anchor="middle">lane 0 = d0</text>
</svg>
<figcaption>Figure 1. One register, many names. The scalar views <code>q0</code> to <code>b0</code> all start at bit 0, so each is the low part of the larger ones. The vector views split the same 128 bits into equal lanes, numbered from the low end, so lane 0 of <code>v0.4s</code> is <code>s0</code>. A scalar instruction that writes <code>s0</code> clears the other 96 bits.</figcaption>
</figure>

The first example makes the views visible. It passes the `f64` value
$1 + 2^{-52}$, whose lowest bit is set, and reads the `s0` view of it. Then
it loads four floats into `q0`, runs one scalar `fadd` on `s0`, and stores
all 128 bits back.

--8<-- "includes/examples/backend/a3-floats-and-vectors/views.cpp.md"

It prints:

```text
--8<-- "examples/backend/a3-floats-and-vectors/views.expected"
```

Three lessons are in that output. First, `fmov w0, s0` copies the low 32 bits
of the register into a general-purpose register **without
conversion**[^a64-fmov], so the `s0` view of $1 + 2^{-52}$ is the bit pattern
`0x00000001`, which as a float is the smallest subnormal, not the float nearest
to the double.
Converting between precisions is a separate instruction, `fcvt s0, d0`, which
rounds using the mode the control register selects[^a64-fcvt] and gives
`0x3f800000`, the float 1.0. A back end that reads an `f64` through its `s`
name has a bug that no type checker will catch.

Second, the scalar `fadd s0, s0, s0` doubled lane 0 and set lanes 1 to 3 to
zero. This is the same rule as A2's `w` registers, applied to the wider file:
a scalar result is written into the low bits and the rest of the 128-bit
register is cleared. The instruction pages spell it out in their pseudocode:
`fmadd`'s, for one, builds the 128-bit result from zeros unless a control bit
called NEP asks for merging[^a64-fmadd]; the call standard requires NEP to be zero at
every function boundary[^aapcs64]. Third, the name `v0.4s` turns the same
`fadd` into four additions, one per lane. The last section of this chapter
comes back to lanes.

### The registers under the call standard

The procedure call standard gives the `v` registers roles, as it does the `x`
registers[^aapcs64]:

| Registers | Role under AAPCS64 |
| --- | --- |
| `v0` to `v7` | floating-point and vector arguments and results |
| `v8` to `v15` | saved by the called function, but only their low 64 bits |
| `v16` to `v31` | scratch: a function may overwrite them without saving them |

The middle row has a catch that matters to a back end. A function that uses
`v8` must restore it before returning, but only the low 64 bits, the `d8`
view, are protected; "it is the responsibility of the caller to preserve
larger values"[^aapcs64]. An `f32` or `f64` held in `d8` to `d15` survives a
call, while a full 128-bit vector does not. So `float f(float a, float b)`
receives `a` in `s0` and `b` in `s1` and returns its result in `s0`, and a
function with both kinds of argument fills the two files independently.
[A4](a4-calling-conventions.md) has the full rules, including Apple's
differences; one of them belongs here: on Apple platforms `long double` is
the same 64-bit type as `double`[^apple-arm64].

## Scalar arithmetic and data movement

The scalar floating-point instructions look like their integer cousins with an
`F` in front, and they take the same destination-first
operands[^arm-fp]. Compiling one-line C++ functions with Apple clang 21 at
`-O2` gives this set:

| Source (`float a, b`) | Instruction | Note |
| --- | --- | --- |
| `a + b`, `a - b` | `fadd s0, s0, s1`, `fsub s0, s0, s1` | |
| `a * b`, `a / b` | `fmul s0, s0, s1`, `fdiv s0, s0, s1` | |
| `-a` | `fneg s0, s0` | flips the sign bit; no rounding |
| `std::fabs(a)`, `std::sqrt(a)` | `fabs s0, s0`, `fsqrt s0, s0` | |
| `double(a)` | `fcvt d0, s0` | exact: every `f32` is an `f64` |
| `float(i)` for an `int` | `scvtf s0, w0` | rounds by the control register's mode[^a64-scvtf] |
| `int(a)` | `fcvtzs w0, s0` | rounds toward zero; see below |
| `p[i]` for `const float *p` | `ldr s0, [x0, x1, lsl #2]` | the same scaled address as A2 |

Loads and stores need no new ideas. `ldr s0, [...]` reads 4 bytes into
`s0`, `ldr d0, [...]` reads 8 and `ldr q0, [...]` reads 16, with the
addressing modes of A2; for a register offset the shift must again match the
access size, so `lsl #2` goes with `s`, `lsl #3` with `d` and `lsl #4` with
`q`[^a64-ldr-fp]. The address
always comes from general-purpose registers. Only the data lives in the `v`
file.

Here is a whole function. The loop below computes a dot product, and Apple
clang 21 compiles it at `-O2` (with vectorization and unrolling turned off, so
that one element is handled per trip):

```cpp
float dot(const float *a, const float *b, long n) {
  float s = 0.0f;
  for (long i = 0; i < n; ++i) s += a[i] * b[i];
  return s;
}
```

```gas
__Z3dotPKfS0_l:
	movi.2d	v0, #0000000000000000
	cmp	x2, #1
	b.lt	LBB0_2
LBB0_1:                                 ; =>This Inner Loop Header: Depth=1
	ldr	s1, [x0], #4
	ldr	s2, [x1], #4
	fmadd	s0, s1, s2, s0
	subs	x2, x2, #1
	b.ne	LBB0_1
LBB0_2:
	ret
```

Read it with A2's procedure. `movi.2d v0, #0` sets all of `v0` to zero, which
makes `s0` the float 0.0: an all-zero bit pattern is $+0.0$ in IEEE 754, so
no conversion is needed. The count `n` in `x2` runs down with `subs`, and
the two post-indexed loads walk the arrays, exactly as A2's `max_postinc`
did. The one new line is `fmadd s0, s1, s2, s0`, and it is the most
important instruction in this chapter.

## Fused multiply-add

`fmadd` multiplies two registers and adds a third in one
instruction[^a64-fmadd]. The operands are written destination, multiplicand,
multiplier, addend, so `fmadd s0, s1, s2, s0` computes `s0 + s1 × s2`: the
addend comes last, although the source wrote it first. It is a **fused
multiply-add** (FMA): the product is not rounded to a float before the
addition. The whole expression is computed as if exactly and rounded once,
which is how C++ describes `std::fma`: "as if to infinite precision and
rounded only once"[^cppref-fma]. The pseudocode on Arm's page shows the same
thing, a single call to one combined multiply-add function[^a64-fmadd].

One rounding instead of two sounds like pure gain, and for accuracy it often
is. It is still a different answer. The next example runs `a * b + c` both
ways on inputs chosen so that the difference is as large as it can be:

--8<-- "includes/examples/backend/a3-floats-and-vectors/fused.cpp.md"

It prints:

```text
--8<-- "examples/backend/a3-floats-and-vectors/fused.expected"
```

The separate instructions give 0 and the fused one gives $2^{-24}$. Neither
is a bug. You can follow every bit by hand.

### Following the bits by hand

<div class="vx-stepper" markdown="1">
<div class="vx-step" markdown="1">

**Step 1. The inputs**

An `f32` has a 24-bit significand: a leading 1 and 23 bits after the binary
point. Near 1.0 the gap between neighbouring floats is therefore $2^{-23}$.
Both inputs are exact floats:

| Name | Value | Binary |
| --- | --- | --- |
| `a` = `b` | $1 + 2^{-12}$ | `1.00000000000100000000000` |
| `c` | $-(1 + 2^{-11})$ | `-1.00000000001000000000000` |

</div>
<div class="vx-step" markdown="1">

**Step 2. The exact product**

$(1 + 2^{-12})^2 = 1 + 2^{-11} + 2^{-24}$. The last term is one place beyond
the 23 bits a float keeps: the exact product needs 24 bits after the point,
and a float has 23.

</div>
<div class="vx-step" markdown="1">

**Step 3. `fmul` rounds the product**

The two floats around the product are $F_0 = 1 + 2^{-11}$ and
$F_1 = 1 + 2^{-11} + 2^{-23}$. The product sits exactly halfway between
them, $2^{-24}$ from each. Round to nearest breaks a tie toward the
neighbour whose last bit is 0, the even one. That is $F_0$, so `fmul`
returns $1 + 2^{-11}$ and the $2^{-24}$ is gone.

</div>
<div class="vx-step" markdown="1">

**Step 4. `fadd` adds `c`**

$(1 + 2^{-11}) + (-(1 + 2^{-11})) = 0$, exactly. The second rounding has
nothing to do; the damage was done in step 3.

</div>
<div class="vx-step" markdown="1">

**Step 5. `fmadd` rounds once**

The fused instruction adds `c` to the exact product:
$1 + 2^{-11} + 2^{-24} - (1 + 2^{-11}) = 2^{-24}$. That is a power of two, a
float exactly, so the one rounding at the end changes nothing. The result
prints as `1p-24`.

</div>
</div>

<figure class="vx-figure">
<svg viewBox="0 0 760 250" role="img" aria-label="Rounding the product once or twice" aria-describedby="a3-fma-desc">
<title id="a3-fma-title">Rounding the product once or twice</title>
<desc id="a3-fma-desc">A number line near 1 plus 2 to the minus 11. Two neighbouring floats are marked: F0 equals 1 plus 2 to the minus 11 on the left, with last bit 0, and F1 equals F0 plus 2 to the minus 23 on the right, with last bit 1. The exact product P equals F0 plus 2 to the minus 24, exactly halfway between them. Upper path, fmul then fadd: P rounds to the even neighbour F0, then adding c gives 0. Lower path, fmadd: P is kept exactly, adding c gives 2 to the minus 24, which is a float, so the single final rounding keeps it.</desc>
<line class="vx-line" x1="60" y1="120" x2="700" y2="120"/>
<line class="vx-line" x1="160" y1="108" x2="160" y2="132"/>
<line class="vx-line" x1="600" y1="108" x2="600" y2="132"/>
<line class="vx-line" x1="380" y1="112" x2="380" y2="128"/>
<text class="vx-mono" x="160" y="152" text-anchor="middle">F0 = 1 + 2⁻¹¹</text>
<text class="vx-text-muted" x="160" y="170" text-anchor="middle">last bit 0 (even)</text>
<text class="vx-mono" x="600" y="152" text-anchor="middle">F1 = F0 + 2⁻²³</text>
<text class="vx-text-muted" x="600" y="170" text-anchor="middle">last bit 1 (odd)</text>
<circle class="vx-dot" cx="380" cy="120" r="6"/>
<text class="vx-mono" x="380" y="152" text-anchor="middle">P = F0 + 2⁻²⁴</text>
<text class="vx-text-muted" x="380" y="170" text-anchor="middle">exact product: a tie</text>
<g class="vx-seq" style="--vx-i: 0; --vx-n: 2">
<path class="vx-flow" d="M380 106 C 360 60, 190 60, 164 102"/>
<polygon class="vx-arrowhead" points="158,96 162,108 170,98"/>
<text class="vx-text-accent" x="270" y="40" text-anchor="middle">fmul: tie goes to even, P becomes F0</text>
<text class="vx-text" x="270" y="60" text-anchor="middle">then fadd with c: F0 − F0 = 0</text>
</g>
<g class="vx-seq" style="--vx-i: 1; --vx-n: 2">
<rect class="vx-box-strong" x="200" y="190" width="360" height="46" rx="4"/>
<text class="vx-text-accent" x="380" y="208" text-anchor="middle">fmadd: P is never rounded on its own</text>
<text class="vx-text" x="380" y="226" text-anchor="middle">P + c = 2⁻²⁴, a float, so the final rounding keeps it</text>
</g>
</svg>
<figcaption>Figure 2. Why <code>fmul</code> then <code>fadd</code> gives 0 and <code>fmadd</code> gives 2⁻²⁴. The exact product falls exactly halfway between two floats, so rounding it on its own throws away its lowest bit. The fused instruction keeps that bit until the end.</figcaption>
</figure>

The difference here is a whole result, not a last bit, because `c` cancels
everything the two paths agree on. That is common in real code: a residual,
a determinant or an error term subtracts nearly equal numbers, and whatever
the rounding kept or lost is all that remains. [P11](../optimize/p11-floating-point.md)
studies the numerical side; for a back end the lesson is that the two
instruction sequences are not interchangeable.

### The six mnemonics

The example's second half shows the other signs. All four scalar forms take
`Sd, Sn, Sm, Sa` and round once[^a64-fmadd][^a64-fmsub][^a64-fnmadd][^a64-fnmsub]:

| Mnemonic | Computes | With `a = 2, b = 3, c = 1` |
| --- | --- | --- |
| `fmadd` | `Sa + Sn × Sm` | 7 |
| `fmsub` | `Sa − Sn × Sm` | −5 |
| `fnmadd` | `−(Sn × Sm) − Sa` | −7 |
| `fnmsub` | `Sn × Sm − Sa` | 5 |

The vector instructions `fmla` and `fmls` do the same for every lane: `fmla`
multiplies corresponding elements and adds each product to the element
already in the destination[^a64-fmla], and `fmls` subtracts it. Those six
mnemonics are what fusion looks like in an AArch64 listing, and they are the
six [A2's exercise](a2-aarch64-assembly.md#for-vortex) searched for.

### Contraction: who decides

Turning a separate multiply and add into one fused operation is called
**contraction**. The source program did not ask for it; the compiler chose
it. Whether a compiler may contract is a language rule, and languages differ.
Clang's `-ffp-contract` option has the values `off`, `on`, `fast` and
`fast-honor-pragmas`; for C and C++ the default is `on`, which allows
"standard compliant fusion in the same statement"[^clang-contract]. The
`dot` loop's `s += a[i] * b[i]` is one statement, so clang fused it.

Compiling `float mac(float s, float a, float b) { return s + a * b; }` with
Apple clang 21 on an Apple M4 Pro (macOS 27, September 2026) shows who
decides:

| Settings | Instructions |
| --- | --- |
| `-O0` | `fmadd s0, s0, s1, s2`, after storing and reloading the arguments |
| `-O2` | `fmadd s0, s1, s2, s0` |
| `-O0 -ffp-contract=off` | `fmul s1, s1, s2`, `fadd s0, s0, s1` |
| `-O2 -ffp-contract=off` | `fmul s1, s1, s2`, `fadd s0, s0, s1` |

The optimization level does not matter; the contraction setting does. At the
LLVM IR level, the permission is written on the instructions. The **fast-math
flag** `contract` on an `fmul` and `fadd` allows "floating-point
contraction"[^llvm-fmf], such as fusing the multiply and the add into one
fused operation. With contraction `on`, clang instead emits a
call to the intrinsic `llvm.fmuladd`, whose rounding between the multiply and
the add is left unspecified: the code generator fuses it when the target has
a fused instruction and it is cheaper[^llvm-fmuladd]. That is why clang's
`-O0` output above is fused. Running `llc` 18.1.8 on the same machine on
three hand-written IR functions gives:

| IR | `llc -O0` | `llc -O2` |
| --- | --- | --- |
| `fmul`, `fadd`, no flags | `fmul`, `fadd` | `fmul`, `fadd` |
| `fmul contract`, `fadd contract` | `fmul`, `fadd` | `fmadd` |
| `call @llvm.fmuladd.f32` | `fmadd` | `fmadd` |

The first row is what [record 56](../decisions/numbers.md#d56) needs, and it
is also what LLVM does when nobody asks for anything. A Vortex front end that
emits LLVM IR stays correct by emitting plain `fmul` and `fadd` without
flags and never calling `llvm.fmuladd`; one that emits C must pass
`-ffp-contract=off`, because the C default fuses; and one that emits assembly
must never choose the six mnemonics above for two source operations.

On x86-64 the question arises later. The baseline x86-64 instruction set has
no FMA: the psABI's table of **microarchitecture levels** puts FMA in level
`x86-64-v3`, together with AVX and AVX2[^psabi]. Apple clang 21 with
`--target=x86_64-linux-gnu -O2` compiles `mac` to `mulss` and `addss`, and
adding `-march=x86-64-v3` turns it into `vfmadd231ss`. A back end targeting
both machines can therefore pass a contraction test on one and fail it on the
other; [B2](b2-x86-64.md) returns to the second target.

??? check "A Vortex compiler that emits C passes only `-O2` to clang. Its contraction test passes on Linux x86-64 and fails on macOS arm64. Why do the two machines disagree, and what is the fix?"

    The generated C has the same meaning on both: clang's default, `on`,
    allows fusing a multiply and an add written in one statement. On macOS
    arm64 the fused instruction is always available, so clang emitted
    `fmadd`. Baseline x86-64 has no FMA instruction, so there was nothing to
    fuse with, and the test passed by luck; built with `-march=x86-64-v3` it
    would fail there too. The fix is the same for both targets: pass
    `-ffp-contract=off`, and keep the test running on more than one
    instruction set.

## Comparisons: four answers, not three

Integer comparison has three outcomes: less, equal, greater. IEEE 754
floating-point comparison has four, because a NaN is not ordered with
anything, itself included. The pair is then **unordered**, and "less",
"equal" and "greater" are all false[^a64-fcmp].

`fcmp s0, s1` compares two floats and writes the result into the same N, Z,
C and V flags that `cmp` uses[^a64-fcmp], so the branches, `csel` and `cset`
of A2 work unchanged. What changes is what each condition means. The third
example runs `fcmp` on one pair of each kind and prints the flags:

--8<-- "includes/examples/backend/a3-floats-and-vectors/compare.cpp.md"

It prints:

```text
--8<-- "examples/backend/a3-floats-and-vectors/compare.expected"
```

Read the first four rows as a table of the flag patterns: less is
N Z C V = 1 0 0 0, equal is 0 1 1 0, greater is 0 0 1 0, and unordered is
0 0 1 1, the last one given on Arm's page for `fcmp`[^a64-fcmp]. Now apply A2's
condition table[^arm-suffix]. `lt` holds when N differs from V. For "less"
that is right, but for "unordered" N = 0 and V = 1 differ too, so `lt` is
true when a NaN is involved. After `fcmp`, `lt` means "less than, or
unordered". `mi` (N set) holds only for "less". Figure 3 draws where each
condition holds.

<figure class="vx-figure">
<svg viewBox="0 0 760 330" role="img" aria-label="Where each condition holds after a floating-point comparison" aria-describedby="a3-fcmp-desc">
<title id="a3-fcmp-title">Where each condition holds after a floating-point comparison</title>
<desc id="a3-fcmp-desc">Four columns for the outcomes of fcmp a, b: less, with flags 1 0 0 0; equal, 0 1 1 0; greater, 0 0 1 0; and unordered, when either value is NaN, 0 0 1 1. Below, one row per condition code shows the columns where it holds. mi holds only for less. lt holds for less and unordered. ls holds for less and equal. le holds for less, equal and unordered. ge holds for equal and greater. gt holds only for greater. eq holds only for equal. ne holds for less, greater and unordered. The cells where lt and le hold for unordered are marked as the trap, because the source comparison a less than b is false there.</desc>
<text class="vx-text-muted" x="30" y="30">fcmp a, b</text>
<text class="vx-text" x="215" y="30" text-anchor="middle">a &lt; b</text>
<text class="vx-text" x="355" y="30" text-anchor="middle">a = b</text>
<text class="vx-text" x="495" y="30" text-anchor="middle">a &gt; b</text>
<text class="vx-text" x="635" y="30" text-anchor="middle">unordered (NaN)</text>
<text class="vx-text-muted" x="30" y="54">N Z C V</text>
<text class="vx-mono" x="215" y="54" text-anchor="middle">1 0 0 0</text>
<text class="vx-mono" x="355" y="54" text-anchor="middle">0 1 1 0</text>
<text class="vx-mono" x="495" y="54" text-anchor="middle">0 0 1 0</text>
<text class="vx-mono" x="635" y="54" text-anchor="middle">0 0 1 1</text>
<line class="vx-line" x1="30" y1="66" x2="720" y2="66"/>
<text class="vx-mono" x="60" y="92">mi</text>
<rect class="vx-cell-on" x="150" y="76" width="130" height="24"/>
<text class="vx-mono" x="60" y="122">lt</text>
<rect class="vx-cell-on" x="150" y="106" width="130" height="24"/>
<rect class="vx-box-bad" x="570" y="106" width="130" height="24"/>
<text class="vx-mono" x="60" y="152">ls</text>
<rect class="vx-cell-on" x="150" y="136" width="130" height="24"/>
<rect class="vx-cell-on" x="290" y="136" width="130" height="24"/>
<text class="vx-mono" x="60" y="182">le</text>
<rect class="vx-cell-on" x="150" y="166" width="130" height="24"/>
<rect class="vx-cell-on" x="290" y="166" width="130" height="24"/>
<rect class="vx-box-bad" x="570" y="166" width="130" height="24"/>
<text class="vx-mono" x="60" y="212">ge</text>
<rect class="vx-cell-on" x="290" y="196" width="130" height="24"/>
<rect class="vx-cell-on" x="430" y="196" width="130" height="24"/>
<text class="vx-mono" x="60" y="242">gt</text>
<rect class="vx-cell-on" x="430" y="226" width="130" height="24"/>
<text class="vx-mono" x="60" y="272">eq</text>
<rect class="vx-cell-on" x="290" y="256" width="130" height="24"/>
<text class="vx-mono" x="60" y="302">ne</text>
<rect class="vx-cell-on" x="150" y="286" width="130" height="24"/>
<rect class="vx-cell-on" x="430" y="286" width="130" height="24"/>
<rect class="vx-cell-on" x="570" y="286" width="130" height="24"/>
<g class="vx-pulse">
<text class="vx-text-accent" x="635" y="148" text-anchor="middle">lt and le hold here:</text>
<text class="vx-text-accent" x="635" y="162" text-anchor="middle">"less, or unordered"</text>
</g>
</svg>
<figcaption>Figure 3. After <code>fcmp</code>, each condition code holds for a set of outcomes. For an ordered pair the integer names still fit, but <code>lt</code> and <code>le</code> also hold for the unordered outcome (dashed), where the source comparison is false. <code>mi</code> and <code>ls</code> give <code>&lt;</code> and <code>&lt;=</code> their IEEE 754 meaning; <code>gt</code>, <code>ge</code> and <code>eq</code> already have it, and <code>ne</code> is true for NaN, as <code>!=</code> must be.</figcaption>
</figure>

Compilers know this. For one-line C++ comparisons of two `float`
arguments, Apple clang 21 at `-O2` writes `fcmp s0, s1` followed by
`cset w0, mi` for `a < b`, `ls` for `a <= b`, `gt` for `a > b`, `ge` for
`a >= b`, `eq` for `a == b` and `ne` for `a != b`. It never uses `lt` or `le`
for these, because they would make `nan < 1.0` true. For `!(a < b)` it writes
`cset w0, pl`, the opposite of `mi`, which is true for NaN, as the negation
must be.

These are exactly the rules of [record 24](../decisions/numbers.md#d24): with
a NaN operand, `<`, `<=`, `>`, `>=` and `==` are false and `!=` is true. A
back end that reuses its integer condition table for floats gets four of the
six operators right and two wrong, and only for NaN inputs, which ordinary
tests rarely contain.

??? check "A back end lowers `if a < b { ... } else { ... }` on `f32` by branching to the `else` block when the condition fails. Which branch condition should it use, and why is `b.ge` wrong?"

    It should branch to `else` on `b.pl` (N clear): true for equal,
    greater and unordered, which is exactly "not less". `b.ge` holds when N
    equals V, and for unordered N = 0 and V = 1, so `b.ge` would fall
    through into the `then` block when `a` is NaN: the program would run the
    branch for `a < b` although `a < b` is false. On integers the two
    conditions agree; on floats only `pl` is the negation of `mi`.

## Leaving the float side: conversions

A comparison is one way a float reaches the integer side; a conversion is the
other. `fcvtzs w0, s0` converts an `f32` to a signed 32-bit integer, rounding
toward zero, which is the C++ and Vortex rule for casts[^a64-fcvtzs]. The
second half of the compare example shows what it does at the edges: 2.75
becomes 2 and -2.75 becomes -2, as expected, but 3e9 becomes 2147483647,
-3e9 becomes -2147483648, infinity becomes 2147483647 and NaN becomes 0.

The instruction never fails. Arm's pseudocode for the conversion computes the
integer and then **saturates** it, clamping it to the nearest value the
destination can hold, and records an Invalid Operation exception in a status
register instead of stopping[^a64-fptofixed]. That is a reasonable hardware
choice and the wrong language rule for Vortex, where each of the last four
inputs must stop the program ([record 27](../decisions/numbers.md#d27)). The
check has to come before the `fcvtzs`, and it must reject NaN too.

### A range check, half finished

For an `f32` in `s0` and a target of `i32`, a value is convertible exactly
when $-2^{31} \le x < 2^{31}$ (every value in that range truncates into
`i32`, and both bounds are exact `f32` values). Suppose `s1` holds $-2^{31}$
and `s2` holds $2^{31}$. The check has this shape, with the conditions left
for you:

```text
fcmp    s0, s1          // compare x with -2^31
b.??    cast_error      // fail when NOT (x >= -2^31)
fcmp    s0, s2          // compare x with 2^31
b.??    cast_error      // fail when NOT (x < 2^31)
fcvtzs  w0, s0          // now safe: the result is exact after truncation
```

Before you open the answer, decide which of the four outcomes (less, equal,
greater, unordered) must reach `cast_error` in each case, and find the code in
Figure 3 that holds for exactly those.

??? check "Which two conditions complete the check, and how does NaN get rejected?"

    The first branch must fire for "less" and "unordered": that is `b.lt`,
    the condition that holds for "less than, or unordered". The second must
    fire for "equal", "greater" and "unordered": `b.pl` (N clear) holds for
    exactly those, and `b.hs` (C set) does too. A NaN input is unordered in
    both comparisons, so the first branch already rejects it; infinities fail
    one of the two bounds. Here the property that makes `lt` wrong for `<`
    is what makes it right: the check is written as the negation of the
    comparison that must hold.

The conversions in the other direction do not fail. `scvtf` turns a signed
integer into a float, rounding with the mode the control register
selects[^a64-scvtf]; an `i32` above $2^{24}$ may not be exact in `f32`, and
it is rounded, not rejected. `fcvt` narrows `f64` to `f32` in the same
way[^a64-fcvt]. Both depend on the rounding mode, which brings in the last
piece of state.

## The floating-point control and status registers

Every floating-point instruction consults two system registers. **FPCR**, the
floating-point control register, holds settings[^fpcr]: the rounding mode in
bits 22 and 23 (0 means round to nearest, the IEEE 754 default; the other
three values round toward plus infinity, minus infinity or zero), a
**flush-to-zero** bit FZ in bit 24 that replaces **subnormal numbers** (the
tiny values below the smallest normal float) with zero, a default-NaN bit DN in
bit 25, and trap-enable bits in bits 8 to 12 that turn floating-point
exceptions into processor traps. **FPSR**, the status register, collects
sticky flags such as "an inexact result happened" or "an invalid operation
happened"[^aapcs64].

The call standard treats them as global state of the program. FPSR's bits
may hold anything when a function is entered, and FPCR's rounding mode,
flush-to-zero and trap bits may be changed only by calls to support functions
that affect the whole program[^aapcs64]. The first example read FPCR on an
Apple M4 Pro under macOS 27: rounding mode 0 and FZ 0, round to nearest and
subnormals kept. LLVM assumes the same environment by default: its results
"assume the round-to-nearest rounding mode, and subnormals are assumed to be
preserved"[^llvm-fpenv].

For Vortex this state is a promise to keep, not a feature to use.
[Record 56](../decisions/numbers.md#d56) requires round to nearest with ties
to even and forbids flushing subnormals to zero, which is what the default
FPCR provides, so a Vortex program and its runtime must never change these
bits. x86-64 has the same arrangement in one register, **MXCSR**, whose
control bits the psABI makes callee-saved and whose status bits it makes
caller-saved[^psabi].

## A first look at lanes

The name `v0.4s` in the first example is a register plus an **arrangement
specifier**: how many elements of what size the instruction should see. The
same element position in the inputs and the output is a **lane**, and a
vector instruction does its operation once per lane; there is no carry or
overflow from one lane into the next[^neon-regs]. The specifiers are:

| Specifier | Lanes × bits | Specifier | Lanes × bits |
| --- | --- | --- | --- |
| `16b` | 16 × 8 | `8b` | 8 × 8 |
| `8h` | 8 × 16 | `4h` | 4 × 16 |
| `4s` | 4 × 32 | `2s` | 2 × 32 |
| `2d` | 2 × 64 | `1d` | 1 × 64 |

The left column fills all 128 bits and the right column uses the low 64[^neon-regs].
For `f32` code, `4s` is the one that matters: four floats per register. A
single element can be named too: `v1.s[1]` is lane 1 of `v1` seen as 32-bit
elements, as in `mov s4, v1.s[1]`, which copies that lane into `s4`. Clang's
Apple listings move the specifier onto the mnemonic, as A2's table noted, so
`fadd v0.4s, v0.4s, v1.4s` appears there as `fadd.4s v0, v0, v1`, and the lane
as `v1[1]`.

Element-wise loops map onto lanes directly. For
`out[i] = a[i] + b[i]`, Apple clang 21 at `-O2` loads sixteen floats from each
array per trip, with two `ldp` instructions of `q` registers, and adds them
with four `fadd.4s` instructions, falling back to narrower loops (four lanes,
then scalar `ldr s0` and `fadd s0`) for the elements left over. Each output element is computed by exactly the
operations the source wrote, so the vector code gives the same bits as the
scalar code. For `y[i] += a * x[i]` it writes `fmla.4s v5, v1, v0[0]`, the
vector fused multiply-add with the scalar `a` taken from lane 0 of `v0`: fast,
and forbidden to Vortex for the same reason as `fmadd`.

A sum is different. The final example adds eight floats two ways: one at a
time into `s0`, and in four lanes whose partial sums `faddp` (add pairwise)
combines at the end[^a64-faddp].

--8<-- "includes/examples/backend/a3-floats-and-vectors/lanes.cpp.md"

It prints:

```text
--8<-- "examples/backend/a3-floats-and-vectors/lanes.expected"
```

The exact sum is 16777223, and neither answer is it. Figure 4 shows where
each one lost its ones.

<figure class="vx-figure">
<svg viewBox="0 0 760 300" role="img" aria-label="Summing the same eight floats serially and in four lanes" aria-describedby="a3-lanes-desc">
<title id="a3-lanes-title">Summing the same eight floats serially and in four lanes</title>
<desc id="a3-lanes-desc">The input is 2 to the 24 followed by seven ones. Upper half, serial: a chain of seven additions, each adding 1 to 2 to the 24; every result rounds back to 2 to the 24 because floats near 2 to the 24 are 2 apart and the tie goes to the even value, so the final result is 16777216. Lower half, four lanes: lane 0 gets 2 to the 24 plus 1, which rounds to 2 to the 24; lanes 1, 2 and 3 each get 1 plus 1, which is 2. The first faddp adds lanes 0 and 1 to give 2 to the 24 plus 2, and lanes 2 and 3 to give 4. The second faddp adds those to give 16777222. The exact sum is 16777223.</desc>
<text class="vx-text" x="30" y="26">serial: ((((2²⁴ + 1) + 1) + 1) ...)</text>
<g class="vx-seq" style="--vx-i: 0; --vx-n: 2">
<rect class="vx-box" x="30" y="40" width="96" height="30" rx="3"/>
<text class="vx-mono" x="78" y="60" text-anchor="middle">2²⁴</text>
<line class="vx-line" x1="126" y1="55" x2="146" y2="55"/>
<rect class="vx-box" x="146" y="40" width="68" height="30" rx="3"/>
<text class="vx-mono" x="180" y="60" text-anchor="middle">+1</text>
<line class="vx-line" x1="214" y1="55" x2="234" y2="55"/>
<rect class="vx-box" x="234" y="40" width="68" height="30" rx="3"/>
<text class="vx-mono" x="268" y="60" text-anchor="middle">+1</text>
<line class="vx-line" x1="302" y1="55" x2="322" y2="55"/>
<text class="vx-text-muted" x="350" y="60" text-anchor="middle">...</text>
<line class="vx-line" x1="378" y1="55" x2="398" y2="55"/>
<rect class="vx-box" x="398" y="40" width="68" height="30" rx="3"/>
<text class="vx-mono" x="432" y="60" text-anchor="middle">+1</text>
<line class="vx-line" x1="466" y1="55" x2="486" y2="55"/>
<rect class="vx-box-bad" x="486" y="40" width="140" height="30" rx="3"/>
<text class="vx-mono" x="556" y="60" text-anchor="middle">16777216</text>
<text class="vx-text-muted" x="30" y="92">each 2²⁴ + 1 is a tie between 2²⁴ and 2²⁴ + 2; it rounds to the even 2²⁴, seven times</text>
</g>
<line class="vx-line" x1="30" y1="112" x2="730" y2="112"/>
<text class="vx-text" x="30" y="136">four lanes, then faddp twice</text>
<g class="vx-seq" style="--vx-i: 1; --vx-n: 2">
<rect class="vx-box" x="30" y="150" width="150" height="44" rx="3"/>
<text class="vx-text-muted" x="105" y="166" text-anchor="middle">lane 0</text>
<text class="vx-mono" x="105" y="184" text-anchor="middle">2²⁴ + 1 → 2²⁴</text>
<rect class="vx-box" x="200" y="150" width="110" height="44" rx="3"/>
<text class="vx-text-muted" x="255" y="166" text-anchor="middle">lane 1</text>
<text class="vx-mono" x="255" y="184" text-anchor="middle">1 + 1 = 2</text>
<rect class="vx-box" x="330" y="150" width="110" height="44" rx="3"/>
<text class="vx-text-muted" x="385" y="166" text-anchor="middle">lane 2</text>
<text class="vx-mono" x="385" y="184" text-anchor="middle">1 + 1 = 2</text>
<rect class="vx-box" x="460" y="150" width="110" height="44" rx="3"/>
<text class="vx-text-muted" x="515" y="166" text-anchor="middle">lane 3</text>
<text class="vx-mono" x="515" y="184" text-anchor="middle">1 + 1 = 2</text>
<line class="vx-line" x1="105" y1="194" x2="170" y2="220"/>
<line class="vx-line" x1="255" y1="194" x2="170" y2="220"/>
<line class="vx-line" x1="385" y1="194" x2="450" y2="220"/>
<line class="vx-line" x1="515" y1="194" x2="450" y2="220"/>
<rect class="vx-box" x="100" y="220" width="140" height="30" rx="3"/>
<text class="vx-mono" x="170" y="240" text-anchor="middle">2²⁴ + 2</text>
<rect class="vx-box" x="400" y="220" width="100" height="30" rx="3"/>
<text class="vx-mono" x="450" y="240" text-anchor="middle">4</text>
<line class="vx-line" x1="170" y1="250" x2="310" y2="266"/>
<line class="vx-line" x1="450" y1="250" x2="310" y2="266"/>
<rect class="vx-box-accent" x="240" y="266" width="140" height="30" rx="3"/>
<text class="vx-mono" x="310" y="286" text-anchor="middle">16777222</text>
<text class="vx-text-muted" x="400" y="286">exact: 16777223</text>
</g>
</svg>
<figcaption>Figure 4. The same eight numbers, grouped two ways. Added one at a time, every 1 meets 2²⁴ alone and is rounded away. Spread over four lanes, six of the ones meet each other first and survive. Neither order is the true sum; they are two different programs.</figcaption>
</figure>

Neither result is "the right one"; each is the correct result of a different
program. The serial loop is the program the source wrote, and it is the only
one [record 56](../decisions/numbers.md#d56) allows, since spreading a sum
across lanes reassociates it. Compilers respect this. For
`float total(const float *a, long n)`, a plain summing loop, Apple clang 21
at `-O2` loads sixteen floats per trip with `ldp` of `q` registers, but then
extracts them one by one (`mov s5, v1[1]` and so on) and adds them into `s0`
in source order: the loads are vectorized and the arithmetic is not. Only when
reassociation is allowed (with `-fassociative-math -fno-signed-zeros
-fno-trapping-math`) does it switch to `fadd.4s` partial sums and finish with
`faddp.4s` and `faddp.2s`, which is the `sum_lanes` shape.
[P10](../optimize/p10-vectorization.md) treats this transformation, and when
a vectorizer may make it, in full.

??? check "The stage 10 kernel computes each `c[row, column]` as a sum over `k`. Which of these could a Vortex back end run in lanes without changing any printed digit: four different `c` elements at once, or four terms of one element's sum at once?"

    Four different elements. Each lane then runs one element's whole sum,
    in source order, with a separate `fmul` and `fadd` (never `fmla`), so
    each element gets exactly the operations the source wrote. Four terms of
    one sum would be added in partial sums and combined at the end, which
    regroups the additions, as `sum_lanes` did, and can change the result.

## Reading float code in a listing

A2's procedure for reading a listing still applies. Floating-point code adds
five things to check:

1. **Name the precision.** `s` operands are `f32`, `d` operands `f64`, `q`
   operands a whole vector. An `fcvt` between them is a conversion that the
   source asked for (or should have).
2. **Look for fusion.** Any of `fmadd`, `fmsub`, `fnmadd`, `fnmsub`, `fmla`
   and `fmls` means a multiply and an add were merged. Find the setting that
   allowed it.
3. **Read comparisons as IEEE 754.** After `fcmp`, `mi`, `ls`, `gt`, `ge`,
   `eq` and `ne` mean `<`, `<=`, `>`, `>=`, `==` and `!=`; `lt`, `le`, `pl`,
   `hi` and the others include "unordered".
4. **Check the edges of conversions.** An `fcvtzs` with no comparison in
   front of it saturates silently.
5. **Find the lanes.** A specifier such as `.4s` means four values per
   instruction. For an element-wise loop that is harmless; for a sum or a
   product it means the operations were regrouped.

## For Vortex

!!! vortex "Exercise"

    **Make your back end's `f32` code obey the language, and prove it with
    three tests.** Whatever back end you chose in
    [stage 6](../compiler/guide/stage-6-first-machine-code.md#choosing-a-back-end),
    find the place where it decides three things: whether a multiply and an
    add may fuse, which condition a floating-point comparison uses, and what
    comes before a float-to-integer conversion. Then add these programs to
    your end-to-end tests, each with its expected output from records
    [4](../decisions/program.md#d4), [24](../decisions/numbers.md#d24),
    [27](../decisions/numbers.md#d27) and [56](../decisions/numbers.md#d56):

    1. **No contraction.** Run with every optimization setting your compiler
       has:

        ```vortex
        // statements: valid
        let a: f32 = 1.000244140625;   // 1 + 2^-12, exact in f32
        let c: f32 = -1.00048828125;   // -(1 + 2^-11)
        print(a * a + c);              // prints: 0.0
        ```

        A fused multiply-add would print `5.9604645e-8` instead.

    2. **NaN comparisons.** A program that makes a NaN from `0.0 / 0.0`
       and prints the result of each of `<`, `<=`, `>`, `>=`, `==` and `!=`
       against an ordinary `f32`, and against itself, in both operand
       orders. Every line of expected output follows from record 24.
    3. **Cast edges.** One program per input that must stop with a runtime
       error when cast with `i32(x)`: NaN, `inf`, `-inf`, `2147483648.0` and
       `-2147483904.0` (the first `f32` below $-2^{31}$). Add one valid
       program that casts `-2147483648.0`, `2.75` and `-2.75` and prints
       `-2147483648 2 -2`.

    Also keep a hand-annotated `-O0` listing of one comparison and one cast
    from your compiler, next to the tests, marking the `fcmp`, the condition
    it is read with, and the checks in front of `fcvtzs`.

    **Not yet.** Do not emit vector instructions or keep values in lanes;
    [P10](../optimize/p10-vectorization.md) and [C1](c1-instruction-selection.md)
    come first. Do not touch FPCR or FPSR from the runtime. Do not add a
    fast-math option: record 56 allows one only as a later, explicit opt-in.

    **Done when** all three tests pass on your unmodified compiler on macOS
    arm64 (and on Linux, if your CI runs there), and each one fails for a
    deliberate, temporary breakage: contraction allowed (for an LLVM path, a
    `contract` flag on the multiply and the add; for a C path, dropping
    `-ffp-contract=off`), `<` lowered with the condition `lt` instead of
    `mi`, and the check in front of the conversion removed. If a breakage
    does not make its test fail, the test is not testing what you think.

## Key ideas

!!! recap "You can now answer"

    - **What are `q0`, `d0` and `s0`?** Views of the same 128-bit register `v0`: its low 128, 64 and 32 bits. A scalar write sets the rest of the register to zero.
    - **What is the difference between `fmov w0, s0` and `fcvtzs w0, s0`?** `fmov` copies the bits without conversion; `fcvtzs` converts the value to an integer, rounding toward zero.
    - **Why does `fmadd` give a different answer from `fmul` then `fadd`?** It rounds once, after the exact product and sum; the pair rounds the product first, which can lose a bit that the addition would have exposed.
    - **Which mnemonics mean contraction on AArch64?** `fmadd`, `fmsub`, `fnmadd`, `fnmsub`, and the vector `fmla` and `fmls`.
    - **Why is `lt` the wrong condition for `a < b` after `fcmp`?** Its flags also match the unordered outcome, so it is true when a NaN is involved; `mi` is true only for "less".
    - **What does `fcvtzs` do with NaN or an out-of-range value?** It saturates, and gives 0 for NaN, without stopping; a language that rejects such casts must check first.
    - **Why can a sum in four lanes differ from a serial sum?** The lanes regroup the additions, and floating-point addition is not associative.

## Where this comes back

!!! next "You will use this again in"

    - [A4. Calling conventions and ABIs](a4-calling-conventions.md): *`v0` to `v7` for arguments*, *only the low 64 bits of `v8` to `v15`*
    - [B1. The simplest back end that works](b1-simplest-backend.md): *`s` registers in stack slots*, *`fcmp` and its conditions*
    - [B2. A second target: x86-64](b2-x86-64.md): *`xmm` registers*, *FMA only from `x86-64-v3`*
    - [C1. Instruction selection](c1-instruction-selection.md): *fused multiply-add as a pattern*, *conversions*
    - [C3. Register allocation I: linear scan](c3-linear-scan.md): *two register files*
    - [C7. Peephole optimization](c7-peephole.md): *contraction as a forbidden combine*
    - [P10. Vectorization](../optimize/p10-vectorization.md): *lanes*, *ordered reductions*
    - [P11. Floating point under optimization](../optimize/p11-floating-point.md): *one rounding versus two*, *the `contract` flag*
    - [P12. Anatomy of a fast GEMM](../optimize/p12-fast-gemm.md): *`fmla` by element*

## Sources and further reading

The call standard's section on SIMD and floating-point registers is short and
worth reading whole. For single instructions, the SIMD&FP pages of DDI 0602
are the reference; their pseudocode settles questions the prose leaves open.

[^aapcs64]: Arm, "Procedure Call Standard for the Arm 64-bit Architecture (AArch64)", release 2025Q4, section "SIMD and Floating-Point registers": `v0` to `v31` and their views, argument and callee-saved roles, and the rules for FPSR and FPCR (including NEP) at a public interface. <https://github.com/ARM-software/abi-aa/blob/main/aapcs64/aapcs64.rst>
[^neon-regs]: Arm, "Learn the architecture: Introducing Neon" (102474, version 1.0), "Registers, vectors, lanes and elements": 128-bit Neon registers, lanes, and the arrangement specifiers. <https://developer.arm.com/documentation/102474/0100/Fundamentals-of-Armv8-Neon-technology/Registers--vectors--lanes-and-elements>
[^arm-fp]: Arm, "Learn the architecture: A64 Instruction Set Architecture Guide" (102374, version 1.3), "Data processing - floating point". <https://developer.arm.com/documentation/102374/0103/Data-processing---floating-point>
[^apple-arm64]: Apple, "Writing ARM64 code for Apple platforms", Apple Developer Documentation: `long double` is a double-precision type. <https://developer.apple.com/documentation/xcode/writing-arm64-code-for-apple-platforms>
[^a64-fmov]: Arm, "Arm A-profile A64 Instruction Set Architecture" (DDI 0602, 2026-06), "FMOV (general)": move to or from a general-purpose register without conversion. <https://developer.arm.com/documentation/ddi0602/2026-06/SIMD-FP-Instructions/FMOV--general---Floating-point-move-to-or-from-general-purpose-register-without-conversion->
[^a64-fcvt]: Arm, DDI 0602 (2026-06), "FCVT": convert precision (scalar), rounding as FPCR selects. <https://developer.arm.com/documentation/ddi0602/2026-06/SIMD-FP-Instructions/FCVT--Floating-point-convert-precision--scalar-->
[^a64-scvtf]: Arm, DDI 0602 (2026-06), "SCVTF (scalar, integer)". <https://developer.arm.com/documentation/ddi0602/2026-06/SIMD-FP-Instructions/SCVTF--scalar--integer---Signed-integer-convert-to-floating-point--scalar-->
[^a64-ldr-fp]: Arm, DDI 0602 (2026-06), "LDR (register, SIMD&FP)": the shift amounts allowed for each access size. <https://developer.arm.com/documentation/ddi0602/2026-06/SIMD-FP-Instructions/LDR--register--SIMD-FP---Load-SIMD-FP-register--register-offset-->
[^a64-fcvtzs]: Arm, DDI 0602 (2026-06), "FCVTZS (scalar, integer)": convert to signed integer, rounding toward zero. <https://developer.arm.com/documentation/ddi0602/2026-06/SIMD-FP-Instructions/FCVTZS--scalar--integer---Floating-point-convert-to-signed-integer--rounding-toward-zero--scalar-->
[^a64-fptofixed]: Arm, DDI 0602 (2026-06), "Shared Pseudocode", `shared/functions/float`: the function `FPToFixed`, which saturates its result with `SatQ` and raises Invalid Operation for NaN and overflow. <https://developer.arm.com/documentation/ddi0602/2026-06/Shared-Pseudocode/shared-functions-float>
[^a64-fmadd]: Arm, DDI 0602 (2026-06), "FMADD": fused multiply-add (scalar), including the pseudocode that zeroes the rest of the destination unless FPCR asks for merging. <https://developer.arm.com/documentation/ddi0602/2026-06/SIMD-FP-Instructions/FMADD--Floating-point-fused-multiply-add--scalar-->
[^a64-fmsub]: Arm, DDI 0602 (2026-06), "FMSUB". <https://developer.arm.com/documentation/ddi0602/2026-06/SIMD-FP-Instructions/FMSUB--Floating-point-fused-multiply-subtract--scalar-->
[^a64-fnmadd]: Arm, DDI 0602 (2026-06), "FNMADD". <https://developer.arm.com/documentation/ddi0602/2026-06/SIMD-FP-Instructions/FNMADD--Floating-point-negated-fused-multiply-add--scalar-->
[^a64-fnmsub]: Arm, DDI 0602 (2026-06), "FNMSUB". <https://developer.arm.com/documentation/ddi0602/2026-06/SIMD-FP-Instructions/FNMSUB--Floating-point-negated-fused-multiply-subtract--scalar-->
[^a64-fmla]: Arm, DDI 0602 (2026-06), "FMLA (vector)": fused multiply-add to accumulator. <https://developer.arm.com/documentation/ddi0602/2026-06/SIMD-FP-Instructions/FMLA--vector---Floating-point-fused-multiply-add-to-accumulator--vector-->
[^a64-fcmp]: Arm, DDI 0602 (2026-06), "FCMP": quiet compare (scalar), with the flags an unordered comparison sets. <https://developer.arm.com/documentation/ddi0602/2026-06/SIMD-FP-Instructions/FCMP--Floating-point-quiet-compare--scalar-->
[^a64-faddp]: Arm, DDI 0602 (2026-06), "FADDP (vector)": add pairwise. <https://developer.arm.com/documentation/ddi0602/2026-06/SIMD-FP-Instructions/FADDP--vector---Floating-point-add-pairwise--vector-->
[^fpcr]: Arm, "Arm A-profile Architecture Registers" (DDI 0601, 2026-06), "FPCR, Floating-point Control Register": the RMode, FZ, DN and trap-enable fields. <https://developer.arm.com/documentation/ddi0601/2026-06/AArch64-Registers/FPCR--Floating-point-Control-Register>
[^arm-suffix]: Arm, "Arm Instruction Set Reference Guide" (100076, version 1.0, now marked superseded), "Condition code suffixes and related flags", table D1-2. <https://developer.arm.com/documentation/100076/0100/A64-Instruction-Set-Reference/Condition-Codes/Condition-code-suffixes-and-related-flags>
[^cppref-fma]: cppreference.com, "std::fma, std::fmaf, std::fmal". <https://en.cppreference.com/w/cpp/numeric/math/fma>
[^clang-contract]: LLVM Project, *Clang Compiler User's Manual*, option `-ffp-contract`. <https://clang.llvm.org/docs/UsersManual.html#cmdoption-ffp-contract>
[^llvm-fmf]: LLVM Project, *LLVM Language Reference Manual*, "Fast-Math Flags": the `contract` flag. <https://llvm.org/docs/LangRef.html#fast-math-flags>
[^llvm-fmuladd]: LLVM Project, *LLVM Language Reference Manual*, "'llvm.fmuladd.*' Intrinsic". <https://llvm.org/docs/LangRef.html#int-fmuladd>
[^llvm-fpenv]: LLVM Project, *LLVM Language Reference Manual*, "Floating-Point Environment". <https://llvm.org/docs/LangRef.html#floatenv>
[^psabi]: *System V Application Binary Interface, AMD64 Architecture Processor Supplement* (the x86-64 psABI), source repository: the microarchitecture levels table (FMA in `x86-64-v3`), the register usage table, and the MXCSR rules. <https://gitlab.com/x86-psABIs/x86-64-ABI>

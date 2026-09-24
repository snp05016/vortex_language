# Numbers, literals and casts

These records settle how Vortex v0.1 handles numbers: how a cast is written
and what it converts, how an integer or floating literal gets its type, what
happens when a literal does not fit, how wide `usize` is, and how strictly
floating-point arithmetic follows IEEE 754, the standard for binary
floating-point arithmetic. The specification chapters now state these rules;
each record below explains why the rule was chosen and what it changed.

## 1. Cast syntax {#d1}

**Status:** Accepted, 2026-09-23. **Applies to:** v0.1. **Resolves:** none.
**Guide stage:** 3, 5.

**Question.** How does the grammar parse a cast such as `f32(count)`, when
`f32` is a keyword?

**Before this decision.**
[Types and values 4.12](../specification/types-and-values.md#412-casts-and-conversions)
and the [formal grammar](../specification/grammar.md#expressions-and-precedence)
said a cast uses call syntax and that name and type checking decide whether the
callee is a conversion. But the type names are keywords
([Lexical structure 2.4](../specification/lexical-structure.md#24-keywords)),
and no primary expression starts with a keyword, so no cast could be parsed.
[Stage 3](../compiler/guide/stage-3-parser-and-tree.md#places-where-the-grammar-needs-care)
listed this as an open decision.

**Options.**

- A cast production: a numeric type keyword, then one parenthesized expression.
- Type names as ordinary identifiers, resolved after parsing.
- An `as` operator.

**Elsewhere.** Go writes a conversion as [`T(x)`][go-conv] and makes its type
names [predeclared identifiers][go-predecl] rather than keywords. Rust writes
[`x as T`][rs-cast], and Swift uses initializer syntax such as
[`Double(x)`][sw-conv].

**Decision.** A cast is a primary expression:

```ebnf
cast_expression ::=
    numeric_type, "(", expression, ")" ;

numeric_type ::=
      "i32" | "u32" | "usize" | "f32" | "f64" ;
```

A cast has exactly one operand, and it is not a call: name resolution never
looks up its type name. `void`, `bool`, `char` and `String` must not begin an
expression, so a cast to one of them is a syntax error. The conversions a cast
performs are set by [record 27](#d27).

**Why.** The five names are already keywords, so the parser recognizes a cast
from its first token without any name lookup. Identifiers would undo the
keyword list, and `as` would add a keyword and respell every existing example.

**Consequences.** Every existing example keeps its spelling. Stage 3 builds a
cast node that holds the target type and the operand; stage 5 checks the
operand.

```vortex
// statements: syntax error
let flag = true;
let bits = bool(flag); // syntax error: bool cannot begin a cast
```

Changed: the grammar's primary expressions and precedence table, Types and
values 4.12, Expressions 5.1 to 5.3 and 5.9, the tour's cast section, the cheat
sheet, parser design 10.1 and 10.6, guide stages 3 to 5 and the word list.

## 24. Floating-point special values {#d24}

**Status:** Accepted, 2026-09-23. **Applies to:** v0.1. **Resolves:** N14.
**Guide stage:** 6, 9.

**Question.** Is floating-point division by zero an error, and how do NaN (not
a number) and the infinities behave in comparisons, casts and output?

**Before this decision.**
[Types and values 4.4](../specification/types-and-values.md#44-floating-point-values)
deferred to "the target's documented IEEE 754 support".
[Diagnostics 10.2](../specification/diagnostics.md#runtime-error) listed only
integer division by zero, and
[stage 9](../compiler/guide/stage-9-runtime-safety.md#floating-point) noted
that no page named NaN or infinity.

**Options.**

- Plain IEEE 754: no error, and the special values flow through.
- A runtime error for floating-point division by zero.
- Whatever the target does.

**Elsewhere.** In Julia, dividing a float by zero gives
[an infinity or NaN][jl-special]. Go does not specify floating-point division
by zero [beyond IEEE 754][go-float]. Rust's `as`
[turns NaN into 0 and clamps infinities][rs-numcast] when casting to an
integer; Vortex stops the program instead.

**Decision.**

- Floating-point arithmetic is never a runtime error. A nonzero finite value
  divided by zero gives an infinity with the sign IEEE 754 specifies, and
  `0.0 / 0.0` gives NaN.
- NaN is unordered: `==` with a NaN operand is `false`, `!=` is `true`, and
  `<`, `<=`, `>` and `>=` are `false`.
- A cast of NaN or an infinity to an integer type must be a runtime error
  ([record 27](#d27)).
- `print` writes these values as `NaN`, `inf` and `-inf`
  ([record 4](program.md#d4)).

**Why.** IEEE 754 already defines every case, and common processors implement
it directly. A check on every division would slow the numerical kernels
Vortex exists for and invent a rule that numerical programmers do not expect.
An integer has no NaN or infinity, so that one cast fails instead of inventing
a number.

**Consequences.**

```vortex
// statements: valid
let zero: f32 = 0.0;
let big = 1.0 / zero;
let odd = zero / zero;
print(big, -big, odd, odd == odd); // prints: inf -inf NaN false
```

Stage 6 lowers floating-point division without a check and prints the special
values; stage 9 checks float-to-integer casts. Changed: Types and values 4.4,
Expressions 5.5, Diagnostics 10.2, the tour's numerical rules, guide stages 9
and 11.

## 27. Cast conversion matrix {#d27}

**Status:** Accepted, 2026-09-23. **Applies to:** v0.1. **Resolves:** none.
**Guide stage:** 5, 9.

**Question.** Which numeric casts are allowed, how do they round, and which
values make them fail?

**Before this decision.**
[Types and values 4.12](../specification/types-and-values.md#412-casts-and-conversions)
said "a complete numeric conversion matrix is not yet fixed". The tour
promised truncation toward zero, checked out-of-range casts, no saturation and
a compile-time error for `u32(-1)`
([casts](../language-tour/08-expressions.md#cast-expressions),
[numerical rules](../language-tour/06-runtime-and-numerical-rules.md#rejected-or-checked-examples)).

**Options.**

- Checked: the exact or nearest value, or a failure.
- Saturating: clamp to the target's limits.
- Unspecified results out of range.

**Elsewhere.** Rust's `as` [rounds to nearest-even into a float and saturates
float-to-integer casts][rs-numcast]. Go truncates float-to-integer
[conversions][go-conv] and rejects the
[constant conversion `uint(-1)`][go-constexpr]. Julia raises
[`InexactError`][jl-numconv] for a value that does not fit, and Zig's
[`@intFromFloat`][zig-intfromfloat] is safety-checked.

**Decision.** The operand must have a numeric type; any other operand type,
such as `bool`, `char` or `String`, is a type error. A cast never wraps or
saturates.

| Cast | Result |
| --- | --- |
| integer to integer | Same value; a runtime error if it does not fit. |
| integer to float | Nearest value, ties to even. |
| `f32` to `f64` | Same value. |
| `f64` to `f32` | Nearest value, ties to even; too large gives an infinity. |
| float to integer | Truncated toward zero; a runtime error for NaN, an infinity or a result that does not fit. |
| same type | Unchanged. |

When the operand is a constant expression, a failing cast is a
constant-evaluation error instead ([record 39](diagnostics.md#d39)).

**Why.** Checked and explicit, and it keeps the tour's promises. A cast that
cannot give the right value stops the program instead of returning a
plausible wrong number.

**Consequences.**

```vortex
// statements: valid
let reading: f64 = -7.9;
let whole = i32(reading); // -7, truncated toward zero
```

```vortex
// statements: constant-evaluation error
let impossible = u32(-1); // constant-evaluation error: -1 does not fit in u32
```

Stage 5 rejects non-numeric operands and evaluates constant casts; stage 9
checks the casts that can fail. Changed: Types and values 4.12, Diagnostics
10.2, the tour, the cheat sheet, guide stages 5, 9 and 11.

## 30. "Compatible" types and implicit conversion {#d30}

**Status:** Accepted, 2026-09-23. **Applies to:** v0.1. **Resolves:** N12.
**Guide stage:** 5.

**Question.** What does "compatible" mean in the type rules, and does v0.1
ever change a value's type without a cast?

**Before this decision.**
[Expressions 5.5](../specification/expressions.md#55-arithmetic-and-bitwise-expressions)
and other sections required "compatible" types without defining the word, and
[Types and values 4.3](../specification/types-and-values.md#43-integers) said
implicit conversion rules "are not yet generalized". The tour's
[arithmetic section](../language-tour/08-expressions.md#arithmetic-expressions)
implied that conversions which lose no information might happen, while
[Runtime and numerical rules](../language-tour/06-runtime-and-numerical-rules.md#floating-point-behavior)
ruled out implicit integer-to-float conversion.

**Options.**

- The identical type, after literal typing.
- Lossless widening, such as `i32` to `f64`.
- C's usual arithmetic conversions.

**Elsewhere.** Go requires [identical types][go-typeid] for operands and
[assignment][go-assignability], apart from untyped constants. Swift requires
every [numeric conversion][sw-conv] to be written out. C and C++ convert mixed
operands through the [usual arithmetic conversions][cpp-uac].

**Decision.** Two types are compatible when they are equal under
[type equality](../specification/types-and-values.md#411-type-equality) once
each literal has its type ([record 31](#d31)). Vortex v0.1 has no implicit
conversions: operands, initializers, assignment values, arguments, return
values, array elements and field values must have exactly the required type,
and only a cast changes a value's type. A rule that accepts different types
says so, as the shift count ([record 22](operators.md#d22)) and the array
index ([record 12](arrays.md#d12)) do.

**Why.** One equality test is explicit and testable, with no promotion table
to learn. Widening can be added later without breaking a valid program, while
removing it later would break the programs that rely on it.

**Consequences.**

```vortex
// statements: type error
let count: i32 = 3;
let scale: f64 = 2.0;
let total = scale * count; // type error: f64 and i32 are different types
```

The fix is `scale * f64(count)`. Stage 5 compares types with type equality and
never inserts a conversion. Changed: the glossary, Types and values 4.3 and
4.11, Expressions 5.5, Arrays 7.3, the tour's expressions chapter, guide stage
5 and the word list.

## 31. Literal typing contexts {#d31}

**Status:** Accepted, 2026-09-23. **Applies to:** v0.1. **Resolves:** N12.
**Guide stage:** 5.

**Question.** When does a literal take a type other than its default, and which
contexts count?

**Before this decision.**
[Types and values 4.2](../specification/types-and-values.md#42-primitive-types)
said `u32`, `usize` and `f64` literals come from "context only", and 4.3 and
4.4 gave the `i32` and `f32` defaults, but no rule listed the contexts or said
whether a type passes through operators
([stage 5](../compiler/guide/stage-5-types-and-rules.md#literals-and-the-types-around-them)).

**Options.**

- A typed peer, then an expected type, then a default.
- Type inference across statements.
- Defaults only, with a cast for every other type.

**Elsewhere.** Go gives an untyped constant its type from context, or else a
[default type][go-const]. Rust [infers a literal's type and falls back to
`i32`][rs-litexpr]. Zig converts literals to a [peer operand's type][zig-peer]
or the [expected result type][zig-coercion].

**Decision.** A *literal-only* expression (literals combined only with
parentheses and operators) takes its type from the first of:

1. its **peer**: the other operand of an arithmetic, bitwise, equality or
   comparison operator, or the other range endpoint, when that one is not
   literal-only;
2. its **expected type**: a `let`'s written type, an assignment target, a
   parameter, the return type, an expected element or field type, or `usize`
   for a dimension. It passes through arithmetic and bitwise operators and the
   left operand of a shift;
3. its **default type**: `i32` for an integer literal, `f32` for a floating
   literal. Cast operands, indices, shift counts and `print` arguments expect
   no type.

A literal never changes kind: given a peer or expected type of the other kind,
it keeps its default type, and the usual type rules report the mismatch.

**Why.** Each expression is typed in one local pass, with no inference engine,
and the old defaults stay as they were.

**Consequences.**

```vortex
// statements: valid
let limit: u32 = 4000000000;
let next = limit - 1;     // 1 takes u32 from limit
let size: usize = 2 * 64; // both literals are usize
```

```vortex
// statements: type error
let ratio: f32 = 1; // type error: an integer literal never becomes f32
```

A cast operand gets no type, so `f64(0.1)` converts the `f32` nearest to 0.1;
write `let x: f64 = 0.1;` instead. Stage 5 applies the rule. Changed: Types
and values 4.2 to 4.4 (with a new
[literal typing](../specification/types-and-values.md#literal-typing) section),
Expressions 5.3, Statements 6.3, the grammar, the glossary, the tour, guide
stage 5.

## 32. Literal range and float rounding {#d32}

**Status:** Accepted, 2026-09-23. **Applies to:** v0.1. **Resolves:** N31.
**Guide stage:** 2, 5.

**Question.** What happens when a literal's value does not fit its type, and
how is a floating literal rounded?

**Before this decision.**
[Types and values 4.3](../specification/types-and-values.md#43-integers) let
context choose an integer type "when the value is representable" but gave no
category for a value that is not, even above the largest `u64`, which the lexer
accepts. Nothing covered rounding or overflow of floating literals
([4.4](../specification/types-and-values.md#44-floating-point-values),
[grammar](../specification/grammar.md#literals)).

**Options.**

- A type error at the literal, and one correct rounding to the final type.
- A lexical error above a fixed width, and floats read through `f64` first.
- Wrapping or clamping out-of-range literals.

**Elsewhere.** Go rejects a typed constant that its type
[cannot represent][go-repr]. Rust's `overflowing_literals` lint
[rejects an out-of-range literal by default][rs-lints].

**Decision.** Once a literal has its type ([record 31](#d31)), its value must
be representable in that type; otherwise the program has a type error at the
literal, however large the value. When a unary `-` is written directly before
an integer literal, the negated value is checked. A floating literal's exact
decimal value is rounded once, to nearest with ties to even, to its final
type. A finite floating literal that rounds to an infinity is a type error.
One too small for the type becomes a subnormal value (a tiny value stored with
reduced precision) or zero, and is valid.

**Why.** Every out-of-range literal gets one category, whatever its size. One
rounding step means `0.1` as an `f32` is the `f32` nearest to 0.1, not an
`f64` value rounded a second time.

**Consequences.**

```vortex
// statements: type error
let big: i32 = 2147483648; // type error: 2147483648 does not fit in i32
```

`let smallest: i32 = -2147483648;` is valid, and `let huge: f32 = 1.0e39;` is
a type error. Stage 2 keeps every literal's spelling and checks no range;
stage 5 checks the range and rounds. Changed: Types and values 4.3 and 4.4, the
grammar's literals, Lexical structure 2.5, Diagnostics 10.2 and 10.4, the
tour's variables chapter, guide stages 2 and 5.

## 42. `usize` width {#d42}

**Status:** Accepted, 2026-09-23. **Applies to:** v0.1. **Resolves:** N13.
**Guide stage:** 8, 9.

**Question.** How wide is `usize`, and who decides?

**Before this decision.**
[Types and values 4.3](../specification/types-and-values.md#43-integers) said
`usize` holds sizes "supported by the target architecture", the tour's
[integer section](../language-tour/04-variables-and-types.md#integer-types)
said its size "matches the machine the program is running on", and
[stage 8](../compiler/guide/stage-8-data-in-memory.md#memory-addresses-and-layout)
called it "target-sized". But
[Conformance 1.6](../specification/conformance.md#16-implementation-defined-behavior)
allows implementation-defined behavior (a choice each implementation must
document) only where the specification says so, and nothing said so here.

**Options.**

- Implementation-defined and listed, with 64 bits on every v0.1 target.
- Fixed at 64 bits by the language.
- Fixed at 32 bits.

**Elsewhere.** Rust's `usize` is [pointer-sized][rs-usize]. Go's `uint` is
[either 32 or 64 bits][go-numeric]. Swift's `Int` has
[the platform's word size][sw-int].

**Decision.** The width of `usize` is implementation-defined. An
implementation must document it, and the width is listed among the
implementation-defined behaviors in the conformance chapter
([record 55](documentation.md#d55)). Every v0.1 target uses 64 bits, so there
`usize` holds `0` through `2^64 - 1`.

**Why.** The width is a real property of the target, and the specification
should say so. Fixing it at 64 bits would force a breaking change for a future
32-bit target, and leaving it unlisted would break the conformance rule.

**Consequences.** Programs can observe the width, for example in the largest
`usize` value before an addition overflows, or in which values `u32(size)`
rejects. A conformance test whose result depends on the width must say so.
Stage 8 records the width in its layout document, and the overflow checks of
stage 9 use it. Changed: Types and values 4.2 and 4.3, the grammar's primitive
types, the conformance list, the tour's variables chapter, guide stages 5 and
8.

## 56. Floating-point strictness {#d56}

**Status:** Accepted, 2026-09-23. **Applies to:** v0.1. **Resolves:** N14.
**Guide stage:** 6, 10.

**Question.** May the compiler fuse, reorder or widen floating-point
operations?

**Before this decision.**
[Types and values 4.4](../specification/types-and-values.md#44-floating-point-values)
deferred to "the target's documented IEEE 754 support", and the
[tour](../language-tour/06-runtime-and-numerical-rules.md#floating-point-behavior)
promised no reordering. No page mentioned fused multiply-add (one instruction
computing `a * b + c` with a single rounding), flush-to-zero (replacing
subnormal values with zero) or extra precision, though C compilers may fuse
the
[stage 10](../compiler/guide/stage-10-matrix-multiplication.md#comparing-with-a-known-answer)
kernel's `sum += a[row, k] * b[k, column]` by default.

**Options.**

- Strict: each operation is one IEEE 754 operation, rounded to nearest-even.
- Allow fusion into fused multiply-adds.
- Whatever the target does.

**Elsewhere.** Go lets an implementation [fuse operations][go-float] unless an
explicit conversion forces rounding. C controls fusion with
[`#pragma STDC FP_CONTRACT`][c-pragma], and by default [GCC][gcc-fpcontract]
fuses outside strict ISO modes and [Clang][clang-fpcontract] within an
expression. Zig is [strict by default][zig-floatmode]; Julia fuses when asked,
through [`muladd`][jl-muladd].

**Decision.** Each `f32` or `f64` operation (unary `-`, binary `+`, `-`, `*`
and `/`, and each cast to a floating-point type) must produce the IEEE 754
binary32 or binary64 result, rounded to nearest with ties to even. An
implementation must not contract operations, reassociate or reorder them,
evaluate them in a wider format, or flush subnormal values to zero, and this
includes values computed during compilation. Relaxed modes may come later
only as an explicit opt-in.

**Why.** Golden outputs, the exact expected output of a test program, must be
reproducible: the same program prints the same digits with every conforming
compiler on every target. It also keeps the tour's promise and the
[philosophy's](../philosophy.md#3-do-not-surprise-the-programmer) rule that
speed must not quietly change an answer.

**Consequences.** In the stage 10 kernel the product is rounded, then the sum.
A back end must turn contraction off, for example with `-ffp-contract=off` for
a C compiler, and must not enable fast-math options. Stage 6 configures the
back end, and stage 10's known answers then hold bit for bit. Changed: Types
and values 4.4, Expressions 5.5, the tour, the philosophy, guide stages 6, 9
and 10.

[go-conv]: https://go.dev/ref/spec#Conversions
[go-predecl]: https://go.dev/ref/spec#Predeclared_identifiers
[rs-cast]: https://doc.rust-lang.org/reference/expressions/operator-expr.html#type-cast-expressions
[sw-conv]: https://docs.swift.org/swift-book/documentation/the-swift-programming-language/thebasics#Integer-and-Floating-Point-Conversion
[jl-special]: https://docs.julialang.org/en/v1/manual/integers-and-floating-point-numbers/#Special-floating-point-values
[go-float]: https://go.dev/ref/spec#Floating_point_operators
[rs-numcast]: https://doc.rust-lang.org/reference/expressions/operator-expr.html#numeric-cast
[go-constexpr]: https://go.dev/ref/spec#Constant_expressions
[jl-numconv]: https://docs.julialang.org/en/v1/manual/mathematical-operations/#Numerical-Conversions
[zig-intfromfloat]: https://ziglang.org/documentation/0.16.0/#intFromFloat
[go-typeid]: https://go.dev/ref/spec#Type_identity
[go-assignability]: https://go.dev/ref/spec#Assignability
[cpp-uac]: https://en.cppreference.com/cpp/language/usual_arithmetic_conversions
[go-const]: https://go.dev/ref/spec#Constants
[rs-litexpr]: https://doc.rust-lang.org/reference/expressions/literal-expr.html#integer-literal-expressions
[zig-peer]: https://ziglang.org/documentation/0.16.0/#Peer-Type-Resolution
[zig-coercion]: https://ziglang.org/documentation/0.16.0/#Type-Coercion
[go-repr]: https://go.dev/ref/spec#Representability
[rs-lints]: https://doc.rust-lang.org/rustc/lints/listing/deny-by-default.html
[rs-usize]: https://doc.rust-lang.org/reference/types/numeric.html#machine-dependent-integer-types
[go-numeric]: https://go.dev/ref/spec#Numeric_types
[sw-int]: https://docs.swift.org/swift-book/documentation/the-swift-programming-language/thebasics#Int
[c-pragma]: https://en.cppreference.com/c/preprocessor/impl
[gcc-fpcontract]: https://gcc.gnu.org/onlinedocs/gcc/Optimize-Options.html#index-ffp-contract
[clang-fpcontract]: https://clang.llvm.org/docs/UsersManual.html#cmdoption-ffp-contract
[zig-floatmode]: https://ziglang.org/documentation/0.16.0/#setFloatMode
[jl-muladd]: https://docs.julialang.org/en/v1/base/math/#Base.muladd

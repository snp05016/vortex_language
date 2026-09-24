# Arrays and shapes

These records settle the array questions that the v0.1 documents left open or
answered in conflicting ways: which dimensions are allowed, how indexes are
typed and checked, how a nested literal fills a multidimensional array, and how
arrays sit in memory. The [Arrays and shapes](../specification/arrays.md)
chapter and the other specification pages now state each rule; these records
explain why it was chosen and what changed.

## 10. Zero-length arrays {#d10}

**Status:** Accepted, 2026-09-23. **Applies to:** v0.1. **Resolves:** N22.
**Guide stage:** 5.

**Question.** May an array dimension be zero, as in `[f32; 0]` or
`[0.0; 4 - 4]`?

**Before this decision.**
[Arrays and shapes, 7.2](../specification/arrays.md#72-dimension-rules) called
the question open and told implementations to "diagnose it as unsupported".
The philosophy's
[feature decision worksheet](../philosophy.md#feature-decision-worksheet) asked
for "a positive `usize`-compatible value" in one row and deferred to "the
separately documented zero-extent policy" in the next.
[Milestone 5](../roadmap.md#milestone-5-types-mutability-and-control-flow-checks)
could not be completed until that policy existed.

**Options.**

- Reject every zero extent.
- Allow zero, which creates arrays with no elements.

**Elsewhere.** Go allows an array length of zero or more ([Go][go-array]). C
and C++ require an array size greater than zero ([C][c-array],
[C++][cpp-array]).

**Decision.** Every extent of every array type and repeat array must be at
least 1. A dimension whose value is zero, written (`[f32; 0]`) or computed
(`[f32; 4 - 4]`), is a constant-evaluation error. The rule applies to every
position of a multidimensional type, such as `[f32; 4, 0]`, and to nested
element types, such as `[[f32; 0]; 3]`.

**Why.** An element list can never be empty, because `[]` is not an expression
in v0.1, so an empty array could only come from a type or a repeat array.
Rejecting zero removes zero-size layouts and arrays that no index can reach.
Allowing zero later would not change the meaning of any program that is valid
now.

**Consequences.** Every v0.1 array has at least one element along each
dimension. Stage 5 reports the error while it evaluates dimensions
([item 52](#d52)); the parser still accepts `[f32; 0]`, and stage 8 never sees
a zero extent.

```vortex
// statements: constant-evaluation error
let empty = [0.0; 4 - 4]; // the extent evaluates to zero
```

Pages changed: Arrays and shapes 7.2, the grammar, tour chapter 4, the
philosophy, roadmap, README, cheat sheet and parser design notes, and guide
stages 3, 5, 8 and 11.

## 11. Constant dimension expressions {#d11}

**Status:** Accepted, 2026-09-23. **Applies to:** v0.1. **Resolves:** N27; N2
in part. **Guide stage:** 4, 5.

**Question.** What may an array dimension contain, given that v0.1 has no
named constants?

**Before this decision.**
[Arrays and shapes, 7.2](../specification/arrays.md#72-dimension-rules)
required dimensions "known at compile time" without defining that, and
[Declarations, 3.8](../specification/declarations.md#38-excluded-declaration-forms)
excludes constants. Yet the [grammar](../specification/grammar.md#types) and
the [philosophy](../philosophy.md#feature-decision-worksheet) used names such
as `rows * columns`, calling the failure a semantic or a constant-evaluation
error.

**Options.**

- Literals and arithmetic only.
- A `const` declaration in v0.1.
- Immutable `let` bindings with constant initializers.

**Elsewhere.** A Go array length must be a constant expression
([Go arrays][go-array], [Go constant expressions][go-constexpr]). Rust rejects
a `let` binding used as a length ([E0435][rs-e0435]), and Zig computes such
values with `comptime` ([Zig][zig-comptime]).

**Decision.** An *integer constant expression* is an integer literal,
optionally preceded by unary `-`, or integer constant expressions combined with
parentheses and the binary operators `+`, `-`, `*`, `/` and `%`. Names and
calls never are, even immutable variables. Every dimension must be one,
whatever its type, or the program has a constant-evaluation error. Its literals
have type `usize`, and it is evaluated with checked `usize` arithmetic; leaving
the `usize` range, dividing by zero, or making the array larger than the
implementation's limit is also a constant-evaluation error.

**Why.** It is the smallest rule that fits a language without constants, and
dimensions can be evaluated as soon as a type needs them ([item 52](#d52)). A
`const` declaration, planned as the first addition after v0.1, can widen it
without breaking programs.

**Consequences.** Names are resolved first, so an undeclared name in a
dimension is a name error. A literal that does not fit `usize`, such as `-1`,
is a type error ([item 32](numbers.md#d32)). [Item 39](diagnostics.md#d39)
reuses this definition for constant operands.

```vortex
// statements: constant-evaluation error
let tile = 4;
let row = [0.0; tile * 2]; // tile is a name, never constant
```

Pages changed: eight specification pages, including a new Expressions section;
tour chapters 1, 4, 8 and 10; the cheat sheet, philosophy, roadmap, README and
parser notes; guide stages 3 to 5.

## 12. Index type and bounds {#d12}

**Status:** Accepted, 2026-09-23. **Applies to:** v0.1. **Resolves:** none.
**Guide stage:** 5, 9.

**Question.** Which types may index an array, and when is an out-of-bounds
index rejected before the program runs?

**Before this decision.**
[Arrays and shapes, 7.6](../specification/arrays.md#76-indexing) asked for "a
supported integer type" without listing types, ignored negative indexes, and
rejected out-of-bounds indexes when "provable", so acceptance depended on the
compiler. Guide
[stage 5](../compiler/guide/stage-5-types-and-rules.md#indexes-are-checked-here-too)
called a constant index past the end a type error.

**Options.**

- Any integer type: `i32`, `u32` or `usize`.
- `usize` only, as in Rust.
- `i32` only.

**Elsewhere.** Go requires a constant index to be non-negative and in range,
and panics on any other out-of-range index ([Go][go-index]). Rust indexes with
`usize`, reports a constant out-of-bounds index through a deny-by-default lint,
and otherwise panics ([Rust][rs-index], [rustc lints][rs-lints]).

**Decision.** Each index must have an integer type (`i32`, `u32` or `usize`);
the indices of one access may differ in type, and any other index type is a
type error. An index `i` is in bounds for an extent `n` when `0 <= i < n`,
compared as mathematical integers, so a negative index is out of bounds. An
index that is an integer constant expression ([item 11](#d11)) must be checked
during compilation; outside its extent it is a constant-evaluation error. Any
other index is checked at run time, where an out-of-bounds index is a runtime
error.

**Why.** Kernel loop counters are usually `i32`, so any integer type avoids a
cast on every access, and tying rejection to constant expressions gives every
compiler the same accepted programs.

**Consequences.** `values[3]` on a three-element array is always rejected;
`values[i]` always compiles with a runtime check, even if an optimizer could
prove it fails ([item 39](diagnostics.md#d39)). Stage 5 checks index types and
constant indexes; stage 9 adds the runtime checks.

```vortex
// statements: constant-evaluation error
let values = [10, 20, 30];
let last = values[3]; // index 3 is outside extent 3
```

Pages changed: Arrays and shapes 7.6, Expressions 5.9, Diagnostics, the
grammar, tour chapters 6 and 8, the cheat sheet, and guide stages 5, 9, 10 and
11.

## 21. Nested literal for a multidimensional array {#d21}

**Status:** Accepted, 2026-09-23. **Applies to:** v0.1. **Resolves:** none.
**Guide stage:** 5, 8.

**Question.** Can a list of lists, such as `[[1.0, 2.0], [3.0, 4.0]]`,
initialize a multidimensional array such as `[f32; 2, 2]`?

**Before this decision.**
[Types and values, 4.11](../specification/types-and-values.md#411-type-equality)
makes the nested type `[[f32; 2]; 2]` and the multidimensional type
`[f32; 2, 2]` different types, and
[Arrays and shapes, 7.3](../specification/arrays.md#73-element-list-construction)
gave no rule connecting them. Yet the
[tour](../language-tour/04-variables-and-types.md#fixed-size-arrays)
initialized a `[f32; 2, 2]` with a list of lists, and guide
[stage 10](../compiler/guide/stage-10-matrix-multiplication.md#traps) advised
avoiding the question.

**Options.**

- Allow a nested literal when the expected type is multidimensional.
- Allow only flat literals such as `[1.0, 2.0, 3.0, 4.0]`.
- Add a row separator, as Julia does with `;`.

**Elsewhere.** Julia builds matrices with concatenation syntax
([Julia][jl-arraylit]). Go writes nested composite literals
([Go][go-complit]), and Zig builds multidimensional arrays by nesting
([Zig][zig-multiarray]).

**Decision.** When an element-list literal has an expected type
`[T; d1, d2, ..., dn]` with n at least 2, it must have exactly d1 elements, each
checked against `[T; d2, ..., dn]`. Expected types come from the contexts of
[item 31](numbers.md#d31), such as an annotated `let`, a parameter or a return
type. Otherwise a list of lists has a nested array type. Nested and
multidimensional values never convert into each other; a mismatch is a type
error. A repeat array always has the type its dimensions spell.

**Why.** It keeps the tour's readable matrix literal, and the written type
still decides which kind of array the value is.

**Consequences.** Stage 5 checks such a literal row by row; a row of the wrong
length is a type error. Stage 8 stores the result like any `[f32; 2, 2]`.

```vortex
// statements: type error
let matrix: [f32; 2, 2] = [[1.0, 2.0], [3.0, 4.0]]; // accepted
let grid = [[1.0, 2.0], [3.0, 4.0]]; // type [[f32; 2]; 2]
let copy: [f32; 2, 2] = grid; // type error: no conversion
```

Pages changed: Arrays and shapes 7.3 and 7.4, Types and values 4.10 and 4.11,
tour chapter 4, and guide stages 8, 10 and 11.

## 43. Memory layout {#d43}

**Status:** Accepted, 2026-09-23. **Applies to:** v0.1. **Resolves:** none.
**Guide stage:** 8.

**Question.** In what order are the elements of a multidimensional array, and
the fields of a struct, stored in memory?

**Before this decision.**
[Arrays and shapes, 7.8](../specification/arrays.md#78-memory-and-layout) said
the exact layout and ordering were "not yet part of the public v0.1
specification", and [Structs, 8.8](../specification/structs.md#88-layout) left
field offsets open. Guide
[stage 8](../compiler/guide/stage-8-data-in-memory.md#arrays-in-memory) asked
each implementer to pick row-major or column-major order, although the speed of
the matrix loops in stage 10 depends on that choice.

**Options.**

- Row-major order: the last index varies fastest.
- Column-major order: the first index varies fastest.
- Leave the order to each implementation.

**Elsewhere.** Julia stores arrays column by column and treats loop order as a
performance rule ([Julia][jl-colmajor]). Rust specifies its array layout
([Rust][rs-layout]), and C++ array elements are contiguous ([C++][cpp-array]).

**Decision.** An array of type `[T; d1, ..., dn]` must be stored contiguously,
with no gap between elements, in row-major order: the last index varies
fastest. For `[f32; 2, 3]` the order is `[0, 0]`, `[0, 1]`, `[0, 2]`, `[1, 0]`,
`[1, 1]`, `[1, 2]`. A nested array `[[T; d2]; d1]` is stored the same way.
Struct fields must be stored in declaration order.
Sizes, alignment and padding are implementation-defined and must be
documented.

**Why.** A v0.1 program cannot observe addresses, so the layout never changes
what a program computes. It does decide which loop order is fast, and the
[philosophy](../philosophy.md#6-explain-performance-decisions) says programmers
should not have to guess why a program is slow. Row-major order matches the way
`[f32; rows, columns]` reads and the way C stores an array of arrays.

**Consequences.** A loop that varies the last index innermost reads
neighbouring elements. Stage 8 still writes a layout document
([I6](implementation.md#i6)) with sizes, alignment and padding, but it no
longer chooses the order, and it may not reorder fields to save padding. The
implementation-defined list in
[Conformance](../specification/conformance.md) gains the padding and alignment
entries ([item 55](documentation.md#d55)).

Pages changed: Arrays and shapes 7.8, Structs 8.8, Conformance, the glossary,
tour chapter 4, the roadmap, and guide stages 8 and 10.

## 47. Index arity and partial indexing {#d47}

**Status:** Accepted, 2026-09-23. **Applies to:** v0.1. **Resolves:** N19.
**Guide stage:** 5.

**Question.** Must an index expression supply one index per dimension, or may
it select a whole row?

**Before this decision.**
[Arrays and shapes, 7.6](../specification/arrays.md#76-indexing) said partial
indexing was "not specified in v0.1", while the
[tour](../language-tour/08-expressions.md#array-expressions) and guide
[stage 5](../compiler/guide/stage-5-types-and-rules.md#indexes-are-checked-here-too)
already treated `matrix[row]` on a two-dimensional array as an error.

**Options.**

- The number of indices must equal the rank (the number of dimensions).
- Allow fewer indices, producing a row or another sub-array.
- Allow one linear index into an array of any rank.

**Elsewhere.** Go arrays are one-dimensional and are composed to form more
dimensions, so each level takes its own index ([Go][go-array]). Julia allows
both linear and partial indexing ([Julia][jl-arrayindex]).

**Decision.** An index suffix must contain exactly as many indices as the rank
of the array it indexes; any other count is a type error. A nested array takes
one suffix per level: for `grid` of type `[[i32; 2]; 3]`, `grid[1][0]` is valid
and `grid[1, 0]` is a type error. Partial indexing, linear indexing and slices
are not part of v0.1.

**Why.** It matches what the tour and the guide already taught, and every index
expression then names one element with a known type. Partial indexing can
arrive later, together with slices, without changing any valid program.

**Consequences.** Stage 5 compares the number of indices with the rank when it
checks an index expression.

```vortex
// statements: type error
let matrix = [0.0; 2, 3];
let row = matrix[1]; // a rank-2 array needs two indices
```

Pages changed: Arrays and shapes 7.6, Expressions 5.9, the grammar, tour
chapter 8, the cheat sheet, and guide stage 5.

## 52. When dimensions are evaluated {#d52}

**Status:** Accepted, 2026-09-23. **Applies to:** v0.1. **Resolves:** N6.
**Guide stage:** 5.

**Question.** At what point does the compiler evaluate array dimensions, given
that comparing array types needs their values?

**Before this decision.** The phase table in the
[specification overview](../specification/index.md#compiler-phase-boundaries)
and the pipeline on the
[architecture page](../compiler/architecture.md#pipeline) put constant
evaluation after type checking. But
[Arrays and shapes, 7.2](../specification/arrays.md#72-dimension-rules), the
[grammar](../specification/grammar.md#types) and guide
[stage 5](../compiler/guide/stage-5-types-and-rules.md#array-dimensions-and-constant-evaluation)
need the values before types are compared: `[f32; 2 * 2]` and `[f32; 4]` are
the same type only once `2 * 2` is known to be 4.

**Options.**

- Evaluate each dimension when the type checker resolves the array type.
- Add a separate constant-evaluation pass that runs before type checking.

**Elsewhere.** In Go the length is part of the array type ([Go][go-array]). In
Rust an array length must be a constant, and a `let` binding used there is
rejected ([E0435][rs-e0435]). Zig evaluates such values at compile time with
`comptime` ([Zig][zig-comptime]).

**Decision.** The compiler must evaluate every dimension when it resolves an
array type (turns the written type into the type it denotes), before it
compares types, lays out storage or generates code. A failure is a
constant-evaluation error, whichever phase finds it. The later
constant-evaluation phase handles only the checked operations of
[item 39](diagnostics.md#d39).

**Why.** By [item 11](#d11) a dimension holds only literals and arithmetic, so
it can be evaluated the moment its type is needed, without a separate pass.
The documented phase order then matches what type equality requires.

**Consequences.** The category of a dimension error and the phase that finds it
differ, as
[Diagnostics, 10.3](../specification/diagnostics.md#103-error-phase-versus-category)
allows. Programs do not change; only the documented order does.

Pages changed: the specification overview, Arrays and shapes 7.2 and 7.5,
Types and values 4.11, the grammar, Diagnostics 10.3, the architecture page,
the parser design notes, tour chapters 4 and 8, the tour overview, the cheat
sheet, and guide stages 3, 5 and 8.

[go-array]: https://go.dev/ref/spec#Array_types
[c-array]: https://en.cppreference.com/c/language/array
[cpp-array]: https://en.cppreference.com/cpp/language/array
[go-constexpr]: https://go.dev/ref/spec#Constant_expressions
[rs-e0435]: https://doc.rust-lang.org/error_codes/E0435.html
[zig-comptime]: https://ziglang.org/documentation/0.16.0/#comptime
[go-index]: https://go.dev/ref/spec#Index_expressions
[rs-index]: https://doc.rust-lang.org/reference/expressions/array-expr.html#array-and-slice-indexing-expressions
[rs-lints]: https://doc.rust-lang.org/rustc/lints/listing/deny-by-default.html
[jl-arraylit]: https://docs.julialang.org/en/v1/manual/arrays/#man-array-literals
[go-complit]: https://go.dev/ref/spec#Composite_literals
[zig-multiarray]: https://ziglang.org/documentation/0.16.0/#Multidimensional-Arrays
[jl-colmajor]: https://docs.julialang.org/en/v1/manual/performance-tips/#man-performance-column-major
[rs-layout]: https://doc.rust-lang.org/reference/type-layout.html#array-layout
[jl-arrayindex]: https://docs.julialang.org/en/v1/manual/arrays/#man-array-indexing

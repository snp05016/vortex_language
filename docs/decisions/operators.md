# Operators, structs and void

These records cover how struct values are built and declared, how the shift,
division, remainder and comparison operators behave, and where `void` may
appear. The [specification](../specification/index.md) chapters now state
each rule; each record below explains why the rule was chosen and what it
changed.

## 7. Struct expression errors and kind misuse {#d7}

**Status:** Accepted, 2026-09-23. **Applies to:** v0.1. **Resolves:** N33.
**Guide stage:** 4, 5.

**Question.** A **struct expression** builds a struct value field by field, as
in `Point { x: 1.0, y: 2.0 }`. Which error category applies when it lists the
wrong fields, or when a name is used as the wrong kind of thing?

**Before this decision.** [Structs](../specification/structs.md#83-struct-construction)
required each field exactly once but named no category, and
[guide stage 4](../compiler/guide/stage-4-names-and-scopes.md#names-that-wait-for-types)
let either of two stages check. [Diagnostics](../specification/diagnostics.md#102-categories)
gave no category to `Point(1.0, 2.0)`, `let f = add;` or `add { x: 1 }`. The
[expressions tour](../language-tour/08-expressions.md#expression-quick-reference)
and the [cheat sheet](../language-and-compiler-cheatsheet.md#expression-validity-at-a-glance)
showed `Point { x: 1.0 }`, which leaves out `y`.

**Options.**

- Name errors, because a field name fails to resolve.
- Type errors, because the check needs the struct's definition.
- Each compiler chooses.

**Elsewhere.** Rust rejects a [missing field][rs-e0063] and an
[unknown field][rs-e0560] in a [struct expression][rs-structexpr].
[Go lets a composite literal leave fields out][go-complit], and
[C++ aggregate initialization fills in missing members][cpp-aggr].

**Decision.** A struct expression must give every field of its struct exactly
once, in any order. A missing, unknown or repeated field is a type error. A
struct-expression name that does not resolve is a name error. A name that
resolves to the wrong kind of declaration is a type error: a struct or a
variable (local, parameter or loop variable) used as a callee; a function or a
variable used as a struct-expression name or as a type; a function or a struct
used as a value.

**Why.** Each error comes from the phase that has the facts. Name resolution
knows whether `Point` exists; which fields it has, and what a declaration can
be used for, are facts about types.

**Consequences.**

```vortex
// program: type error
struct Point { x: f32, y: f32 }

fn main() {
    let p = Point { x: 1.0 }; // type error: field y is missing
}
```

Field values are evaluated in the order written ([record 38](statements.md#d38)).
A repeated field in a struct *declaration* stays a name error. Stage 4
resolves names and records kinds; stage 5 checks fields and kinds. Pages changed: structs, expressions, diagnostics, grammar, the
expressions tour, the cheat sheet, guide stages 4 and 5.

## 22. Shift semantics {#d22}

**Status:** Accepted, 2026-09-23. **Applies to:** v0.1. **Resolves:** none.
**Guide stage:** 5, 6, 9.

**Question.** `x << n` shifts the bits of `x` left by `n` places, and `x >> n`
shifts them right. Which counts are valid, what type may `n` have, and does
`>>` keep a negative `x` negative?

**Before this decision.** [Expressions](../specification/expressions.md#55-arithmetic-and-bitwise-expressions)
said invalid shift counts are checked, but the runtime checks in
[Diagnostics](../specification/diagnostics.md#runtime-error), the philosophy's
[initial scope](../philosophy.md#initial-scope) and
[Milestone 9](../roadmap.md#milestone-9-runtime-safety) left shifts out, and
[guide stage 9](../compiler/guide/stage-9-runtime-safety.md#integer-overflow)
called the question open. The count's type and the meaning of `>>` were
unstated.

**Options.**

- Any non-negative count; shifting by the bit width or more gives 0.
- A count below the bit width, checked.
- Large counts left undefined.

**Elsewhere.** [Go requires a non-negative count, sets no upper limit and
shifts signed values arithmetically][go-intops].
[Rust treats a count of at least the bit width as overflow][rs-overflow], and
[C++ leaves such counts undefined][cpp-arith].

**Decision.** Both operands must be integers. The result has the left operand's
type; the count may have any integer type, and a literal count has type `i32`.
The count must be at least 0 and less than the result type's bit width: 32 for
`i32` and `u32`, 64 for `usize` on every v0.1 target
([record 42](numbers.md#d42)). Any other count is a runtime error, or a
constant-evaluation error for a constant count ([record 39](diagnostics.md#d39)).
`<<` fills with zeros and drops the bits shifted out; that is not overflow.
`>>` is arithmetic for signed types (it copies the sign bit) and logical for
unsigned ones.

**Why.** The specification already promised a check. A fixed range turns
C++'s undefined case into one testable error, and an arithmetic `>>` halves
negative numbers too, rounding down.

**Consequences.**

```vortex
// statements: valid
let a: i32 = -7 >> 1; // -4, while -7 / 2 is -3
let b: i32 = 1 << 31; // -2147483648: no error
let c: u32 = 1 << 31; // 2147483648
```

Stage 5 checks types and constant counts, stage 6 generates both kinds of
`>>`, and stage 9 checks other counts at run time. Pages changed: expressions,
diagnostics, the philosophy, the roadmap, the runtime and expressions tours,
the cheat sheet, guide stages 9 and 11.

## 26. Empty structs {#d26}

**Status:** Accepted, 2026-09-23. **Applies to:** v0.1. **Resolves:** none.
**Guide stage:** 3.

**Question.** May a struct declare no fields, as in `struct Marker {}`?

**Before this decision.** The [grammar](../specification/grammar.md#struct-definitions-and-fields)
and [Programs and declarations](../specification/declarations.md#34-struct-declarations)
accepted it, and [Structs](../specification/structs.md#82-empty-structs) made
its storage implementation-defined. Yet a struct expression needs at least one
field, so nothing could build a `Marker`, and
[section 8.3](../specification/structs.md#83-struct-construction) called the
mismatch an open issue. The [declarations tour](../language-tour/10-declarations.md#struct-declarations)
allowed "zero or more" fields, the [parser design](../compiler/parser-design.md#73-struct-declaration)
listed `struct Empty {}` as valid, and guide stages
[8](../compiler/guide/stage-8-data-in-memory.md#structs-in-memory) and
[11](../compiler/guide/stage-11-release.md#writing-down-what-the-compiler-does)
carried the question.

**Options.**

- Allow empty structs, with a size of zero and a new expression `Marker {}`.
- Require at least one field.

**Elsewhere.** [Go allows `struct {}`][go-struct], and
[Zig has zero-bit types, which occupy no memory][zig-zerobit]. Vortex takes
the smaller rule.

**Decision.** A struct declaration must declare at least one field. The
grammar requires one, so `struct Marker {}` is a syntax error.

**Why.** An empty struct would need a zero-size layout, a rule for arrays of
zero-size elements and a new expression form, all for a value that holds no
data. Requiring a field removes all three, and allowing empty structs later
would not break any program written under this rule.

**Consequences.**

```vortex
// items: syntax error
struct Marker {} // a struct needs at least one field
```

The parser (stage 3) reports the error at the `}`. Layout (stage 8) never
meets a zero-size struct, and the list of implementation-defined behavior
loses the storage of empty structs. Pages changed: the grammar, declarations
and structs chapters, the declarations tour, the parser design, and guide
stages 3, 8 and 11.

## 33. Integer division and remainder {#d33}

**Status:** Accepted, 2026-09-23. **Applies to:** v0.1. **Resolves:** N11.
**Guide stage:** 5, 6, 9.

**Question.** Which way does integer `/` round, which sign does `%` take, and
what happens when the smallest value of a signed type (`MIN`, -2147483648 for
`i32`) is divided by -1?

**Before this decision.** [Expressions](../specification/expressions.md#55-arithmetic-and-bitwise-expressions)
and [Diagnostics](../specification/diagnostics.md#runtime-error) said division
and remainder by zero, and overflow, are checked, but not how `/` rounds, which
sign `%` takes, or what `MIN / -1` and `MIN % -1` give.
[Guide stage 9](../compiler/guide/stage-9-runtime-safety.md#integer-division-and-remainder-by-zero)
called `MIN / -1` an overflow, and its checklist paired that check with both
operators.

**Options.**

- Round toward zero; `%` takes the sign of the dividend (the left operand).
- Round down; `%` takes the sign of the divisor.
- `MIN / -1` fails or wraps to `MIN`; `MIN % -1` fails or gives 0.

**Elsewhere.** [Go truncates and lets `MIN / -1` wrap][go-arith].
[Rust truncates][rs-arith] and [treats `MIN / -1` as overflow][rs-overflow],
and [in C++ `INT_MIN % -1` is undefined behavior][cpp-arith].

**Decision.** Integer `/` truncates toward zero. The result of `%` has the sign
of the dividend, so `(a / b) * b + a % b` equals `a` whenever `a / b` is
defined. A zero divisor is a runtime error for both operators. `MIN / -1` is an
overflow runtime error. `MIN % -1` is 0 and is not an error. When the operands
that decide a failure are constant expressions, the failure is a
constant-evaluation error instead ([record 39](diagnostics.md#d39)).

**Why.** An operation fails only when its true result cannot be represented.
The true quotient of `MIN / -1` is one more than the largest `i32`; the true
remainder is 0, which fits.

**Consequences.**

```vortex
// statements: valid
let q = -7 / 2; // -3
let r = -7 % 2; // -1
let s = 7 % -2; // 1
```

Unsigned division never overflows. Stage 5 evaluates constant cases, stage 6
generates truncating division, and stage 9 adds the zero and `MIN / -1` checks
and makes `MIN % -1` give 0, even where a divide instruction would fault.
Pages changed: expressions, diagnostics, the runtime and expressions tours, the
cheat sheet, guide stage 9.

## 35. Equality and ordering per type {#d35}

**Status:** Accepted, 2026-09-23. **Applies to:** v0.1. **Resolves:** N8.
**Guide stage:** 5, 7.

**Question.** Which types can be compared with `==` and `!=`, and which can be
ordered with `<`, `<=`, `>` and `>=`?

**Before this decision.** [Expressions](../specification/expressions.md#56-comparison-and-equality)
allowed equality "where their type defines equality", but no type defined it.
Ordering needed "compatible ordered operands" that no page listed; only strings
and structs were ruled out. The [types chapter](../specification/types-and-values.md)
had no list of operations per type.

**Options.**

- Scalars only.
- Scalars, plus element-by-element equality for arrays and structs, and string
  comparison.
- Comparison defined per type in the program, which v0.1 cannot express.

**Elsewhere.** [Go compares arrays and structs element by element][go-cmp].
[Rust defines comparison through the `PartialEq` and `PartialOrd` traits][rs-cmp],
interfaces that each type implements.

**Decision.** `==` and `!=` take two operands of the same type
([record 30](numbers.md#d30)), which must be `bool`, `char`, an integer type or
a floating-point type. `<`, `<=`, `>` and `>=` take two operands of the same
integer or floating-point type. Floating-point comparison follows IEEE 754:
NaN (the "not a number" value) is unequal to every value, itself included;
every ordering with NaN is false; and `0.0 == -0.0` is true. `String`, arrays
and structs have no equality or ordering; comparing them is a type error. Every
comparison produces `bool`.

**Why.** It is the smallest useful set, and it hides no loops: equality on a
`[f32; 1024, 1024]` would run a million comparisons behind one operator.

**Consequences.**

```vortex
// statements: type error
let a = [1, 2];
let b = [1, 2];
let same = a == b; // type error: arrays have no equality
```

`char` values can be tested for equality but not ordered. A reference-typed
name used as an operand reads the value it refers to
([record 40](references.md#d40)), so an address is never compared. Stage 5
checks operand types; stage 7 generates comparisons with the NaN results.
Pages changed: the expressions, types and grammar chapters, the expressions
tour, the cheat sheet, and guide stage 5.

## 37. Chained comparisons and bitwise precedence {#d37}

**Status:** Accepted, 2026-09-23. **Applies to:** v0.1. **Resolves:** N7.
**Guide stage:** 3, 5.

**Question.** What do `a < b < c` and `a == b == c` mean, and how does
`a & b == c` group?

**Before this decision.** The [grammar](../specification/grammar.md#expressions-and-precedence)
folded comparisons to the left, like `+`, and left the rest to type checking,
so `a == b == c` was accepted whenever `c` was a `bool`.
[Guide stage 3](../compiler/guide/stage-3-parser-and-tree.md#precedence-and-associativity)
said type checking rejects such chains. Following C, the
[precedence table](../specification/expressions.md#52-precedence-and-associativity)
put `&` below `==`, so `a & b == c` groups as `a & (b == c)`.

**Options.**

- Keep left folding; type checking rejects what does not fit.
- Mathematical chains: `a < b < c` means `a < b && b < c`.
- **Non-associative** comparisons: a chain needs parentheses.
- For `&`, `^` and `|`: keep C's order, or rank them above comparisons.

**Elsewhere.** [Rust requires parentheses to chain comparisons][rs-cmp], while
[Julia chains them as in mathematics][jl-chain].
[Go ranks `&` above the comparisons][go-prec]; [C ranks it below][c-prec].

**Decision.** `==`, `!=`, `<`, `<=`, `>` and `>=` do not associate: an operand
of one of them must not be an unparenthesized equality or comparison
expression, or it is a syntax error. So `a < b < c`, `a == b == c` and
`a < b == c` are syntax errors, while `(a < b) == c` is valid when `c` is a
`bool`. The precedence levels, including C's order for `&`, `^` and `|`, do
not change.

**Why.** One parse rule removes the conflict between the grammar and the
guide. A reader who knows Julia expects a chain and one who knows C expects
left grouping; a syntax error misleads neither.

**Consequences.**

```vortex
// statements: syntax error
let i = 5;
let inside = 0 <= i < 10; // syntax error: comparisons do not chain
```

`a & b == c` still parses as `a & (b == c)`, a type error because `&` needs
integers; the diagnostic should suggest `(a & b) == c`. Stage 3 rejects
chains; stage 5 reports the `&` case. Pages changed: grammar, expressions,
glossary, the parser design, the expressions tour, the cheat sheet, guide
stages 3 and 5.

## 44. `void` values {#d44}

**Status:** Accepted, 2026-09-23. **Applies to:** v0.1. **Resolves:** N16.
**Guide stage:** 5.

**Question.** Where may the type `void` be written, and where may a call to a
`void` function appear?

**Before this decision.** [Types and values](../specification/types-and-values.md#42-primitive-types)
said `void` cannot be stored, but the [grammar](../specification/grammar.md#functions-and-parameters)
allowed "any v0.1 type as a parameter", and its type checks allowed `void`
"where a no-value type makes sense". `let x = f();` and `return f();` for a
`void` function `f` were unspecified; the
[expressions tour](../language-tour/08-expressions.md#function-call-expressions)
called storing a `void` result invalid, and the
[statements tour](../language-tour/09-statements.md#expression-statements)
allowed the call as a statement.

**Options.**

- `void` only as a return type, and `void` calls only as statements.
- A unit type: one value that can be stored, passed and returned.
- The first option, plus `return f();` inside a `void` function.

**Elsewhere.** [In Go, a call to a function without results may appear only as
a statement][go-exprstmt]. [C++ allows `return f();` in a `void`
function][cpp-return].

**Decision.** `void` may appear only as a function's return type, written as
`-> void` or implied by leaving the return type out. Anywhere else (a
parameter, a `let` annotation, a struct field, an array element) it is a type
error. A call to a `void` function may appear only as the whole expression of
an expression statement, optionally in parentheses. Anywhere else, including
`let x = f();`, `return f();` and `g(f())`, it is a type error.

**Why.** Anything more needs a unit value, and v0.1 has none to define. The
rule can grow into one later without breaking a valid program.

**Consequences.**

```vortex
// program: type error
fn log() {
    print("ready");
}

fn main() {
    log();            // valid: an expression statement
    let done = log(); // type error: log returns void
}
```

Stage 5 checks both rules. Pages changed: the types, grammar, declarations,
expressions and statements chapters, the variables, expressions and
statements tours, the cheat sheet, and guide stages 5 and 7.

## 45. Recursive structs {#d45}

**Status:** Accepted, 2026-09-23. **Applies to:** v0.1. **Resolves:** N17.
**Guide stage:** 5.

**Question.** May a struct contain itself, directly or through other fields?

**Before this decision.** [Structs](../specification/structs.md#81-declaration)
had no rule, and the [grammar](../specification/grammar.md#struct-definitions-and-fields)
asked only that every field type resolve. Neither `struct A { next: A }` nor a
cycle through an array field was addressed.

**Options.**

- Reject any cycle of fields held by value.
- Reject only a field whose type is the struct itself.
- Leave it to layout, which would never finish.

**Elsewhere.** [Go forbids a struct from containing itself, directly or through
arrays][go-struct]. Rust requires [recursive types][rs-rectypes] to have a
[finite size][rs-e0072], and [C forbids members of incomplete type][c-struct].

**Decision.** A struct must not contain itself by value, directly or through
any chain of field types and array element types. Such a cycle is a type
error.

**Why.** A struct's size must be finite. A struct that contains itself would
need infinite storage.

**Consequences.**

```vortex
// items: type error
struct Tree {
    value: i32,
    children: [Tree; 2], // type error: Tree contains itself
}
```

Two structs that contain each other, such as `struct A { b: B }` and
`struct B { a: A }`, form the same kind of cycle. References cannot be struct
fields in v0.1 ([record 41](references.md#d41)), so no recursive struct is
valid. The diagnostic should name the fields that close the cycle. Stage 5
checks this before layout, so stage 8 always finds a finite size. Pages
changed: the structs and grammar chapters, the declarations tour, and guide
stages 5 and 8.

[rs-e0063]: https://doc.rust-lang.org/error_codes/E0063.html
[rs-e0560]: https://doc.rust-lang.org/error_codes/E0560.html
[rs-structexpr]: https://doc.rust-lang.org/reference/expressions/struct-expr.html
[go-complit]: https://go.dev/ref/spec#Composite_literals
[cpp-aggr]: https://en.cppreference.com/cpp/language/aggregate_initialization
[go-intops]: https://go.dev/ref/spec#Integer_operators
[rs-overflow]: https://doc.rust-lang.org/reference/expressions/operator-expr.html#overflow
[cpp-arith]: https://en.cppreference.com/cpp/language/operator_arithmetic
[go-struct]: https://go.dev/ref/spec#Struct_types
[zig-zerobit]: https://ziglang.org/documentation/0.16.0/#Zero-Bit-Types
[go-arith]: https://go.dev/ref/spec#Arithmetic_operators
[rs-arith]: https://doc.rust-lang.org/reference/expressions/operator-expr.html#arithmetic-and-logical-binary-operators
[go-cmp]: https://go.dev/ref/spec#Comparison_operators
[rs-cmp]: https://doc.rust-lang.org/reference/expressions/operator-expr.html#comparison-operators
[jl-chain]: https://docs.julialang.org/en/v1/manual/mathematical-operations/#Chaining-comparisons
[go-prec]: https://go.dev/ref/spec#Operator_precedence
[c-prec]: https://en.cppreference.com/c/language/operator_precedence
[go-exprstmt]: https://go.dev/ref/spec#Expression_statements
[cpp-return]: https://en.cppreference.com/cpp/language/return
[rs-rectypes]: https://doc.rust-lang.org/reference/types.html#recursive-types
[rs-e0072]: https://doc.rust-lang.org/error_codes/E0072.html
[c-struct]: https://en.cppreference.com/c/language/struct

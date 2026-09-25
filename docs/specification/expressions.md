# 5. Expressions

An expression computes, constructs, or refers to a value. Expression nesting
forms the value-producing part of the Vortex abstract syntax tree.

## 5.1 Expression forms

<a class="vx-rule" id="expr.forms.included" href="#expr.forms.included">expr.forms.included</a> Vortex v0.1 includes:

- scalar literals and identifiers;
- array and struct construction;
- grouping;
- prefix unary operations;
- infix binary operations;
- bounded ranges, which may appear only as the iterable of a `for` statement;
- calls and casts;
- indexing and field access.

<a class="vx-rule" id="expr.forms.no-assignment" href="#expr.forms.no-assignment">expr.forms.no-assignment</a> Assignment and control flow are not expressions.

## 5.2 Precedence and associativity

From lowest to highest precedence:

| Level | Form | Operators | Associativity |
| ---: | --- | --- | --- |
| 1 | Range | `..`, `..=` | Non-chaining |
| 2 | Logical OR | `||` | Left |
| 3 | Logical AND | `&&` | Left |
| 4 | Bitwise OR | `|` | Left |
| 5 | Bitwise XOR | `^` | Left |
| 6 | Bitwise AND | `&` | Left |
| 7 | Equality | `==`, `!=` | None (does not chain) |
| 8 | Comparison | `<`, `<=`, `>`, `>=` | None (does not chain) |
| 9 | Shift | `<<`, `>>` | Left |
| 10 | Additive | `+`, `-` | Left |
| 11 | Multiplicative | `*`, `/`, `%` | Left |
| 12 | Unary | `+`, `-`, `!`, `~`, `&`, `&mut` | Right |
| 13 | Postfix | call, index, field | Left and chainable |
| 14 | Primary | literal, name, cast, array, struct, grouping | Not applicable |

Thus `2 + 3 * 4` groups as `2 + (3 * 4)`, and `- -value` groups from the
right.

<a class="vx-rule" id="expr.precedence.no-chaining" href="#expr.precedence.no-chaining">expr.precedence.no-chaining</a> Equality and comparison operators do not associate, and neither may be an
unparenthesized operand of the other: `a < b < c`, `a == b == c` and
`a < b == c` are syntax errors; write `(a < b) == c` when that is meant.
Bitwise `&`, `^` and `|` sit below the comparisons, as in C, so `a & b == c`
means `a & (b == c)`, a type error; write `(a & b) == c`.
([Why](../decisions/operators.md#d37))

<a class="vx-rule" id="expr.precedence.range-level" href="#expr.precedence.range-level">expr.precedence.range-level</a> Level 1 exists for the `for` statement: a range parses wherever an expression
may appear, but it is valid only as a `for` iterable (see [5.8](#58-ranges)).

## 5.3 Primary expressions

### Literals and names

<a class="vx-rule" id="expr.literals.context-typed" href="#expr.literals.context-typed">expr.literals.context-typed</a> A scalar literal produces its decoded value. An integer or floating literal
takes its type from its context, as
[literal typing](types-and-values.md#literal-typing) describes
([decision record](../decisions/numbers.md#d31)).

<a class="vx-rule" id="expr.names.value-lookup" href="#expr.names.value-lookup">expr.names.value-lookup</a> An identifier expression reads the value of the declaration selected by name
resolution. That declaration must be a variable: a local, a parameter or a
loop variable. When that declaration has type `&T` or `&mut T`, the
expression reads the referent (the storage the reference refers to) and has
type `T` ([References and mutability](references.md) 9.4). A name that
resolves to a function or a struct, used as a value, is a type error;
`let f = add;` is rejected because v0.1 has no function values
([decision 7](../decisions/operators.md#d7)).

### Grouping

<a class="vx-rule" id="expr.grouping.overrides-precedence" href="#expr.grouping.overrides-precedence">expr.grouping.overrides-precedence</a> Parentheses contain one expression and override ordinary precedence:

```vortex
// fragment
(left + right) * scale
```

<a class="vx-rule" id="expr.grouping.empty-invalid" href="#expr.grouping.empty-invalid">expr.grouping.empty-invalid</a> Empty parentheses do not form a value in v0.1.

### Array construction

<a class="vx-rule" id="expr.arrays.element-list" href="#expr.arrays.element-list">expr.arrays.element-list</a> An element-list array contains one or more compatible expressions:

```vortex
// fragment
[1, 2, 3]
[first(), second(), third()]
```

<a class="vx-rule" id="expr.arrays.repeat-form" href="#expr.arrays.repeat-form">expr.arrays.repeat-form</a> A repeat array contains one value followed by one or more dimensions, each an
integer constant expression ([5.12](#512-constant-expressions)):

```vortex
// fragment
[0.0; 4]
[0.0; 2, 3]
```

See [Arrays and shapes](arrays.md).

### Struct construction

<a class="vx-rule" id="expr.structs.field-initializers" href="#expr.structs.field-initializers">expr.structs.field-initializers</a> A struct expression names a struct and supplies one initializer for every
field, in any order:

```vortex
// fragment
Point { x: 0.0, y: 0.0 }
```

See [Structs](structs.md).

### Casts

<a class="vx-rule" id="expr.casts.syntax" href="#expr.casts.syntax">expr.casts.syntax</a> A cast is a numeric type keyword followed by one parenthesized operand, such
as `f32(count)`. It converts the operand's value to that type. See
[Casts and conversions](types-and-values.md#412-casts-and-conversions) for the
grammar and the allowed conversions, and the
[decision record](../decisions/numbers.md#d1).

## 5.4 Unary expressions

| Operator | Required category |
| --- | --- |
| `+` | Numeric operand |
| `-` | Signed integer or floating-point operand |
| `!` | `bool` operand |
| `~` | Integer operand |
| `&` | A place |
| `&mut` | A mutable place |

<a class="vx-rule" id="expr.unary.places-no-deref" href="#expr.unary.places-no-deref">expr.unary.places-no-deref</a> Unary expressions group right to left. The parser preserves the exact
operator. Type and reference checking validate the operand. A place is a name
followed by zero or more index and field suffixes, such as `grid[i, j]` or
`points[i].x` ([References and mutability](references.md) 9.3). Vortex v0.1
has no dereference operator: a program reads and writes through a reference
by using the reference's name ([References and mutability](references.md)
9.4; [record 40](../decisions/references.md#d40)).

<a class="vx-rule" id="expr.unary.reference-position" href="#expr.unary.reference-position">expr.unary.reference-position</a> A reference expression may appear only as a complete call argument or as the
complete initializer of a `let` without `mut`, optionally inside parentheses;
anywhere else it is a type error ([References and mutability](references.md)
9.7; [record 41](../decisions/references.md#d41)).

## 5.5 Arithmetic and bitwise expressions

<a class="vx-rule" id="expr.arithmetic.operand-types" href="#expr.arithmetic.operand-types">expr.arithmetic.operand-types</a> The arithmetic operators are `+`, `-`, `*`, `/`, and `%`. Remainder requires
integer operands. Both operands of an arithmetic operator must have the same
type after [literal typing](types-and-values.md#literal-typing), and the
result has that type. There is no implicit conversion
([decision record](../decisions/numbers.md#d30)).

<a class="vx-rule" id="expr.arithmetic.div-mod-semantics" href="#expr.arithmetic.div-mod-semantics">expr.arithmetic.div-mod-semantics</a> Integer `/` truncates toward zero: `7 / 2` is 3 and `-7 / 2` is -3. The result
of `%` has the sign of the dividend (the left operand): `-7 % 2` is -1 and
`7 % -2` is 1, so `(a / b) * b + a % b` equals `a` whenever `a / b` is
defined. In `i32`, `-2147483648 % -1` is 0, although the matching quotient
overflows. [Checked integer operations](#checked-integer-operations) lists
when `/` and `%` fail. ([Why](../decisions/operators.md#d33))

<a class="vx-rule" id="expr.arithmetic.float-ieee754" href="#expr.arithmetic.float-ieee754">expr.arithmetic.float-ieee754</a> Floating-point `/` by zero is not an error: it gives an infinity or NaN (not a
number), as [Types and values 4.4](types-and-values.md#44-floating-point-values)
describes ([decision record](../decisions/numbers.md#d24)). Each
floating-point `+`, `-`, `*` and `/` is one IEEE 754 operation rounded to
nearest with ties to even; an implementation must not fuse, reorder or widen
these operations ([decision record](../decisions/numbers.md#d56)).

<a class="vx-rule" id="expr.arithmetic.bitwise-operators" href="#expr.arithmetic.bitwise-operators">expr.arithmetic.bitwise-operators</a> The integer bitwise operators are `&`, `|`, `^`, `~`, `<<`, and `>>`. Prefix
`&` is a reference operator, while infix `&` is bitwise AND; their grammatical
positions distinguish them.

<a class="vx-rule" id="expr.arithmetic.shift-semantics" href="#expr.arithmetic.shift-semantics">expr.arithmetic.shift-semantics</a> Both operands of `<<` and `>>` must be integers, but they need not have the
same type. The result has the type of the left operand. The count (the right
operand) may have any integer type, and an integer literal count has type
`i32`. The count must be at least 0 and less than the bit width of the left
operand's type: 32 for `i32` and `u32`, and the width of `usize` (64 bits on
every v0.1 target). Any other count makes the shift fail
([checked integer operations](#checked-integer-operations)). `<<` shifts in
zero bits and discards the bits shifted out; losing bits this way is not
overflow, so `1 << 31` in `i32` is -2147483648. `>>` is an arithmetic shift
for signed types (it copies the sign bit, so `-7 >> 1` is -4) and a logical
shift for unsigned types (it shifts in zero bits).
([Why](../decisions/operators.md#d22))

### Checked integer operations

<a class="vx-rule" id="expr.checked.applies-to-integers" href="#expr.checked.applies-to-integers">expr.checked.applies-to-integers</a> The operations in this table are **checked** for every integer type (`i32`,
`u32` and `usize`). An operation fails when its condition holds:

| Operation | Fails when |
| --- | --- |
| `a + b`, `a - b`, `a * b` | the exact result is outside the range of the operands' type |
| `-a` | the exact result is outside the range, which happens only when `a` is the smallest `i32`, -2147483648 |
| `a / b` | `b` is zero, or the exact result is outside the range, which happens only for `-2147483648 / -1` |
| `a % b` | `b` is zero |
| `a += b`, `a -= b`, `a *= b`, `a /= b`, `a %= b` | the operation `a + b`, `a - b`, `a * b`, `a / b` or `a % b` fails |
| `T(v)`, where `T` is an integer type | `v`, truncated toward zero if it is a floating-point value, cannot be represented in `T`, or `v` is NaN or an infinity ([Types and values 4.12](types-and-values.md#412-casts-and-conversions)) |
| `a << n`, `a >> n` | `n` is negative, or not less than the bit width of `a`'s type |

<a class="vx-rule" id="expr.checked.failure-kind" href="#expr.checked.failure-kind">expr.checked.failure-kind</a> A failure is a runtime error ([Diagnostics 10.2](diagnostics.md#runtime-error)).
When every operand that decides the check is an integer constant expression,
the failure is a constant-evaluation error instead (see
[5.12](#512-constant-expressions)). Array indexing is also checked
([Arrays and shapes 7.6](arrays.md#76-indexing)).

<a class="vx-rule" id="expr.checked.scope-limits" href="#expr.checked.scope-limits">expr.checked.scope-limits</a> The variable of a `for` loop never overflows
([Statements 6.8](statements.md#68-for-loops)). Floating-point arithmetic and
casts to a floating-point type are never checked
([Types and values 4.4](types-and-values.md#44-floating-point-values)). No
other integer operation is checked. Decided in
[decision 34](../decisions/diagnostics.md#d34).

## 5.6 Comparison and equality

<a class="vx-rule" id="expr.comparison.operand-types" href="#expr.comparison.operand-types">expr.comparison.operand-types</a> `==` and `!=` require two operands of the same type, which must be `bool`,
`char`, an integer type or a floating-point type. `<`, `<=`, `>`, and `>=`
require two operands of the same integer or floating-point type.
Floating-point comparisons follow IEEE 754: NaN is unequal to every value,
including itself; every ordering comparison with NaN is false; and
`0.0 == -0.0` is true.

<a class="vx-rule" id="expr.comparison.excluded-types" href="#expr.comparison.excluded-types">expr.comparison.excluded-types</a> `String`, array and struct values have no equality or ordering in v0.1, and
`bool` and `char` have no ordering; such a comparison is a type error. An
operand of reference type reads the value it refers to
([References](references.md)), so an address is never compared.

<a class="vx-rule" id="expr.comparison.result-type" href="#expr.comparison.result-type">expr.comparison.result-type</a> All comparison and equality expressions produce `bool`.
([Why](../decisions/operators.md#d35))

Comparisons do not chain; see [5.2](#52-precedence-and-associativity).

## 5.7 Logical expressions

<a class="vx-rule" id="expr.logical.short-circuit" href="#expr.logical.short-circuit">expr.logical.short-circuit</a> `!`, `&&`, and `||` require boolean operands. `&&` and `||` evaluate the left
operand first and evaluate the right operand only when required to determine
the result.

```vortex
// statements: valid
let values = [0.5, 1.5, 2.5, 3.5];
let index = 4;
let safe = index < 4 && values[index] > 0.0; // false: values[4] is never read
```

<a class="vx-rule" id="expr.logical.optimization-preserved" href="#expr.logical.optimization-preserved">expr.logical.optimization-preserved</a> The required short-circuit behavior is observable and must be preserved by
lowering and optimization.

## 5.8 Ranges

<a class="vx-rule" id="expr.ranges.endpoints-operators" href="#expr.ranges.endpoints-operators">expr.ranges.endpoints-operators</a> A range has two endpoints and one of two operators:

```vortex
// fragment
start..end   // excludes end
start..=end  // includes end
```

<a class="vx-rule" id="expr.ranges.position-restriction" href="#expr.ranges.position-restriction">expr.ranges.position-restriction</a> Open-ended and chained ranges are syntax errors in v0.1. A range expression
may appear only as the iterable of a `for` statement, possibly enclosed in
parentheses. A range anywhere else, such as `let span = 0..10;` or
`values[0..2]`, is a type error: a range is not a value in v0.1 and has no
type. [Statements 6.8](statements.md#68-for-loops) gives the endpoint rules
and the loop's behavior. See [decision 36](../decisions/statements.md#d36).

## 5.9 Postfix expressions

<a class="vx-rule" id="expr.postfix.chaining" href="#expr.postfix.chaining">expr.postfix.chaining</a> Postfix suffixes can be chained in source order:

```vortex
// fragment
factory().items[row, column].value
```

### Calls

<a class="vx-rule" id="expr.calls.syntax" href="#expr.calls.syntax">expr.calls.syntax</a> A call contains a callee expression followed by zero or more ordered argument
expressions. A trailing argument comma is not accepted.

<a class="vx-rule" id="expr.calls.callee-must-resolve" href="#expr.calls.callee-must-resolve">expr.calls.callee-must-resolve</a> Name and type checking determine whether the callee is callable and whether
the argument count and types are valid. A cast is not a call
([5.3](#casts)). The callee must be a name that resolves to a function. Any
other callee, such as a struct name, a local variable, a parameter or a
parenthesized expression, is a type error, so `Point(0.0, 0.0)` does not
construct a `Point`. ([Why](../decisions/operators.md#d7))

<a class="vx-rule" id="expr.calls.print-arity" href="#expr.calls.print-arity">expr.calls.print-arity</a> The built-in `print` takes one or more arguments; its rules are in
[Programs and declarations 3.9](declarations.md#39-built-in-functions).

<a class="vx-rule" id="expr.calls.void-result" href="#expr.calls.void-result">expr.calls.void-result</a> A call to a `void` function produces no value, so it may appear only as the
whole expression of an expression statement
([Statements](statements.md#65-expression-statements)); as an operand, an
argument, an initializer or a returned value it is a type error
([why](../decisions/operators.md#d44)).

### Indexing

<a class="vx-rule" id="expr.indexing.syntax" href="#expr.indexing.syntax">expr.indexing.syntax</a> An index suffix contains one or more comma-separated index expressions. A
trailing comma is not accepted. The AST preserves every index in source order.
Each index must have an integer type (`i32`, `u32` or `usize`);
[Arrays and shapes, 7.6](arrays.md#76-indexing) defines the bounds rule and
when it is checked ([decision 12](../decisions/arrays.md#d12)).

<a class="vx-rule" id="expr.indexing.rank-match" href="#expr.indexing.rank-match">expr.indexing.rank-match</a> The number of indices must equal the rank of the indexed array; any other
count is a type error ([decision 47](../decisions/arrays.md#d47)). When the
indexed operand is a name of reference type, the index applies to the
referent.

### Field access

<a class="vx-rule" id="expr.fields.access" href="#expr.fields.access">expr.fields.access</a> Field access uses `.` followed by an identifier. Type checking verifies that
the selected field exists on the resolved object type. When the object is a
name of reference type, the field is selected from the referent
([record 40](../decisions/references.md#d40)).

## 5.10 Evaluation order

<a class="vx-rule" id="expr.order.left-to-right" href="#expr.order.left-to-right">expr.order.left-to-right</a> Vortex v0.1 evaluates expressions strictly from left to right:

- <a class="vx-rule" id="expr.order.binary-operands" href="#expr.order.binary-operands">expr.order.binary-operands</a> the left operand of a binary operator before the right operand;
- <a class="vx-rule" id="expr.order.call-array-order" href="#expr.order.call-array-order">expr.order.call-array-order</a> call arguments and array elements in source order;
- <a class="vx-rule" id="expr.order.struct-field-order" href="#expr.order.struct-field-order">expr.order.struct-field-order</a> struct field initializer values in the order written in the struct
  expression, not the order of the struct declaration;
- <a class="vx-rule" id="expr.order.index-order" href="#expr.order.index-order">expr.order.index-order</a> the base of an index expression, then its indices from left to right.

<a class="vx-rule" id="expr.order.check-timing" href="#expr.order.check-timing">expr.order.check-timing</a> An operation runs after all of its operands have been evaluated, and its
runtime check (array bounds, integer overflow, division or remainder by zero,
shift count, or cast range) happens when the operation runs. An index
expression checks its indices from left to right. `&&` and `||` evaluate the
right operand only when needed ([5.7](#57-logical-expressions)).
[Statements 6.4](statements.md#64-assignment) gives the order for assignment
statements. See [decision 38](../decisions/statements.md#d38).

<a class="vx-rule" id="expr.order.optimization-limit" href="#expr.order.optimization-limit">expr.order.optimization-limit</a> Optimization may remove an evaluation only when doing so preserves all
specified observable behavior and required diagnostics.

## 5.11 Invalid forms

<a class="vx-rule" id="expr.invalid.excluded-forms" href="#expr.invalid.excluded-forms">expr.invalid.excluded-forms</a> Vortex v0.1 has no assignment expressions, ternary expressions, value-producing
`if`, `match`, lambdas, optional chaining, null coalescing, increment,
decrement, open-ended ranges, or chained ranges.

The [formal grammar](grammar.md) gives the complete productions.

## 5.12 Constant expressions

<a class="vx-rule" id="expr.constants.integer-definition" href="#expr.constants.integer-definition">expr.constants.integer-definition</a> An **integer constant expression** is one of:

- an integer literal, optionally preceded by unary `-`;
- an integer constant expression in parentheses;
- two integer constant expressions joined by one of the binary operators `+`,
  `-`, `*`, `/` or `%`.

<a class="vx-rule" id="expr.constants.exclusions-and-typing" href="#expr.constants.exclusions-and-typing">expr.constants.exclusions-and-typing</a> No other expression is a constant expression in v0.1. A name is never a
constant expression, even when it names an immutable variable initialized with
a literal, and a call is never one. The literals of a constant expression are
typed by the rules of [Types and values](types-and-values.md); in an array
dimension they have type `usize`. A constant expression is evaluated with the
checked arithmetic of its type.

<a class="vx-rule" id="expr.constants.required-contexts" href="#expr.constants.required-contexts">expr.constants.required-contexts</a> Array dimensions must be integer constant expressions
([Arrays and shapes, 7.2](arrays.md#72-dimension-rules)), a constant index is
checked during compilation ([7.6](arrays.md#76-indexing)), and
[Diagnostics](diagnostics.md) lists the other checked operations whose
constant operands are evaluated during compilation. See
[decision 11](../decisions/arrays.md#d11).

<a class="vx-rule" id="expr.constants.deciding-operands" href="#expr.constants.deciding-operands">expr.constants.deciding-operands</a> The **deciding operands** of a checked operation
([5.5](#checked-integer-operations), and indexing in
[Arrays and shapes 7.6](arrays.md#76-indexing)) are: the divisor, for division
or remainder by zero; the count, for a shift; each index, for its own
dimension's bounds; the operand, for a cast; and every operand, for overflow.

<a class="vx-rule" id="expr.constants.compile-time-check" href="#expr.constants.compile-time-check">expr.constants.compile-time-check</a> When every deciding operand of a checked operation is an integer constant
expression, the implementation must perform the check during compilation, with
the same rules as at run time and the types the operands have after literal
typing, whether or not the operation would ever execute. A failed check is a
constant-evaluation error
([Diagnostics 10.2](diagnostics.md#constant-evaluation-error)).

<a class="vx-rule" id="expr.constants.runtime-check" href="#expr.constants.runtime-check">expr.constants.runtime-check</a> Every other check is performed at run time, and its failure is a runtime
error. An implementation must not reject a program because it can prove that a
run-time check will fail; it may warn
([Diagnostics 10.1](diagnostics.md#101-required-diagnostic-data)).

```vortex
// statements: constant-evaluation error
let values = [10, 20, 30];
let last = values[3]; // index 3 is outside extent 3
```

```vortex
// statements: runtime error
let values = [10, 20, 30];
let index: usize = 3;
let last = values[index]; // a name is never constant: checked at run time
```

<a class="vx-rule" id="expr.constants.float-excluded" href="#expr.constants.float-excluded">expr.constants.float-excluded</a> A float literal is not an integer constant expression, so `i32(3.0e10)` is
checked at run time, while `u32(-1)` is a constant-evaluation error. Decided
in [decision 39](../decisions/diagnostics.md#d39).

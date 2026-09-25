# 10. Diagnostics

Every ill-formed Vortex program must be rejected. Diagnostics explain which
rule failed and identify the relevant source span. This chapter defines the
minimum diagnostic taxonomy; exact prose and internal C++ exception types are
implementation details.

## 10.1 Required diagnostic data

Every error diagnostic should contain:

- <a class="vx-rule" id="diag.data.category" href="#diag.data.category">diag.data.category</a> a category;
- <a class="vx-rule" id="diag.data.message" href="#diag.data.message">diag.data.message</a> a concise primary message;
- <a class="vx-rule" id="diag.data.span" href="#diag.data.span">diag.data.span</a> a primary source span, shown to the reader as a line and column counted as
  [Conformance 1.7](conformance.md#17-source-locations) defines;
- <a class="vx-rule" id="diag.data.construct" href="#diag.data.construct">diag.data.construct</a> the unexpected or invalid construct;
- <a class="vx-rule" id="diag.data.expected-form" href="#diag.data.expected-form">diag.data.expected-form</a> the expected form or violated rule when that information is useful;
- <a class="vx-rule" id="diag.data.notes" href="#diag.data.notes">diag.data.notes</a> optional notes pointing to related declarations or source spans.

<a class="vx-rule" id="diag.data.no-internals" href="#diag.data.no-internals">diag.data.no-internals</a> Diagnostics must not rely only on an internal enum name or raw token number.

<a class="vx-rule" id="diag.warnings.definition" href="#diag.warnings.definition">diag.warnings.definition</a> An implementation may also issue **warnings**: diagnostics that point out a
likely mistake without rejecting the program. No warning is required. A
warning must not change whether a program is accepted, the compiler's exit
status, or the executable it produces. Warnings are not one of the categories
in [10.2](#102-categories), and conformance tests do not check for them. Which
warnings exist, and their wording, are implementation-defined
([Conformance 1.6](conformance.md#16-implementation-defined-behavior)).

Decision records: [Source positions](../decisions/diagnostics.md#d16) and
[Warnings](../decisions/diagnostics.md#d48).

## 10.2 Categories

### Lexical error

<a class="vx-rule" id="diag.categories.lexical" href="#diag.categories.lexical">diag.categories.lexical</a> The source text cannot form a valid v0.1 token.
[Lexical structure 2.7](lexical-structure.md#27-lexical-errors) is the complete
list of lexical errors ([decision 19](../decisions/lexical.md#d19)).

### Syntax error

<a class="vx-rule" id="diag.categories.syntax" href="#diag.categories.syntax">diag.categories.syntax</a> The token sequence does not match the grammar. Examples include a missing
semicolon, unmatched delimiter, missing type after `:`, or unexpected token at
top level.

### Name error

<a class="vx-rule" id="diag.categories.name" href="#diag.categories.name">diag.categories.name</a> A name is unknown, duplicated in the same scope, used outside its scope, or
declared where another declaration of the same name is visible. Vortex has no
shadowing, and all top-level names share one namespace
([Scopes](declarations.md#36-scopes); decision records
[2](../decisions/names.md#d2) and [5](../decisions/names.md#d5)). A named type
that does not resolve also produces a name error.

### Type error

<a class="vx-rule" id="diag.categories.type" href="#diag.categories.type">diag.categories.type</a> An operation receives unsupported or incompatible operand types, an
initializer does not match its annotation, a call has invalid arguments, an
assignment value is incompatible, a reference type or reference expression
appears where v0.1 does not allow it
([record 41](../decisions/references.md#d41)), or a `return` statement does
not match the function's return type: a missing value, a value in a `void`
function, or a value of the wrong type
([decision 8](../decisions/statements.md#d8)).

<a class="vx-rule" id="diag.categories.type-misc" href="#diag.categories.type-misc">diag.categories.type-misc</a> A numeric literal whose value does not fit its type, or a floating literal that
rounds to an infinity, is also a type error, reported at the literal
([decision record](../decisions/numbers.md#d32)). A struct expression with a
missing, unknown or repeated field is a type error, and so is a name used as
the wrong kind of declaration: a struct or variable called like a function
(`Point(0.0, 0.0)`), a function or struct used as a value (`let f = add;`), or
a function or variable used as a type or as the name of a struct expression
([why](../decisions/operators.md#d7)). A range expression used anywhere except
as the iterable of a `for` statement, and a `for` iterable that is not a
range, are type errors ([decision 36](../decisions/statements.md#d36)).

### Semantic error

<a class="vx-rule" id="diag.categories.semantic" href="#diag.categories.semantic">diag.categories.semantic</a> The syntax and local types are valid, but a contextual rule fails. Examples
include assigning to an immutable place (including a parameter), taking `&mut`
of an immutable place, using a variable in a way that conflicts with a live
borrow ([References and mutability](references.md) 9.8), using `break` outside
a loop, a non-`void` function whose body does not end in a terminating
statement ([Statements 6.10](statements.md#610-return)), a program with no
`main`, or a `main` with parameters or a non-`void` return type. A second
function named `main` is a duplicate declaration, which is a name error
([Programs and declarations 3.3](declarations.md#33-entry-point)). Decision
records: [6](../decisions/program.md#d6), [8](../decisions/statements.md#d8),
[23](../decisions/references.md#d23) and [41](../decisions/references.md#d41).

### Constant-evaluation error

<a class="vx-rule" id="diag.categories.constant-eval" href="#diag.categories.constant-eval">diag.categories.constant-eval</a> An expression required at compile time cannot be evaluated to a valid value.
An array dimension that is not an integer constant expression (for example,
one that uses a name, a call, or a floating-point literal), whose evaluation
leaves the `usize` range or divides by zero, or whose value is zero is in this
category, and so is an array whose total size exceeds the implementation's
limit ([Arrays and shapes, 7.2](arrays.md#72-dimension-rules);
[decision 11](../decisions/arrays.md#d11)). A constant index outside its
extent is also in this category
([Arrays and shapes, 7.6](arrays.md#76-indexing);
[decision 12](../decisions/arrays.md#d12)). A checked operation whose deciding
operands are all integer constant expressions, and whose check fails, is also
in this category: for example `10 / 0`, `u32(-1)`, or `values[3]` when
`values` has three elements
([Expressions 5.12](expressions.md#512-constant-expressions)).

### Runtime error

<a class="vx-rule" id="diag.categories.runtime" href="#diag.categories.runtime">diag.categories.runtime</a> A valid program reaches a checked operation whose dynamic values violate a
runtime rule. The required v0.1 checks are:

- <a class="vx-rule" id="diag.runtime.bounds" href="#diag.runtime.bounds">diag.runtime.bounds</a> an array index out of bounds ([Arrays and shapes 7.6](arrays.md#76-indexing));
- <a class="vx-rule" id="diag.runtime.div-zero" href="#diag.runtime.div-zero">diag.runtime.div-zero</a> integer division or remainder by zero;
- <a class="vx-rule" id="diag.runtime.overflow" href="#diag.runtime.overflow">diag.runtime.overflow</a> integer overflow in `+`, `-`, `*`, unary `-`, `/` and the compound
  assignments, including the smallest value of a signed type divided by -1
  (the remainder of that division is 0 and never fails);
- <a class="vx-rule" id="diag.runtime.shift" href="#diag.runtime.shift">diag.runtime.shift</a> a shift count that is negative or not less than the bit width of the shifted
  value's type;
- <a class="vx-rule" id="diag.runtime.cast" href="#diag.runtime.cast">diag.runtime.cast</a> a cast to an integer type whose operand the target type cannot represent: an
  integer outside the target range, or a floating-point NaN, infinity, or
  value whose truncation is outside the target range
  ([Types and values 4.12](types-and-values.md#412-casts-and-conversions),
  [decision record](../decisions/numbers.md#d27)).

<a class="vx-rule" id="diag.runtime.float-unchecked" href="#diag.runtime.float-unchecked">diag.runtime.float-unchecked</a> [Expressions 5.5](expressions.md#checked-integer-operations) gives the exact
condition for each operation. Floating-point arithmetic is not checked:
division by zero and overflow give infinities or NaN as
[Types and values 4.4](types-and-values.md#44-floating-point-values) describes
([decision record](../decisions/numbers.md#d24)).

<a class="vx-rule" id="diag.runtime.must-report-at-runtime" href="#diag.runtime.must-report-at-runtime">diag.runtime.must-report-at-runtime</a> A check whose deciding operands are all integer constant expressions is made
during compilation instead, as a constant-evaluation error. Every other failure
must be reported at run time: an implementation must not reject a program
because it can prove that a run-time check will fail, though it may warn.
Decision records: [Checked integer operations](../decisions/diagnostics.md#d34)
and [Provable runtime failures](../decisions/diagnostics.md#d39).

### Implementation-limit error

<a class="vx-rule" id="diag.categories.impl-limit" href="#diag.categories.impl-limit">diag.categories.impl-limit</a> The program uses specified v0.1 behavior that the current compiler has not yet
implemented, or it exceeds a limit that the implementation documents, such as
its stack size. The first kind is reported during compilation and is
appropriate while a compiler is being developed. Exhausting the stack is
reported while the program runs ([10.6](#106-runtime-reporting)). This category
must not be used to disguise a crash or silently ignore source.

## 10.3 Error phase versus category

<a class="vx-rule" id="diag.phase.independent-of-detection" href="#diag.phase.independent-of-detection">diag.phase.independent-of-detection</a> Category and detection phase are related but not identical. One rule can fail
in two categories. An index that is an integer constant expression is checked
during compilation, and an out-of-bounds value is a constant-evaluation error;
any other index is checked at run time, and an out-of-bounds value is a runtime
error. Both violate the same bounds rule. The category depends only on whether
the deciding operands are constant expressions, never on how much a compiler
can prove ([decision 39](../decisions/diagnostics.md#d39)).

<a class="vx-rule" id="diag.phase.array-dimension" href="#diag.phase.array-dimension">diag.phase.array-dimension</a> Likewise, an invalid array dimension is found while the type checker resolves
an array type, yet it is a constant-evaluation error
([decision 52](../decisions/arrays.md#d52)).

<a class="vx-rule" id="diag.phase.describe-failure-not-pass" href="#diag.phase.describe-failure-not-pass">diag.phase.describe-failure-not-pass</a> Diagnostics should describe the language failure rather than forcing users to
understand the compiler pass that happened to detect it.

## 10.4 Examples

Each example's first line is its label (see
[Specification examples](conformance.md#18-specification-examples)). A test
places a `statements` example inside `fn main() { ... }`; written alone at the
top of a file, a statement would be a syntax error. It appends `fn main() {}`
to an `items` example, which holds top-level declarations only, and compiles a
`program` example as written. The reasons are in
[record 28](../decisions/documentation.md#d28).

```vortex
// statements: lexical error
let value = "unterminated;
```

Required result: lexical error at the unterminated string.

```vortex
// statements: syntax error
let value i32 = 10;
```

Required result: syntax error identifying the missing `:` or unexpected type
token.

```vortex
// statements: type error
let value: bool = 10;
```

Required result: type error relating the initializer to the written type.

```vortex
// statements: type error
let small: i32 = 2147483648; // type error: 2147483648 does not fit in i32
```

Required result: type error at the literal, whose value does not fit in `i32`.

```vortex
// program: type error
struct Point { x: f32, y: f32 }

fn main() {
    let p = Point { x: 1.0 };
}
```

Required result: type error at the struct expression, naming the missing field
`y`.

```vortex
// statements: semantic error
let value = 10;
value = 20;
```

Required result: semantic error pointing to the immutable assignment target,
with an optional note at the declaration.

```vortex
// items: constant-evaluation error
fn make(size: usize) {
    let values: [f32; size] = [0.0; size];
}
```

Required result: constant-evaluation error explaining that a fixed dimension
depends on a runtime parameter.

```vortex
// statements: constant-evaluation error
let values = [10, 20, 30];
let last = values[3];
```

Required result: constant-evaluation error at the index `3`, which is outside
the extent 3. The same access through a variable holding 3 compiles and fails
at run time.

## 10.5 Recovery

<a class="vx-rule" id="diag.recovery.sync-points" href="#diag.recovery.sync-points">diag.recovery.sync-points</a> After a syntax error, a parser may synchronize at:

- <a class="vx-rule" id="diag.recovery.sync-statement" href="#diag.recovery.sync-statement">diag.recovery.sync-statement</a> `;` for a simple statement;
- <a class="vx-rule" id="diag.recovery.sync-block" href="#diag.recovery.sync-block">diag.recovery.sync-block</a> `}` for a block or declaration body;
- <a class="vx-rule" id="diag.recovery.sync-top-level" href="#diag.recovery.sync-top-level">diag.recovery.sync-top-level</a> the next top-level `fn` or `struct` token.

<a class="vx-rule" id="diag.recovery.no-cascades" href="#diag.recovery.no-cascades">diag.recovery.no-cascades</a> Recovery should report useful independent errors without producing cascades
from one missing token. A compiler may stop after a documented maximum number
of errors.

## 10.6 Runtime reporting

<a class="vx-rule" id="diag.runtime-report.exit-zero" href="#diag.runtime-report.exit-zero">diag.runtime-report.exit-zero</a> A compiled program whose `main` returns exits with status 0.

<a class="vx-rule" id="diag.runtime-report.sequence" href="#diag.runtime-report.sequence">diag.runtime-report.sequence</a> When a required runtime check fails, the program must, in this order:

1. <a class="vx-rule" id="diag.runtime-report.flush-stdout" href="#diag.runtime-report.flush-stdout">diag.runtime-report.flush-stdout</a> write to standard output everything that earlier `print` calls produced;
2. <a class="vx-rule" id="diag.runtime-report.stderr-format" href="#diag.runtime-report.stderr-format">diag.runtime-report.stderr-format</a> write exactly one line to standard error, in the form
   `runtime error[<kind>]: <message> at <file>:<line>:<column>`;
3. <a class="vx-rule" id="diag.runtime-report.exit-101" href="#diag.runtime-report.exit-101">diag.runtime-report.exit-101</a> exit with status 101.

<a class="vx-rule" id="diag.runtime-report.nothing-else" href="#diag.runtime-report.nothing-else">diag.runtime-report.nothing-else</a> No further statement runs and nothing else is written.

- <a class="vx-rule" id="diag.runtime-report.kind-field" href="#diag.runtime-report.kind-field">diag.runtime-report.kind-field</a> `<kind>` names the failed check, from the table below.
- <a class="vx-rule" id="diag.runtime-report.message-field" href="#diag.runtime-report.message-field">diag.runtime-report.message-field</a> `<message>` is implementation-defined text that describes the failure.
- <a class="vx-rule" id="diag.runtime-report.file-field" href="#diag.runtime-report.file-field">diag.runtime-report.file-field</a> `<file>` is the source path as it was given to the compiler.
- <a class="vx-rule" id="diag.runtime-report.position-field" href="#diag.runtime-report.position-field">diag.runtime-report.position-field</a> `<line>` and `<column>` give the start of the failing operation's source
  span, counted as for compile-time diagnostics. When no position is known, as
  for stack exhaustion, the text ` at <file>:<line>:<column>` is omitted.

| Kind | Failed check |
| --- | --- |
| `bounds` | An index outside its dimension's extent |
| `divide-by-zero` | Integer division or remainder by zero |
| `overflow` | A checked integer operation whose result does not fit its type |
| `shift` | A shift count outside 0 to the operand's bit width minus 1 |
| `cast` | A cast whose value the destination type cannot represent |
| `stack` | Stack exhaustion |

See the [decision record](../decisions/program.md#d14).

<a class="vx-rule" id="diag.runtime-report.stack-exhaustion" href="#diag.runtime-report.stack-exhaustion">diag.runtime-report.stack-exhaustion</a> If the program exhausts its call stack, execution stops at that point and is
reported in the same way as a failed runtime check, with the kind `stack`. Its
category is implementation-limit: the program broke no language rule; it
exceeded a documented limit
([Conformance 1.5](conformance.md#15-undefined-behavior)). Decision record:
[Stack exhaustion](../decisions/diagnostics.md#d46).

<a class="vx-rule" id="diag.runtime-report.no-exceptions" href="#diag.runtime-report.no-exceptions">diag.runtime-report.no-exceptions</a> Vortex v0.1 does not specify exception handling inside the language, so a Vortex
program cannot catch these runtime failures.

## 10.7 Verification requirements

<a class="vx-rule" id="diag.verification.requirements" href="#diag.verification.requirements">diag.verification.requirements</a> Each diagnostic rule should have:

- <a class="vx-rule" id="diag.verification.rejected-program" href="#diag.verification.rejected-program">diag.verification.rejected-program</a> one minimal rejected program;
- <a class="vx-rule" id="diag.verification.category" href="#diag.verification.category">diag.verification.category</a> the expected category;
- <a class="vx-rule" id="diag.verification.span-assertion" href="#diag.verification.span-assertion">diag.verification.span-assertion</a> an assertion that the primary source span covers the relevant construct;
- <a class="vx-rule" id="diag.verification.accepted-program" href="#diag.verification.accepted-program">diag.verification.accepted-program</a> a nearby accepted program that proves the test is not rejecting a broader
  valid form.

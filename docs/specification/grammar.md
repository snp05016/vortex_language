# Vortex v0.1 grammar reference

This is the formal source-language reference for Vortex v0.1. It combines the
EBNF grammar with implementation notes, valid and invalid examples, semantic
boundaries, and an implementation checklist.

Use it while building the lexer, parser, AST, semantic checker, tests, or error
messages. This page is part of the normative specification: its productions
define v0.1 syntax, and [Document authority](conformance.md#11-document-authority)
says which page decides when two disagree. The reasons are in
[record 49](../decisions/documentation.md#d49).

## Contents

- [How to read the grammar](#how-to-read-the-grammar)
- [Parsing versus semantic checking](#parsing-versus-semantic-checking)
- [Program structure](#program-structure)
- [Functions and parameters](#functions-and-parameters)
- [Struct definitions and fields](#struct-definitions-and-fields)
- [Blocks and statements](#blocks-and-statements)
- [Expressions and precedence](#expressions-and-precedence)
- [Array expressions](#array-expressions)
- [Struct expressions](#struct-expressions)
- [Types](#types)
- [Literals](#literals)
- [Identifiers and keywords](#identifiers-and-keywords)
- [Implementation checklist](#implementation-checklist)

## How to read the grammar

The grammar uses Extended Backus-Naur Form (EBNF):

| Notation | Meaning |
| --- | --- |
| `"fn"` | The exact source text `fn` |
| `rule_name` | Another grammar rule |
| `[ value ]` | `value` is optional |
| `{ value }` | `value` may occur zero or more times |
| `a \| b` | Either `a` or `b` |
| `a, b` | `a` followed by `b`; this comma is EBNF punctuation |
| `;` | Ends an EBNF rule; a source semicolon is written as `";"` |

Whitespace and `//` comments may appear between tokens unless they occur inside
a string or character literal. Newlines do not end statements. A semicolon is
required everywhere the grammar contains `";"`.

For each major section:

1. Read the EBNF to see what token shapes the parser accepts.
2. Read **Allowed** and **Not allowed** for the v0.1 boundary.
3. Read **Semantic checks** for rules handled after parsing.
4. Read **AST responsibility** for information the parser must preserve.

## Parsing versus semantic checking

The parser asks: **Does this source have a valid Vortex shape?** The semantic
checker asks: **Does that valid shape make sense?**

Both lines match the variable-declaration grammar:

```vortex
// statements: type error
let count: i32 = 10;
let flag: bool = 10;
```

The second is a type error, because an integer cannot initialize a `bool`.

Array dimensions follow the same separation. Dimensions are full expressions
in the grammar:

```vortex
// statements: valid
let values: [f32; 2 + 2] = [0.0; 2 + 2];
```

The parser stores `2 + 2` as an expression. Semantic analysis must check that
it is an integer constant expression
([Arrays and shapes, 7.2](arrays.md#72-dimension-rules)) and evaluate it. A
dimension that uses a name, such as a parameter, is never constant:

```vortex
// items: constant-evaluation error
fn make_values(size: usize) {
    let values: [f32; size] = [0.0; size]; // constant-evaluation error: size is a name
}
```

Unless a section says otherwise, these are semantic checks:

- whether a name is declared, visible, or duplicated;
- whether an expression has the required type;
- whether a literal fits its required type;
- whether an assignment target is mutable;
- whether a reference type or reference expression appears where v0.1 allows
  it, and whether a borrow conflicts with another use of the same variable;
- whether `return`, `break`, or `continue` is legal in context;
- whether an array dimension is an integer constant expression whose value is
  at least 1;
- whether a call has a valid callee, argument count, and argument types.

In this reference, "semantic checks" means every static check made after
parsing. A failed check reports one of the categories in
[Diagnostics](diagnostics.md#102-categories): a name, type, semantic or
constant-evaluation error.
[Specification examples](conformance.md#18-specification-examples) calls this
group "Static error". The reasons are in
[record 51](../decisions/documentation.md#d51).

## Program structure

<span id="rule-program"></span><span id="rule-top_level_declaration"></span>

=== "Grammar"

    ```ebnf
    program ::=
        { top_level_declaration } ;

    top_level_declaration ::=
          function
        | struct_definition ;
    ```

=== "Diagram"

    --8<-- "includes/railroad/program.md"

    --8<-- "includes/railroad/top_level_declaration.md"

### Allowed

- Any number of top-level functions and structs, in either order.
- An empty source file as a parseable program.

```vortex
// program: valid
struct Pair {
    left: i32,
    right: i32,
}

fn main() {
    let pair = Pair { left: 2, right: 3 };
    print(pair.left + pair.right);
}
```

### Not allowed

- Top-level executable statements or `let` declarations.
- Nested functions or structs.
- Modules, imports, classes, enums, traits, aliases, or generics.

```vortex
// program: syntax error
let count = 3;      // syntax error: local declarations belong in blocks
print("starting");  // syntax error: statement at top level

fn main() {}
```

### Semantic checks

Every top-level function and struct name is visible throughout the file, so a
declaration may use a function or struct that appears after it
([decision record](../decisions/names.md#d3)).

An executable must contain exactly one `main` with no parameters and a `void`
return type. The parser builds an AST for a file that violates this, and name
resolution then reports each mistake once: a semantic error for a missing
`main` or a wrong signature, and a name error for a second `main`
([Programs and declarations 3.3](declarations.md#33-entry-point),
[decision](../decisions/program.md#d6)).

### AST responsibility

The program root preserves top-level declarations in source order. Each
declaration preserves its source location.

## Functions and parameters

<span id="rule-function"></span><span id="rule-parameter_list"></span><span id="rule-parameter"></span>

=== "Grammar"

    ```ebnf
    function ::=
        "fn", identifier,
        "(", [ parameter_list ], ")",
        [ "->", type ],
        block ;

    parameter_list ::=
        parameter, { ",", parameter } ;

    parameter ::=
        identifier, ":", type ;
    ```

=== "Diagram"

    --8<-- "includes/railroad/function.md"

    --8<-- "includes/railroad/parameter_list.md"

    --8<-- "includes/railroad/parameter.md"

An omitted return annotation means `void`.

### Allowed

- Zero or more typed parameters.
- Any v0.1 type except `void` as a parameter type, including a reference type.
- Any v0.1 type except a reference type as an explicit return type.
- An omitted return annotation for a `void` function.
- An empty body in a `void` function.

```vortex
// items: valid
fn announce() {
    print("ready");
}

fn add(left: i32, right: i32) -> i32 {
    return left + right;
}

fn fill(values: &mut [f32; 2 + 2], value: f32) -> void {
    values[0] = value;
}
```

### Not allowed

- Untyped or defaulted parameters.
- A trailing comma in a parameter list.
- Function declarations without bodies.
- Methods, generic parameters, or variadic parameters.

```vortex
// items: syntax error
fn untyped(value) {}             // missing `: type`
fn defaulted(value: i32 = 1) {} // defaults are unsupported
fn trailing(value: i32,) {}     // trailing comma is unsupported
```

### Semantic checks

Parameter names must be unique and must not reuse the name of a top-level
function or struct or `print`. Parameters share one scope with the locals
declared directly in the function body. A parameter of type `void` is a type
error. The return type controls which `return` forms are valid. `main` must
have the required v0.1 signature. Decided in
[decision 2](../decisions/names.md#d2),
[decision 41](../decisions/references.md#d41) and
[decision 44](../decisions/operators.md#d44).

### AST responsibility

A function preserves its name, ordered parameter declarations, return type,
body, and locations. Each parameter preserves its name and type. Tooling that
must reproduce source exactly may also distinguish omitted `-> type` from
explicit `-> void`.

## Struct definitions and fields

<span id="rule-struct_definition"></span><span id="rule-struct_field"></span>

=== "Grammar"

    ```ebnf
    struct_definition ::=
        "struct", identifier, "{",
        struct_field, { ",", struct_field }, [ "," ],
        "}" ;

    struct_field ::=
        identifier, ":", type ;
    ```

=== "Diagram"

    --8<-- "includes/railroad/struct_definition.md"

    --8<-- "includes/railroad/struct_field.md"

### Allowed

- Typed, comma-separated fields.
- An optional trailing field comma.

```vortex
// items: valid
struct Point {
    x: f32,
    y: f32,
}
```

### Not allowed

- Field defaults, untyped fields, methods, visibility modifiers, inheritance,
  or generic fields.
- Semicolons in place of commas between fields.
- A struct with no fields ([why](../decisions/operators.md#d26)).

```vortex
// items: syntax error
struct Marker {}  // a struct needs at least one field

struct Point {
    x: f32 = 0.0, // field defaults are unsupported
    y,            // a field type is required
}
```

### Semantic checks

A struct name must differ from every other top-level function or struct name
and from `print`, because all top-level names share one namespace; a clash is
a name error ([decision record](../decisions/names.md#d5)). Field names must be
unique, and every field type must resolve. A struct must not contain itself by
value, directly or through other structs or array element types; such a cycle
is a type error ([why](../decisions/operators.md#d45)).

### AST responsibility

A struct preserves its name and ordered field declarations. Each field
preserves its name, type, and location.

## Blocks and statements

<span id="rule-block"></span><span id="rule-statement"></span>

=== "Grammar"

    ```ebnf
    block ::=
        "{", { statement }, "}" ;

    statement ::=
          variable_declaration
        | assignment_statement
        | return_statement
        | expression_statement
        | if_statement
        | while_statement
        | for_statement
        | break_statement
        | continue_statement
        | block ;
    ```

=== "Diagram"

    --8<-- "includes/railroad/block.md"

    --8<-- "includes/railroad/statement.md"

A block contains zero or more statements, is itself a statement, and creates a
nested local scope.

### Variable declarations

<span id="rule-variable_declaration"></span>

=== "Grammar"

    ```ebnf
    variable_declaration ::=
        "let", [ "mut" ], identifier,
        [ ":", type ],
        "=", expression, ";" ;
    ```

=== "Diagram"

    --8<-- "includes/railroad/variable_declaration.md"

**Allowed:** one immutable or mutable local, with an explicit or inferred type,
and always with an initializer.

```vortex
// statements: valid
let width = 128;
let mut total: f32 = 0.0;
```

**Not allowed:** a missing initializer, `mut` after the name, or multiple names.

```vortex
// statements: syntax error
let width;
let total mut = 0;
let left, right = 0, 1;
```

**Semantic checks:** infer an omitted type, check an explicit type against the
initializer, and record mutability. The name becomes visible only after the
whole declaration and must not reuse any visible name, because Vortex has no
shadowing ([Scopes](declarations.md#36-scopes),
[decision record](../decisions/names.md#d2)).

**AST responsibility:** preserve the name, `mut` flag, optional written type,
initializer, and location.

### Assignment statements

<span id="rule-assignment_statement"></span><span id="rule-assignment_target"></span><span id="rule-assignment_operator"></span>

=== "Grammar"

    ```ebnf
    assignment_statement ::=
        assignment_target, assignment_operator, expression, ";" ;

    assignment_target ::=
        identifier,
        {
            "[", expression, { ",", expression }, "]"
          | ".", identifier
        } ;

    assignment_operator ::=
          "="
        | "+="
        | "-="
        | "*="
        | "/="
        | "%=" ;
    ```

=== "Diagram"

    --8<-- "includes/railroad/assignment_statement.md"

    --8<-- "includes/railroad/assignment_target.md"

    --8<-- "includes/railroad/assignment_operator.md"

**Allowed:** names, indexed elements, fields, and chains of index and field
suffixes as targets.

```vortex
// fragment
count = 4;
total += value;
matrix[row, column] *= scale;
points[index].x = 0.0;
```

**Not allowed:** assignment as an expression, an arbitrary expression target,
or unsupported compound assignment operators.

```vortex
// items: syntax error
fn update(count: i32, left: i32, right: i32, flags: u32, mask: u32) {
    let result = count = 4;
    (left + right) = 4;
    flags &= mask;
}
```

**Semantic checks:** the target must exist, must be a mutable place as defined
in [References and mutability 9.3](references.md#93-local-mutability) (its root
is a `let mut` variable or a name of type `&mut T`; any other parameter is
immutable), and must accept the right-hand type. Assigning to an immutable
place is a semantic error ([record 23](../decisions/references.md#d23)).
Compound assignment also requires the corresponding binary operation to be
valid.

**AST responsibility:** preserve the operator, complete target, value, and
location.

### Return statements

<span id="rule-return_statement"></span>

=== "Grammar"

    ```ebnf
    return_statement ::=
        "return", [ expression ], ";" ;
    ```

=== "Diagram"

    --8<-- "includes/railroad/return_statement.md"

**Allowed:** `return;` and `return expression;`.

**Not allowed:** a missing semicolon or multiple returned expressions.

```vortex
// items: syntax error
fn pick(value: i32, left: i32, right: i32) -> i32 {
    return value             // missing `;`
    return left, right;      // multiple returns are unsupported
}
```

**Semantic checks:** `return` must occur in a function. `return;` is allowed
only in a `void` function, and `return expression;` only in a non-`void`
function, with an expression compatible with its return type; any other
`return` is a type error. `return f();` is a type error when `f` returns
`void`, even in a `void` function. The body of a non-`void` function must end
in a terminating statement, or the program has a semantic error
([Statements 6.10](statements.md#610-return),
[decision 8](../decisions/statements.md#d8)).

**AST responsibility:** preserve the optional expression.

### Expression statements

<span id="rule-expression_statement"></span>

=== "Grammar"

    ```ebnf
    expression_statement ::=
        expression, ";" ;
    ```

=== "Diagram"

    --8<-- "includes/railroad/expression_statement.md"

**Allowed:** any expression followed by `;`; calls are the usual useful form.

```vortex
// fragment
print("ready");
calculate();
```

**Not allowed:** omitting the semicolon. Assignment uses its own statement rule
because it is not an expression.

**Semantic checks:** an implementation may warn about a discarded value with
no effect, but the statement remains valid; warnings never change acceptance
([Diagnostics 10.1](diagnostics.md#101-required-diagnostic-data)). A call to a
`void` function may appear only as the whole expression of an expression
statement, optionally in parentheses. Decided in
[decision 44](../decisions/operators.md#d44) and
[decision 48](../decisions/diagnostics.md#d48).

**AST responsibility:** preserve the evaluated expression.

### Conditional statements

<span id="rule-if_statement"></span>

=== "Grammar"

    ```ebnf
    if_statement ::=
        "if", expression, block,
        [ "else", ( if_statement | block ) ] ;
    ```

=== "Diagram"

    --8<-- "includes/railroad/if_statement.md"

**Allowed:** `if`, `if`/`else`, and `else if`, all with braced bodies.

```vortex
// statements: valid
let score = 45;
if score >= 50 {
    print("passed");
} else if score >= 40 {
    print("close");
} else {
    print("try again");
}
```

**Not allowed:** value-producing `if`, an unbraced body, or a bare statement
after `else`.

```vortex
// statements: syntax error
let ready = true;
let label = if ready { "yes" } else { "no" };
if ready print("ready");
```

**Semantic checks:** each condition must be `bool`.

**AST responsibility:** preserve the condition, then block, and optional else
branch. An else branch is another `if` or a block.

### While loops

<span id="rule-while_statement"></span>

=== "Grammar"

    ```ebnf
    while_statement ::=
        "while", expression, block ;
    ```

=== "Diagram"

    --8<-- "includes/railroad/while_statement.md"

**Allowed:** one condition and a braced body.

```vortex
// statements: valid
let mut index = 0;
let count = 4;
while index < count {
    index += 1;
}
```

**Not allowed:** `do while`, a missing condition, or an unbraced body.

**Semantic checks:** the condition must be `bool`.

**AST responsibility:** preserve the condition and body.

### For loops

<span id="rule-for_statement"></span>

=== "Grammar"

    ```ebnf
    for_statement ::=
        "for", identifier, "in", expression, block ;
    ```

=== "Diagram"

    --8<-- "includes/railroad/for_statement.md"

**Allowed:** one loop variable, one iterable expression, and a braced body.
The grammar accepts any expression after `in`; a static rule requires a range
([Statements 6.8](statements.md#68-for-loops)).

```vortex
// statements: valid
let start = 1;
let finish = 3;

for index in 0..4 {
    print(index);
}

for index in start..=finish {
    print(index);
}
```

**Not allowed:** C-style loops, multiple bindings, or an unbraced body.

```vortex
// statements: syntax error
let values = [1, 2, 3];
for (let i = 0; i < 4; i += 1) {}
for index, value in values {}
```

An iterable that is not a range parses, then fails type checking:

```vortex
// statements: type error
let values = [1, 2, 3];
for value in values {}   // type error: the iterable must be a range in v0.1
```

**Semantic checks:** after any enclosing parentheses are removed, the iterable
must be a range whose two endpoints have the same integer type after literal
typing; otherwise it is a type error. The loop variable has that type, is
immutable, and is visible only in the body
([Statements 6.8](statements.md#68-for-loops),
[decision 13](../decisions/statements.md#d13)). The loop variable must not
reuse a visible name, and a local declared directly in the loop body must not
reuse the loop variable's name.

**AST responsibility:** preserve the variable name, iterable, and body.

### Break and continue

<span id="rule-break_statement"></span><span id="rule-continue_statement"></span>

=== "Grammar"

    ```ebnf
    break_statement ::=
        "break", ";" ;

    continue_statement ::=
        "continue", ";" ;
    ```

=== "Diagram"

    --8<-- "includes/railroad/break_statement.md"

    --8<-- "includes/railroad/continue_statement.md"

**Allowed:** exactly `break;` and `continue;`.

**Not allowed:** labels, values, conditions, or omitted semicolons.

```vortex
// statements: syntax error
while true {
    break outer;
    continue 2;
}
```

**Semantic checks:** both must occur inside a loop.

**AST responsibility:** preserve the statement kind and location.

### Standalone blocks

A block may appear anywhere a statement may appear:

```vortex
// statements: valid
{
    let temporary = 42;
    print(temporary);
}
```

Blocks do not produce values in v0.1, and no semicolon follows the closing
brace.

## Expressions and precedence

The grammar is ordered from lowest to highest precedence. Postfix operations
bind most tightly; ranges bind least tightly. A range expression parses
wherever an expression may appear, but it is valid only as the iterable of a
`for` statement ([decision 36](../decisions/statements.md#d36)).

| Precedence | Category | Operators/forms | Associativity |
| ---: | --- | --- | --- |
| 1 | Range | `..`, `..=` | Non-chaining |
| 2 | Logical OR | `||` | Left |
| 3 | Logical AND | `&&` | Left |
| 4 | Bitwise OR | `\|` | Left |
| 5 | Bitwise XOR | `^` | Left |
| 6 | Bitwise AND | `&` | Left |
| 7 | Equality | `==`, `!=` | None (does not chain) |
| 8 | Comparison | `<`, `<=`, `>`, `>=` | None (does not chain) |
| 9 | Shift | `<<`, `>>` | Left |
| 10 | Additive | `+`, `-` | Left |
| 11 | Multiplicative | `*`, `/`, `%` | Left |
| 12 | Unary | `+`, `-`, `!`, `~`, `&`, `&mut` | Right |
| 13 | Postfix | call, index, field | Left/chained |
| 14 | Primary | literal, name, cast, array, struct, grouping | N/A |

<span id="rule-expression"></span><span id="rule-range_expression"></span><span id="rule-range_operator"></span><span id="rule-logical_or_expression"></span><span id="rule-logical_and_expression"></span><span id="rule-bitwise_or_expression"></span><span id="rule-bitwise_xor_expression"></span><span id="rule-bitwise_and_expression"></span><span id="rule-equality_expression"></span><span id="rule-comparison_expression"></span><span id="rule-shift_expression"></span><span id="rule-additive_expression"></span><span id="rule-multiplicative_expression"></span><span id="rule-unary_expression"></span><span id="rule-postfix_expression"></span><span id="rule-call_suffix"></span><span id="rule-argument_list"></span><span id="rule-index_suffix"></span><span id="rule-field_suffix"></span><span id="rule-primary_expression"></span><span id="rule-cast_expression"></span><span id="rule-numeric_type"></span>

=== "Grammar"

    ```ebnf
    expression ::=
        range_expression ;

    range_expression ::=
        logical_or_expression,
        [ range_operator, logical_or_expression ] ;

    range_operator ::=
          ".."
        | "..=" ;

    logical_or_expression ::=
        logical_and_expression, { "||", logical_and_expression } ;

    logical_and_expression ::=
        bitwise_or_expression, { "&&", bitwise_or_expression } ;

    bitwise_or_expression ::=
        bitwise_xor_expression, { "|", bitwise_xor_expression } ;

    bitwise_xor_expression ::=
        bitwise_and_expression, { "^", bitwise_and_expression } ;

    bitwise_and_expression ::=
        equality_expression, { "&", equality_expression } ;

    equality_expression ::=
          comparison_expression
        | shift_expression, ( "==" | "!=" ), shift_expression ;

    comparison_expression ::=
        shift_expression,
        [ ( "<" | "<=" | ">" | ">=" ), shift_expression ] ;

    shift_expression ::=
        additive_expression, { ( "<<" | ">>" ), additive_expression } ;

    additive_expression ::=
        multiplicative_expression, { ( "+" | "-" ), multiplicative_expression } ;

    multiplicative_expression ::=
        unary_expression, { ( "*" | "/" | "%" ), unary_expression } ;

    unary_expression ::=
          ( "+" | "-" | "!" | "~" ), unary_expression
        | "&", [ "mut" ], unary_expression
        | postfix_expression ;

    postfix_expression ::=
        primary_expression, { call_suffix | index_suffix | field_suffix } ;

    call_suffix ::=
        "(", [ argument_list ], ")" ;

    argument_list ::=
        expression, { ",", expression } ;

    index_suffix ::=
        "[", expression, { ",", expression }, "]" ;

    field_suffix ::=
        ".", identifier ;

    primary_expression ::=
          literal
        | identifier
        | cast_expression
        | array_expression
        | struct_expression
        | "(", expression, ")" ;

    cast_expression ::=
        numeric_type, "(", expression, ")" ;

    numeric_type ::=
          "i32"
        | "u32"
        | "usize"
        | "f32"
        | "f64" ;
    ```

=== "Diagram"

    --8<-- "includes/railroad/expression.md"

    --8<-- "includes/railroad/range_expression.md"

    --8<-- "includes/railroad/range_operator.md"

    --8<-- "includes/railroad/logical_or_expression.md"

    --8<-- "includes/railroad/logical_and_expression.md"

    --8<-- "includes/railroad/bitwise_or_expression.md"

    --8<-- "includes/railroad/bitwise_xor_expression.md"

    --8<-- "includes/railroad/bitwise_and_expression.md"

    --8<-- "includes/railroad/equality_expression.md"

    --8<-- "includes/railroad/comparison_expression.md"

    --8<-- "includes/railroad/shift_expression.md"

    --8<-- "includes/railroad/additive_expression.md"

    --8<-- "includes/railroad/multiplicative_expression.md"

    --8<-- "includes/railroad/unary_expression.md"

    --8<-- "includes/railroad/postfix_expression.md"

    --8<-- "includes/railroad/call_suffix.md"

    --8<-- "includes/railroad/argument_list.md"

    --8<-- "includes/railroad/index_suffix.md"

    --8<-- "includes/railroad/field_suffix.md"

    --8<-- "includes/railroad/primary_expression.md"

    --8<-- "includes/railroad/cast_expression.md"

    --8<-- "includes/railroad/numeric_type.md"

Every operand of an equality or comparison operator is a shift expression, so
it cannot itself be an unparenthesized equality or comparison: `a < b < c`,
`a == b == c` and `a < b == c` are syntax errors, while `(a < b) == c` is valid
syntax.

### Allowed

- Arithmetic, equality, comparison, logical, bitwise, and shift operations.
- Inclusive and exclusive ranges with both endpoints, as the iterable of a
  `for` statement.
- Prefix unary operations, including shared and mutable references.
- Calls with zero or more arguments.
- One or more comma-separated indices.
- Chained postfix operations and parenthesized grouping.
- Casts to a numeric type, with exactly one operand.

```vortex
// fragment
left + right * scale
!(ready && enabled)
start..=finish
matrix[row, column]
points[index].x
scale(&mut values, 2.0)
f32(count) * scale
```

A cast such as `f32(value)` is a `cast_expression`: a numeric type keyword
followed by exactly one parenthesized expression. The parser recognizes it from
the keyword, so a cast never needs name lookup and is never a call. `void`,
`bool`, `char` and `String` cannot begin an expression, so `bool(value)` is a
syntax error. Which conversions are allowed is a type rule; see
[Casts and conversions](types-and-values.md#412-casts-and-conversions) and the
[decision record](../decisions/numbers.md#d1).

### Not allowed

- Assignment, ternary, value-producing `if`, `match`, or lambda expressions.
- Open-ended or chained ranges.
- A trailing comma in a call or index suffix.
- Optional chaining, null-coalescing, increment, or decrement.
- A cast to `void`, `bool`, `char` or `String`, or a cast with zero or several
  operands.
- Chained or mixed comparisons without parentheses.

```vortex
// fragment
count = 3
ready ? left : right
..finish
start..middle..finish
calculate(value,)
bool(flag)
f32(left, right)
a < b < c
a == b == c
```

### Semantic checks

Operators must support their operand types. Logical operands must be `bool`;
bitwise and shift operands must be valid integers. Equality operands must be
two values of the same `bool`, `char`, integer or floating-point type, and
ordering operands two values of the same integer or floating-point type
([Expressions](expressions.md#56-comparison-and-equality),
[why](../decisions/operators.md#d35)). A cast operand must have a numeric
type.

Calls, indices, fields, and references are checked against resolved types. A
range anywhere except as a `for` iterable is a type error. Each index must have
an integer type (`i32`, `u32` or `usize`), and a constant index outside its
extent is a constant-evaluation error
([Arrays and shapes, 7.6](arrays.md#76-indexing);
[decision 12](../decisions/arrays.md#d12)). An index suffix must supply exactly
one index per dimension of the indexed array; any other count is a type error
([decision 47](../decisions/arrays.md#d47)).

The operand of `&` or `&mut` must be a place, and `&mut` requires a mutable
place ([References and mutability 9.3](references.md#93-local-mutability)). A
name of type `&T` or `&mut T` used as a value reads its referent and has type
`T`, and index and field suffixes apply to the referent. v0.1 has no
dereference operator
([References and mutability 9.4](references.md#94-shared-references);
[record 40](../decisions/references.md#d40)). A reference expression may appear
only as a complete call argument or as the complete initializer of a `let`
without `mut`, optionally inside parentheses; anywhere else it is a type error
([References and mutability 9.7](references.md#97-lifetimes)).

Bitwise `&`, `^` and `|` bind less tightly than the comparisons, as in C, so
`a & b == c` parses as `a & (b == c)`. Because `&` needs integer operands and
`b == c` is a `bool`, type checking rejects it; the diagnostic should suggest
`(a & b) == c` ([why](../decisions/operators.md#d37)).

### AST responsibility

Preserve operators, children, and locations. A range preserves `..` versus
`..=`. An index preserves every index in order. A reference preserves whether
`mut` was written. A cast preserves its target type and its one operand; it is
not a call node.

## Array expressions

<span id="rule-array_expression"></span><span id="rule-array_element_list"></span><span id="rule-repeat_array_body"></span>

=== "Grammar"

    ```ebnf
    array_expression ::=
        "[", ( repeat_array_body | array_element_list ), "]" ;

    array_element_list ::=
        expression, { ",", expression }, [ "," ] ;

    repeat_array_body ::=
        expression, ";",
        expression, { ",", expression } ;
    ```

=== "Diagram"

    --8<-- "includes/railroad/array_expression.md"

    --8<-- "includes/railroad/array_element_list.md"

    --8<-- "includes/railroad/repeat_array_body.md"

The semicolon distinguishes a repeat array from an element list. Every repeat
dimension is a full expression.

### Allowed

- One or more explicit elements, optionally with a trailing comma.
- A repeated value followed by one or more dimension expressions.
- Multidimensional repeats and compound dimension expressions.

```vortex
// fragment
[1.0, 2.0, 3.0]
[1, 2 + 3, calculate()]
[0.0; 16]
[0.0; 2 + 2, 8 / 2]
[false; (4 * 2)]
```

Every dimension above is an integer constant expression. A dimension such as
`rows_per_tile * 2` also parses, but it is always a constant-evaluation error
in v0.1, because a name is never a constant expression.

### Not allowed

- Empty arrays.
- A missing repeat value or dimension.
- A trailing comma after repeat dimensions.
- Runtime-sized repeat arrays.
- Mixing the list comma form with the repeat semicolon form.

```vortex
// fragment
[]
[; 4]
[0.0;]
[0.0; 4,]
[1.0, 2.0; 4]
```

### Semantic checks

Element-list values need a compatible common type and shape. Each repeat
dimension must be an integer constant expression whose value is at least 1,
and the array's total size must not exceed the implementation's limit; a
violation is a constant-evaluation error
([Arrays and shapes, 7.2](arrays.md#72-dimension-rules)). Decided in
[decision 10](../decisions/arrays.md#d10) and
[decision 11](../decisions/arrays.md#d11).

### AST responsibility

An element-list array preserves ordered element expressions. A repeat array
preserves its value expression and ordered dimension expressions. The parser
must not reduce dimensions to integer tokens; the type checker evaluates them
when it resolves the array type.

## Struct expressions

<span id="rule-struct_expression"></span><span id="rule-field_initializer"></span>

=== "Grammar"

    ```ebnf
    struct_expression ::=
        identifier, "{",
        field_initializer,
        { ",", field_initializer },
        [ "," ],
        "}" ;

    field_initializer ::=
        identifier, ":", expression ;
    ```

=== "Diagram"

    --8<-- "includes/railroad/struct_expression.md"

    --8<-- "includes/railroad/field_initializer.md"

### Allowed

- A named struct expression with at least one named field.
- Any expression as a field value.
- An optional trailing comma.

```vortex
// fragment
Point { x: 0.0, y: 0.0 }

Point {
    x: calculate_x(),
    y: origin.y + offset,
}
```

### Not allowed

- Empty, positional, shorthand, anonymous, or update-style struct expressions.

```vortex
// fragment
Point {}
Point(0.0, 0.0) // parses as a call; a type error, since a struct is not a function
Point { x, y }
```

An identifier followed by `{` begins a struct expression only when its contents
have `field: value` form.

### Semantic checks

The name must resolve (otherwise a name error) to a struct (otherwise a type
error). Every field must appear exactly once, in any order; a missing, unknown
or repeated field, or an incompatible field value, is a type error
([why](../decisions/operators.md#d7)).

### AST responsibility

Preserve the struct name plus every field name, value, order, and location.

## Types

<span id="rule-type"></span><span id="rule-primitive_type"></span><span id="rule-array_type"></span><span id="rule-reference_type"></span>

=== "Grammar"

    ```ebnf
    type ::=
          primitive_type
        | array_type
        | reference_type
        | identifier ;

    primitive_type ::=
          "void"
        | "bool"
        | "char"
        | "i32"
        | "u32"
        | "usize"
        | "f32"
        | "f64"
        | "String" ;

    array_type ::=
        "[", type, ";",
        expression, { ",", expression },
        "]" ;

    reference_type ::=
        "&", [ "mut" ], type ;
    ```

=== "Diagram"

    --8<-- "includes/railroad/type.md"

    --8<-- "includes/railroad/primitive_type.md"

    --8<-- "includes/railroad/array_type.md"

    --8<-- "includes/railroad/reference_type.md"

Every array dimension is a full expression. The type checker evaluates it when
it resolves the array type, before type equality, layout, and code generation
([decision 52](../decisions/arrays.md#d52)).

### Primitive types

| Type | Meaning |
| --- | --- |
| `void` | No returned value |
| `bool` | `true` or `false` |
| `char` | One Unicode scalar value |
| `i32` | 32-bit signed integer |
| `u32` | 32-bit unsigned integer |
| `usize` | Unsigned size/index integer; implementation-defined width, 64 bits on every v0.1 target |
| `f32` | 32-bit floating-point number |
| `f64` | 64-bit floating-point number |
| `String` | UTF-8 string value |

The `char` and `usize` rows follow [decision 15](../decisions/lexical.md#d15)
and [decision 42](../decisions/numbers.md#d42).

### Allowed

- Listed primitive types.
- Fixed arrays with one or more dimension expressions.
- Nested array types.
- Shared and mutable references to a non-reference type, in the positions the
  semantic checks allow.
- Identifiers naming user-defined types.

```vortex
// fragment
i32
[f32; 16]
[f32; 2 + 2, 8 / 2]
[[i32; 2]; 3]
&[f32; 16]
&mut [f32; 4 * 2, 8]
Point
```

A type such as `&mut [f32; rows_per_tile * 2, columns_per_tile]` also parses,
but v0.1 always rejects it with a constant-evaluation error: a name is never a
constant expression, and v0.1 has no constant declarations
([decision 11](../decisions/arrays.md#d11)).

### Not allowed

- Missing dimensions or a trailing dimension comma.
- Runtime-sized arrays.
- Tuples, unions, function types, inferred `_` types, generics, aliases, or raw
  pointers.
- New primitive names such as `i64`, `u64`, `u8`, `f16`, or `bf16`. These
  spellings are reserved for a future version, so the lexer rejects them.

```vortex
// fragment
[f32;]
[f32; 4,]
Vector<f32>
*mut i32
u64 // lexical error: reserved for a future version
```

### Semantic checks

Named types must resolve (otherwise a name error) to a struct (otherwise a
type error), which may be declared anywhere in the file, including after the
use. `void` may appear only as a function's return type; anywhere else it is a
type error ([why](../decisions/operators.md#d44)). Every array dimension must
be an integer constant expression whose value is at least 1, and the array's
total size must not exceed the implementation's limit; a violation is a
constant-evaluation error ([decision 10](../decisions/arrays.md#d10)).

A reference type may appear only as a parameter type or as the type of a `let`
binding without `mut`. As a return type, a struct field type, an array element
type, the referenced type of another reference, or the type of a `let mut`
binding, it is a type error
([References and mutability 9.7](references.md#97-lifetimes);
[record 41](../decisions/references.md#d41)). The parser accepts all of these
forms; the type checker rejects them.

### AST responsibility

Use distinct primitive, array, reference, and named type nodes. An array type
preserves its element type and all dimension expressions. A reference preserves
its referenced type and `mut` flag. A named type preserves the identifier until
name resolution.

## Literals

<span id="rule-literal"></span><span id="rule-integer_literal"></span><span id="rule-decimal_integer"></span><span id="rule-binary_integer"></span><span id="rule-floating_literal"></span><span id="rule-exponent"></span><span id="rule-boolean_literal"></span><span id="rule-character_literal"></span><span id="rule-string_literal"></span><span id="rule-escape_sequence"></span>

=== "Grammar"

    ```ebnf
    literal ::=
          integer_literal
        | floating_literal
        | boolean_literal
        | character_literal
        | string_literal ;

    integer_literal ::=
          decimal_integer
        | binary_integer ;

    decimal_integer ::=
          "0"
        | nonzero_digit, { digit } ;

    binary_integer ::=
        "0b", binary_digit, { binary_digit } ;

    floating_literal ::=
        digit, { digit }, ".", digit, { digit }, [ exponent ] ;

    exponent ::=
        ( "e" | "E" ), [ "+" | "-" ], digit, { digit } ;

    boolean_literal ::=
          "true"
        | "false" ;

    character_literal ::=
        "'", ( character_content | escape_sequence ), "'" ;

    string_literal ::=
        '"', { string_content | escape_sequence }, '"' ;

    escape_sequence ::=
          "\\n"
        | "\\t"
        | "\\\\"
        | '\\"'
        | "\\'" ;
    ```

=== "Diagram"

    --8<-- "includes/railroad/literal.md"

    --8<-- "includes/railroad/integer_literal.md"

    --8<-- "includes/railroad/decimal_integer.md"

    --8<-- "includes/railroad/binary_integer.md"

    --8<-- "includes/railroad/floating_literal.md"

    --8<-- "includes/railroad/exponent.md"

    --8<-- "includes/railroad/boolean_literal.md"

    --8<-- "includes/railroad/character_literal.md"

    --8<-- "includes/railroad/string_literal.md"

    --8<-- "includes/railroad/escape_sequence.md"

`character_content` is one source character other than `'`, `\`, a line feed
or a carriage return. `string_content` is one source character other than
`"`, `\`, a line feed or a carriage return. A source character is one Unicode
scalar value that [Lexical structure 2.1](lexical-structure.md#source-encoding)
allows ([decision 15](../decisions/lexical.md#d15)).

The lexer reads a number as one run of characters and then checks it against
these rules. A run that matches no rule is one malformed numeric literal
([Lexical structure 2.5](lexical-structure.md#how-far-a-number-extends),
[decision 17](../decisions/lexical.md#d17)).

### Allowed

```vortex
// fragment
0
42
0b101010
3.14
1.0e-4
true
'A'
'λ'
'\n'
"Vortex"
"line one\nline two"
```

### Not allowed

- Hexadecimal or octal notation, numeric separators, or type suffixes.
- A leading or trailing decimal point without digits on both sides.
- Raw, byte, or multiline strings.
- Unsupported escapes.
- More or fewer than one character or escape in a character literal.
- A decimal integer with a leading zero, such as `010`.
- The uppercase binary prefix `0B`.
- An exponent without a decimal point, such as `1e5`, or without digits, such
  as `1.0e`.

```vortex
// fragment
0xff
1_000
42u32
010
0B101
1e5
1.0e
.5    // syntax error: lexes as . then 5
5.
"bad\rvalue"
'ab'
```

Every line except `.5` is a lexical error.

### Semantic checks

A numeric literal takes its type from its context as
[literal typing](types-and-values.md#literal-typing) describes; with no target
type, an integer literal has type `i32` and a floating literal has type `f32`.
An integer literal never takes a floating-point type, and a floating literal
never takes an integer type. The literal's value must be representable in its
type, or the program has a type error
([4.3](types-and-values.md#43-integers),
[4.4](types-and-values.md#44-floating-point-values)). A leading `-` is a unary
operator applied to a positive numeric literal, not part of the literal
grammar; when it is written directly before an integer literal, the negated
value is checked, so `-2147483648` is a valid `i32`. Decided in
[decision 31](../decisions/numbers.md#d31) and
[decision 32](../decisions/numbers.md#d32).

### AST responsibility

Preserve literal category, location, and the literal's exact value: the decoded
value of a character or string literal, and the spelling (or an exact value)
of a numeric literal. Type checking assigns the concrete numeric type later, so
an integer literal too large for any fixed-width integer must still reach type
checking, which reports it, and a floating literal is rounded once, to its
final type ([decision record](../decisions/numbers.md#d32)). `-42` is a negate
node containing the positive literal `42`.

## Identifiers and keywords

<span id="rule-identifier"></span><span id="rule-identifier_start"></span><span id="rule-identifier_continue"></span><span id="rule-digit"></span><span id="rule-nonzero_digit"></span><span id="rule-binary_digit"></span>

=== "Grammar"

    ```ebnf
    identifier ::=
        identifier_start, { identifier_continue } ;

    identifier_start ::=
          letter
        | "_" ;

    identifier_continue ::=
          letter
        | digit
        | "_" ;

    digit ::= "0" | "1" | "2" | "3" | "4" | "5" | "6" | "7" | "8" | "9" ;

    nonzero_digit ::= "1" | "2" | "3" | "4" | "5" | "6" | "7" | "8" | "9" ;

    binary_digit ::= "0" | "1" ;
    ```

=== "Diagram"

    --8<-- "includes/railroad/identifier.md"

    --8<-- "includes/railroad/identifier_start.md"

    --8<-- "includes/railroad/identifier_continue.md"

    --8<-- "includes/railroad/digit.md"

    --8<-- "includes/railroad/nonzero_digit.md"

    --8<-- "includes/railroad/binary_digit.md"

In v0.1, `letter` means ASCII `A` through `Z` or `a` through `z`. Names are
case-sensitive.

### Allowed

```text
value
item_count
Point
_temporary
matrix2
```

### Not allowed

- A digit first, punctuation other than `_`, a keyword, a word reserved for a
  future version, or non-ASCII letters.

```vortex
// statements: lexical error
let 2values = 0;   // lexed as one malformed numeric literal
let u64 = 0;       // reserved for a future version
```

```vortex
// statements: syntax error
let item-count = 0;
let while = true;
```

The v0.1 keywords are:

```text
fn      struct  let     mut     return
if      else    while   for     in
break   continue
void    bool    char    i32     u32
usize   f32     f64     String
true    false
```

The words reserved for a future version are:

```text
i8      i16     i64     u8      u16
u64     f16     bf16    const
```

They have no meaning in v0.1, and the lexer rejects each one
([Lexical structure 2.4](lexical-structure.md#24-keywords),
[decision 29](../decisions/lexical.md#d29)).

### Semantic checks

The lexer distinguishes keywords from identifiers and rejects reserved words.
Later stages resolve names, enforce scope, and diagnose duplicates or unknown
names.

### AST responsibility

Preserve spelling and location. Do not resolve a type-name or value-name inside
the parser.

## Implementation checklist

### Source file and declarations

- [ ] Parse an ordered sequence of functions and structs.
- [ ] Parse typed parameters and optional return annotations.
- [ ] Parse structs with one or more fields and an optional trailing field
  comma; reject an empty field list.
- [ ] Preserve declaration and child-node locations.

### Statements

- [ ] Parse variable, assignment, return, expression, `if`, `while`, `for`,
  `break`, `continue`, and block statements.
- [ ] Preserve variable mutability and optional written type.
- [ ] Restrict assignment syntax to assignment-target shapes.
- [ ] Preserve exact assignment operators and `else if` structure.

### Expressions

- [ ] Implement every precedence level in the documented order.
- [ ] Keep unary parsing right-associative, binary loops left-associative, and
  equality and comparison operators non-associative.
- [ ] Reject `a < b < c`, `a == b == c` and `a < b == c` as syntax errors.
- [ ] Prevent a range from consuming a second range operator.
- [ ] Preserve exclusive/inclusive ranges and shared/mutable references.
- [ ] Support chained calls, multidimensional indices, and fields.
- [ ] Parse a cast when `i32`, `u32`, `usize`, `f32` or `f64` begins an
  expression.
- [ ] Keep assignment outside the expression parser.

### Arrays and types

- [ ] Distinguish element lists from repeat arrays at `;`.
- [ ] Parse each repeat-array dimension with the full expression parser.
- [ ] Parse each array-type dimension with the full expression parser.
- [ ] Stop a dimension expression at its enclosing comma or `]`.
- [ ] Preserve dimension expressions in source order.
- [ ] Leave integer typing and compile-time evaluation to semantic analysis.
- [ ] Build distinct primitive, array, reference, and named type nodes.

### Diagnostics and verification

- [ ] Report the unexpected token, its location, and the expected form.
- [ ] Recover at `;`, `}`, or the next top-level `fn`/`struct` when possible.
- [ ] Add accepted and rejected tests for every major production.
- [ ] Test compound dimensions such as `[f32; 2 + 2]` and `[0.0; 4 * 8]`.
- [ ] Verify that dimensions using names or calls, non-integer dimensions, and
  zero dimensions parse first, then fail with a constant-evaluation error.

## v0.1 boundary summary

Vortex v0.1 contains functions, structs, local variables, fixed-size arrays,
references, imperative control flow, and a compact expression system. It does
not contain modules, generics, classes, enums, methods, pattern matching,
exceptions, closures, dynamic arrays, raw pointers, or runtime-sized layouts.

Full expression syntax for array dimensions does not make arrays dynamic. The
parser accepts the expression; the type checker must still evaluate it as an
integer constant expression before layout or code generation.

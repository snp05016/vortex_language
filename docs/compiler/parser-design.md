# Vortex v0.1 parser design and implementation assignment

This document is the working reference for implementing the Vortex parser. It
explains what the parser must recognize, what AST node it should build, what it
must reject immediately, and what must be left for semantic analysis.

The source syntax is defined by the
[Vortex v0.1 Grammar](../specification/grammar.md), and
[document authority](../specification/conformance.md#11-document-authority)
says which page decides when two disagree. This page is informative. The C++
node shapes of the actual compiler are those in
[`ast.h`](https://github.com/snp05016/vortex_language/blob/main/src/frontend/ast.h).

> **Implementation status:** `parser.cpp` is currently a scaffold. The designs
> and function shapes below describe one implementation strategy, not the
> owner's code, and do not claim that every parser function already exists.

## Contents

- [1. Assignment objective](#1-assignment-objective)
- [2. What the parser does and does not do](#2-what-the-parser-does-and-does-not-do)
- [3. Required parser result](#3-required-parser-result)
- [4. Token-stream helpers](#4-token-stream-helpers)
- [5. Source grammar to parser-function map](#5-source-grammar-to-parser-function-map)
- [6. Parsing a complete program](#6-parsing-a-complete-program)
- [7. Parsing declarations](#7-parsing-declarations)
- [8. Parsing types](#8-parsing-types)
- [9. Parsing statements](#9-parsing-statements)
- [10. Parsing expressions](#10-parsing-expressions)
- [11. Arrays and expression dimensions](#11-arrays-and-expression-dimensions)
- [12. Assignment disambiguation](#12-assignment-disambiguation)
- [13. Source locations](#13-source-locations)
- [14. Diagnostics and recovery](#14-diagnostics-and-recovery)
- [15. Ownership and constructors](#15-ownership-and-constructors)
- [16. Recommended implementation order](#16-recommended-implementation-order)
- [17. Parser test assignment](#17-parser-test-assignment)
- [18. Completion checklist](#18-completion-checklist)

## 1. Assignment objective

Implement a recursive-descent parser that:

1. consumes the token stream produced by `Lexer`;
2. recognizes every Vortex v0.1 declaration, statement, type, and expression;
3. preserves operator precedence and associativity;
4. constructs owned AST nodes with accurate source locations;
5. reports useful syntax errors; and
6. recovers well enough to report more than one error when practical.

The core transformation is:

```text
Token stream -> Parser -> AST
```

For example:

```vortex
// fragment
let result: i32 = left + right * 2;
```

should become a tree shaped approximately like this:

```text
VarDeclStmt
├── name: result
├── declared type: PrimitiveType(i32)
└── initializer: BinaryExpr(Add)
    ├── Identifier(left)
    └── BinaryExpr(Multiply)
        ├── Identifier(right)
        └── Literal(2)
```

The multiplication node is nested below the addition node because `*` has
higher precedence than `+`.

## 2. What the parser does and does not do

This boundary is essential. The parser checks **form**. Later stages check
**meaning**.

| Question | Parser? | Later phase |
| --- | --- | --- |
| Is a semicolon present? | Yes | - |
| Are braces, brackets, and parentheses balanced? | Yes | - |
| Does a parameter have `name: type` form? | Yes | - |
| Does an operator have the required operands? | Yes | - |
| Does an identifier refer to a declared name? | No | Name resolution |
| Is `value + other` valid for their types? | No | Type checking |
| Is an assignment target mutable? | No | Semantic checking |
| Is `break;` inside a loop? | No | Semantic checking |
| Does `return` match the function return type? | No | Type checking |
| Is an array dimension an integer constant expression of at least 1? | No | Type checking, while resolving the array type |
| Does `main` have the required signature? | No | Name resolution |

### The parser can accept

```vortex
// fragment
let value: MissingType = unknown_name + true;
```

This is structurally valid. Name and type checking must report its meaning
errors later.

### The parser must reject

```vortex
// statements: syntax error
let value i32 = 10;
```

The required `:` is missing, so the token sequence does not match the grammar.

### Do not put these jobs in the parser

- Do not maintain the final symbol table.
- Do not decide the final type of an unannotated integer literal.
- Do not execute or constant-fold ordinary expressions while parsing.
- Do not reject a name merely because it has not yet been declared.
- Do not generate LLVM IR or machine code.

## 3. Required parser result

The long-term parser entry point should return an AST root rather than only
printing that a token was seen.

A representative interface is:

```cpp
std::unique_ptr<Program> parse_program();
```

If the AST does not yet have a `Program` node, a temporary result may be a
vector of declarations:

```cpp
std::vector<std::unique_ptr<Decl>> parse_program();
```

Choose one interface that matches the current AST and use it consistently. A
successful parse must preserve ownership of every child node. A failed parse
must produce a diagnostic rather than silently returning an incomplete tree.

## 4. Token-stream helpers

Keep token movement in a small set of helpers. This prevents every grammar
function from duplicating lexer logic.

| Helper | Purpose | Consumes a token? |
| --- | --- | --- |
| `current()` | Return the current token | No |
| `previous()` | Return the most recently consumed token | No |
| `check(kind)` | Test the current token kind | No |
| `match(kind)` | Consume only when the kind matches | Sometimes |
| `advance()` | Move to the next token | Yes |
| `expect(kind, message)` | Require and consume a token or report an error | Yes on success |
| `at_end()` | Test for `EOF_TOKEN` | No |

Example behavior:

```cpp
if (match(TokenKind::KW_MUT)) {
  is_mutable = true;
}

Token name = expect(TokenKind::IDENTIFIER,
                    "expected a variable name after 'let'");
```

### Allowed

- Look ahead without consuming when choosing between grammar alternatives.
- Preserve the first token of a construct for the AST source location.
- Centralize unexpected-token messages through `expect`.

### Not allowed

- Calling `lexer.next_token()` independently throughout every parser function.
- Consuming a token merely to inspect it and then trying to reconstruct it.
- Looping after `EOF_TOKEN` without terminating.
- Ignoring `INVALID` tokens.

## 5. Source grammar to parser-function map

Use one focused function for each important nonterminal. Names may differ, but
responsibilities should remain separate.

| Grammar area | Suggested parser function | Result |
| --- | --- | --- |
| `program` | `parse_program()` | program/root declarations |
| `function` | `parse_function_decl()` | `unique_ptr<Decl>` |
| `struct_definition` | `parse_struct_decl()` | `unique_ptr<Decl>` |
| `parameter` | `parse_parameter()` | parameter node/value |
| `struct_field` | `parse_struct_field()` | field node/value |
| `type` | `parse_type()` | `unique_ptr<Type>` |
| `block` | `parse_block()` | `unique_ptr<BlockStmt>` |
| `statement` | `parse_statement()` | `unique_ptr<Stmt>` |
| `variable_declaration` | `parse_variable_decl()` | `unique_ptr<Stmt>` |
| `return_statement` | `parse_return_stmt()` | `unique_ptr<Stmt>` |
| `if_statement` | `parse_if_stmt()` | `unique_ptr<Stmt>` |
| `while_statement` | `parse_while_stmt()` | `unique_ptr<Stmt>` |
| `for_statement` | `parse_for_stmt()` | `unique_ptr<Stmt>` |
| `expression` | `parse_expression()` | `unique_ptr<Expr>` |
| `primary_expression` | `parse_primary()` | `unique_ptr<Expr>` |

The implementation may use either one function per precedence level or a Pratt
parser for expressions. Do not implement two competing expression parsers.

## 6. Parsing a complete program

At the top level, v0.1 permits function and struct declarations.

```vortex
// program: valid
struct Point {
    x: f32,
    y: f32,
}

fn main() {
    let origin = Point { x: 0.0, y: 0.0 };
}
```

Suggested control flow:

```text
parse_program
└── while not EOF
    ├── `fn`     -> parse_function_decl
    ├── `struct` -> parse_struct_decl
    └── other    -> top-level syntax error and synchronize
```

### Allowed at top level

- `fn` declarations
- `struct` declarations
- comments and whitespace, which the lexer normally removes as trivia

### Not allowed at top level in v0.1

- `let` variables
- executable expressions
- assignments
- standalone blocks
- type aliases, enums, classes, or imports

The parser only records declarations. Name resolution later checks that there
is exactly one valid `main` function
([Programs and declarations 3.3](../specification/declarations.md#33-entry-point),
[decision](../decisions/program.md#d6)).

## 7. Parsing declarations

### 7.1 Function declaration

Required shape:

```vortex
// items: valid
fn add(left: i32, right: i32) -> i32 {
    return left + right;
}
```

Parse in this order:

1. consume `fn`;
2. require the function name;
3. require `(`;
4. parse zero or more comma-separated parameters;
5. require `)`;
6. if `->` appears, parse the return type;
7. otherwise represent the return type as `void` according to the AST design;
8. parse the body block; and
9. construct the function declaration node.

Valid:

```vortex
// items: valid
fn run() {}
fn identity(value: i32) -> i32 { return value; }
fn combine(a: i32, b: i32) -> i32 { return a + b; }
```

Invalid syntax:

```vortex
// items: syntax error
fn () {}                       // missing function name
fn add(left i32) {}            // missing ':'
fn add(left: i32 right: i32) {} // missing comma
fn add() i32 {}                // missing '->'
```

Do not reject duplicate parameter names in the parser. That is a declaration
and scope rule for semantic analysis.

### 7.2 Parameter declaration

A parameter contains a name and a type, but no runtime value:

```vortex
// fragment
left: i32
```

The parser stores `left` as the parameter name and a `PrimitiveType(i32)` child
as its type. Literal values belong to call arguments or initializers, not to
the parameter declaration.

A parameter is an owned child record of its function, not an independent
top-level declaration. A simple representation is therefore
`std::vector<ParamDecl>` inside `FunctionDecl`. It does not need
`std::unique_ptr<ParamDecl>` unless parameter objects require stable addresses
or polymorphic subclasses. The `ParamDecl` may still own its polymorphic type
child with `std::unique_ptr<Type>`.

### 7.3 Struct declaration

Required shape:

```vortex
// items: valid
struct Point {
    x: f32,
    y: f32,
}
```

The parser records the struct name and each field declaration in source order.
A struct needs at least one field
([why](../decisions/operators.md#d26)), and the final comma is optional.
Duplicate field names are syntactically valid but must be rejected by semantic
analysis.

Valid:

```vortex
// items: valid
struct Pair { left: i32, right: i32 }
struct Span { left: i32, right: i32, }
```

Invalid syntax:

```vortex
// items: syntax error
struct { x: i32 }              // missing struct name
struct Point { x i32 }         // missing ':'
struct Point { x: i32 y: i32 } // missing comma
struct Empty {}                // at least one field is required
```

## 8. Parsing types

Every type parser call returns one concrete `Type` child.

| First token | Type form | Example AST meaning |
| --- | --- | --- |
| primitive keyword | primitive | `PrimitiveType(i32)` |
| identifier | named/user-defined | `NamedType("Point")` or current equivalent |
| `[` | array | element type plus dimension expressions |
| `&` | reference | referred-to type plus mutability |

### 8.1 Primitive type

```vortex
// fragment
void  bool  char  i32  u32  usize  f32  f64  String
```

The token determines one `PrimitiveTypeKind`. This enum is type information;
it is not the same as a literal's stored value.

### 8.2 Named type

```vortex
// fragment
Point
```

The parser stores only the name. It does not need to know yet whether `Point`
was declared or whether it names a struct.

### 8.3 Reference type

```vortex
// fragment
&i32
&mut [f32; 4]
```

After `&`, consume optional `mut`, then recursively parse another type. The
parser records mutability and accepts a reference type in every type position,
including a return type, a field type, an array element type and `& &i32`.
Where references may appear, and the borrow rules, belong to semantic analysis,
which reports a misplaced reference type as a type error
([decision](../decisions/references.md#d41)).

### 8.4 Array type

```vortex
// fragment
[f32; 16]
[f32; 2 + 2, 8 / 2]
```

After `[`, parse:

1. one element `type`;
2. one `;`;
3. one or more comma-separated dimension **expressions**; and
4. the closing `]`.

The AST stores each dimension as `std::unique_ptr<Expr>`. The parser does not
convert dimensions directly to `uint64_t` because doing so would lose their
source structure.

Valid syntax:

```vortex
// fragment
[i32; 4]
[f32; 2 + 2]
[[i32; 4]; 3]
[f32; rows, columns]
```

The last example is syntactically valid. For fixed-size v0.1 arrays, the type
checker must still check that every dimension is an integer constant
expression with a value of at least 1. A name or a function call therefore
always fails later, with a constant-evaluation error
([decision](../decisions/arrays.md#d11)). A zero extent is rejected by the
semantic rules, not by the parser ([decision](../decisions/arrays.md#d10)).

Invalid syntax:

```vortex
// fragment
[i32]       // missing dimensions
[i32;]      // missing first dimension expression
[i32; 4,]   // trailing dimension comma is not in the v0.1 grammar
[; 4]       // missing element type
```

## 9. Parsing statements

`parse_statement()` dispatches using the current token.

| Current token or shape | Statement parser |
| --- | --- |
| `let` | variable declaration |
| `return` | return statement |
| `if` | if statement |
| `while` | while statement |
| `for` | for statement |
| `break` | break statement |
| `continue` | continue statement |
| `{` | block statement |
| expression followed by assignment operator | assignment statement |
| other expression followed by `;` | expression statement |

### 9.1 Variable declaration

```vortex
// statements: valid
let count = 10;
let mut total: i32 = 0;
```

The node must preserve:

- variable name;
- whether `mut` was present;
- optional declared type; and
- required initializer expression.

Valid:

```vortex
// fragment
let value = calculate();
let mut limit: i32 = 10;
```

Invalid syntax:

```vortex
// statements: syntax error
let = 10;          // missing name
let value;         // initializer is required in v0.1
let value: = 10;   // missing type
let value = 10     // missing semicolon
```

### 9.2 Assignment statement

```vortex
// fragment
value = 10;
point.x += 1.0;
matrix[row, column] = 0.0;
```

The parser records the target, assignment operator, and value. It should limit
the target shape to identifier, field access, and indexing chains. Whether the
target is mutable and whether the value type is compatible are later checks.

Not assignment expressions:

```vortex
// statements: syntax error
let result = (value = 10); // invalid: assignment is a statement
call(value = 10);          // invalid for the same reason
```

### 9.3 Return statement

```vortex
// fragment
return;
return value;
```

The expression is optional syntactically. The type checker compares it with the
current function return type.

### 9.4 If statement

```vortex
// fragment
if condition {
    run();
} else if other {
    recover();
} else {
    stop();
}
```

Braces are required. Parentheses around the condition are not required by the
grammar. The parser accepts any expression as the condition; type checking
later requires `bool`.

### 9.5 While statement

```vortex
// fragment
while index < limit {
    index += 1;
}
```

The AST stores the condition expression and body block.

### 9.6 For statement

```vortex
// statements: valid
for index in 0..10 {
    print(index);
}
```

Vortex uses `for name in expression`, not a C-style initializer-condition-step
loop. The AST must preserve the loop variable name, iterable expression, and
body. The parser accepts any expression as the iterable. Type checking then
requires a range, possibly in parentheses, whose endpoints have the same
integer type, and reports anything else as a type error
([decision 13](../decisions/statements.md#d13),
[decision 36](../decisions/statements.md#d36)). Leaving the check to type
checking gives a precise message and lets planned iterables, such as arrays,
arrive without a grammar change.

### 9.7 Break and continue

```vortex
// fragment
break;
continue;
```

Both require a semicolon and have no expression child. Their placement inside
a loop is checked later.

### 9.8 Block

```vortex
// statements: valid
{
    let value = 10;
    print(value);
}
```

Parse statements until `}` or end of file. Reaching end of file before `}` is
an unterminated-block syntax error.

## 10. Parsing expressions

Expressions must preserve precedence. From tightest binding to loosest:

| Level | Operators/forms | Associativity |
| --- | --- | --- |
| Postfix | calls `()`, indexing `[]`, field access `.` | chained left-to-right |
| Unary | prefix `+`, `-`, `!`, `~`, `&`, `&mut` | right-associative |
| Multiplicative | `*`, `/`, `%` | left-associative |
| Additive | `+`, `-` | left-associative |
| Shift | `<<`, `>>` | left-associative |
| Comparison | `<`, `<=`, `>`, `>=` | non-associative: a second comparison or equality operator without parentheses is a syntax error |
| Equality | `==`, `!=` | non-associative: a second comparison or equality operator without parentheses is a syntax error |
| Bitwise AND | `&` | left-associative |
| Bitwise XOR | `^` | left-associative |
| Bitwise OR | `\|` | left-associative |
| Logical AND | `&&` | left-associative |
| Logical OR | `\|\|` | left-associative |
| Range | `..`, `..=` | at most one range operator |

So `a < b < c` and `a < b == c` are syntax errors, while `(a < b) == c` parses
([why](../decisions/operators.md#d37)).

### 10.1 Primary expressions

The first token determines the primary form:

| Token | Node/form |
| --- | --- |
| integer, float, boolean, char, or string literal | literal expression |
| identifier | identifier or struct construction |
| `i32`, `u32`, `usize`, `f32` or `f64` followed by `(` | cast expression |
| `[` | array or repeat-array expression |
| `(` | grouped expression |

Valid:

```vortex
// fragment
42
name
(left + right)
[1, 2, 3]
Point { x: 1.0, y: 2.0 }
f32(count)
```

Invalid:

```vortex
// fragment
()           // no unit/empty-tuple expression in v0.1
[]           // not an expression: an array has at least one element
(1 + 2       // missing ')'
bool(flag)   // only numeric types can begin a cast
```

### 10.2 Postfix expressions

Start with one primary, then repeatedly apply suffixes:

```vortex
// fragment
factory().points[row, column].x
```

This becomes a nested tree in source order. Each suffix wraps the expression
built so far.

### 10.3 Unary expressions

```vortex
// fragment
-value
!ready
~bits
&value
&mut value
```

The operand is another expression node, not a numeric value field. That allows
nested forms such as `-(left + right)` and `!!ready`. There is no dereference
operator. A name of reference type is an ordinary identifier expression; the
type checker, not the parser, decides that it reads or writes the referent
([decision](../decisions/references.md#d40)).

### 10.4 Binary expressions

Build one `BinaryExpr` per operator. Do not use lexer token kinds as the final
semantic operator representation; map them to the AST's `BinOp` enum.

```vortex
// fragment
1 + 2 * 3
```

Correct:

```text
Add(1, Multiply(2, 3))
```

Incorrect:

```text
Multiply(Add(1, 2), 3)
```

### 10.5 Ranges

```vortex
// fragment
0..10
0..=10
```

The AST must preserve whether the end is exclusive (`..`) or inclusive (`..=`).
The parser should not erase that distinction. The parser accepts a range
wherever an expression may appear, including `let span = 0..10;`. Type
checking allows a range only as the iterable of a `for` statement and reports
it anywhere else as a type error
([decision 36](../decisions/statements.md#d36)).

### 10.6 Calls and casts

```vortex
// fragment
add(left, right)
f32(count)
```

The two look alike but start differently. A cast begins with one of the
keywords `i32`, `u32`, `usize`, `f32` or `f64`, so the parser knows it has a
cast before it reads the `(`. It builds a cast node that holds the target type
and exactly one operand, never a call node, and name resolution never looks up
the type name. A call begins with any other primary expression, usually a
name. See the [cast grammar](../specification/grammar.md#expressions-and-precedence)
and the [decision record](../decisions/numbers.md#d1).

## 11. Arrays and expression dimensions

Vortex has three related but distinct constructs:

| Construct | Example | AST information |
| --- | --- | --- |
| Element-list array | `[1, 2, 3]` | vector of element expressions |
| Repeat array | `[0; 2 + 2]` | repeated value plus dimension expressions |
| Array type | `[i32; 2 + 2]` | element type plus dimension expressions |

### 11.1 Element-list arrays

```vortex
// fragment
[first(), second(), third()]
```

Every element is already an expression. The parser keeps source order. Later
type checking requires compatible element types.

### 11.2 Repeat arrays

```vortex
// fragment
[0.0; 4]
[0.0; 2 + 2, 8 / 2]
```

The expression before `;` is the value to repeat. Every item after `;` is a
dimension expression. Do not confuse the two roles.

Approximate tree:

```text
RepeatArrayExpr
├── value: Literal(0.0)
└── dimensions
    ├── BinaryExpr(Add, 2, 2)
    └── BinaryExpr(Divide, 8, 2)
```

### 11.3 Array types

```vortex
// fragment
[f32; 2 + 2, 8 / 2]
```

Approximate tree:

```text
ArrayType
├── element type: PrimitiveType(f32)
└── dimensions
    ├── BinaryExpr(Add, 2, 2)
    └── BinaryExpr(Divide, 8, 2)
```

### 11.4 What dimensions may contain

Syntactically, dimensions are expressions. Therefore, the parser can build
nodes for literals, arithmetic, identifiers, grouping, and calls.

For fixed-size v0.1 arrays, the type checker then requires each dimension to
be an integer constant expression (integer literals with `+`, `-`, `*`, `/`,
`%` and parentheses) whose value, computed with checked `usize` arithmetic, is
at least 1.

| Example | Parser result | Semantic result for fixed-size arrays |
| --- | --- | --- |
| `[i32; 4]` | Accepted | Accepted |
| `[i32; 2 + 2]` | Accepted | Accepted: evaluates to `4` |
| `[i32; (8 / 2)]` | Accepted | Accepted: evaluates to `4` |
| `[i32; 3.5]` | Accepted syntax | Rejected: not an integer extent |
| `[i32; 0]` | Accepted syntax | Rejected: every extent must be at least 1 (constant-evaluation error) |
| `[i32; rows]` | Accepted syntax | Rejected: a name is never a constant expression |
| `[i32; runtime_size()]` | Accepted syntax | Rejected: a call is never a constant expression |

This phase split is the reason the AST stores `Expr` children instead of raw
integer values.

### 11.5 Disambiguating array forms

After consuming `[`, parse the first item. The next separator tells you which
form is present:

```text
`,` or `]` after an expression -> element-list array
`;` after an expression        -> repeat array
`;` after a parsed type        -> array type, but only in a type context
```

Type context removes the hardest ambiguity. `parse_type()` knows that `[`
begins an array type; `parse_primary()` knows that `[` begins an array value.

## 12. Assignment disambiguation

An identifier may begin either an expression statement or an assignment:

```vortex
// fragment
calculate(value);
value = calculate();
point.x += 1.0;
```

One practical strategy is:

1. parse an expression candidate;
2. if the next token is an assignment operator, validate that the candidate's
   shape is assignable and parse an assignment statement;
3. otherwise require `;` and produce an expression statement.

The grammar currently restricts targets to identifier, indexing, and field
access chains. Reject targets such as:

```vortex
// statements: syntax error
(left + right) = 10;
call() = 10;
42 = value;
```

Whether a structurally valid target is mutable is not a parser decision.

## 13. Source locations

Every AST node should point to useful source text. At minimum, use the first
token of the construct:

| Node | Suggested starting location |
| --- | --- |
| Function declaration | `fn` |
| Struct declaration | `struct` |
| Variable declaration | `let` |
| Binary expression | left operand or operator, consistently |
| Unary expression | prefix operator |
| Call expression | callee start |
| Array expression/type | opening `[` |
| Block | opening `{` |

If `SourceLocation` represents only a point rather than a full span, preserve
the best available start location now. Full spans can be added later without
changing grammar responsibilities.

## 14. Diagnostics and recovery

Good diagnostics say what was expected, what appeared, and where.

Weak:

```text
parse error
```

Useful:

```text
expected ')' after function parameters; found '->'
```

### Suggested synchronization points

After an error, advance until one of these safe boundaries:

- `;`
- `}`
- `fn`
- `struct`
- end of file

Do not recover inside a nested construct by skipping past its closing delimiter
without tracking nesting. It may discard the rest of a valid function.

### Required malformed-input cases

- unexpected end of file inside a block;
- missing closing `)`, `]`, or `}`;
- missing semicolon;
- missing name after `fn`, `struct`, `let`, or `for`;
- missing type after `:` or `->`;
- missing expression after a unary or binary operator;
- empty array literal;
- a struct with no fields;
- a chained comparison such as `a < b < c` or `a < b == c`;
- missing repeat-array dimension; and
- invalid top-level token.

## 15. Ownership and constructors

Use `std::unique_ptr` for owned child nodes because each AST child has one
owning parent.

```cpp
auto result = std::make_unique<BinaryExpr>(
    location,
    BinOp::Add,
    std::move(left),
    std::move(right));
```

After `std::move(left)`, do not read or reuse `left` as though it still owns the
node.

A concrete-node constructor should receive enough information to create a
valid node. For example, a binary-expression constructor needs its operator and
both operands, not only a source location.

### Required ownership rules

- A parent owns its child nodes.
- Vectors of nodes use `vector<unique_ptr<...>>` when the children are
  polymorphic.
- Small plain values such as enums, booleans, and names are stored directly.
- Optional child syntax may be represented by a null `unique_ptr` when the AST
  documents that meaning.
- Do not create owning raw pointers with `new` in parser code.

## 16. Recommended implementation order

Implement and test one vertical slice at a time.

1. Token buffering and helper methods.
2. Primitive and named types.
3. Literals, identifiers, and grouping.
4. Unary and binary precedence.
5. Calls, indexing, and field access.
6. Array values, repeat arrays, and array types with expression dimensions.
7. Variable, return, and expression statements.
8. Blocks and control flow.
9. Parameters, functions, fields, and structs.
10. Complete-program parsing.
11. Assignment disambiguation.
12. Diagnostics and recovery.

After each step, test both a valid form and its closest invalid form.

## 17. Parser test assignment

Tests should inspect AST shape, not merely whether parsing returned.

### 17.1 Expression precedence

Input:

```vortex
// fragment
1 + 2 * 3
```

Required assertion: the root is addition and its right child is multiplication.

### 17.2 Postfix chaining

Input:

```vortex
// fragment
factory().items[row, column].value
```

Required assertion: suffix nodes are nested in source order.

### 17.3 Expression dimensions

Input:

```vortex
// fragment
[f32; 2 + 2, 8 / 2]
```

Required assertion: the `ArrayType` has two dimension expression children; the
first is addition and the second is division.

### 17.4 Repeat array

Input:

```vortex
// fragment
[0.0; 2 + 2, 4]
```

Required assertion: the repeated value is separate from both dimension nodes.

### 17.5 Declaration

Input:

```vortex
// items: valid
fn add(left: i32, right: i32) -> i32 {
    return left + right;
}
```

Required assertions: function name, two parameters, return type, one body
statement, and binary return expression are all preserved.

### 17.6 Invalid syntax

At minimum, add focused cases for:

```vortex
// statements: syntax error
let value = ;
let sizes: [i32;] = [1];
let zeros = [0;];
let ready = true;
if ready print(ready);
```

```vortex
// items: syntax error
fn add(left i32) {}
```

Each line with a mistake is a separate test case
([example labels](../decisions/documentation.md#d28)). Each test should assert
a meaningful failure location and message category.

## 18. Completion checklist

### Token handling

- [ ] Parser stops at `EOF_TOKEN`.
- [ ] `check`, `match`, `advance`, and `expect` have consistent behavior.
- [ ] Invalid lexer tokens become parser diagnostics.

### Declarations and types

- [ ] Functions and structs parse at the top level.
- [ ] Parameters and fields retain names, types, and locations.
- [ ] Primitive, named, reference, and array types produce distinct nodes.
- [ ] Array-type dimensions are stored as expression nodes.

### Statements

- [ ] Every v0.1 statement form parses.
- [ ] Variable declarations preserve mutability and optional type annotations.
- [ ] `for` uses `name in expression` structure.
- [ ] Assignment remains a statement.

### Expressions

- [ ] Precedence and associativity match the grammar.
- [ ] `..` and `..=` remain distinguishable in the AST.
- [ ] Calls, multidimensional indices, and field accesses chain correctly.
- [ ] Repeat-array dimensions are stored as expression nodes.
- [ ] Empty arrays are rejected.

### Quality

- [ ] Every required delimiter uses a useful `expect` message.
- [ ] AST nodes receive meaningful source locations.
- [ ] Ownership uses `unique_ptr` consistently.
- [ ] Parser tests check tree shapes and invalid forms.
- [ ] Parser code does not perform name resolution or type checking.

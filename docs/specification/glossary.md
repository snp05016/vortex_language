# 12. Glossary

This glossary fixes the meaning of recurring terms in the Vortex
specification. Language-specific definitions link to the chapter containing the
full rule.

## Terms

abstract syntax tree (AST)
: A tree representation that preserves the meaningful structure of parsed
  source without retaining every punctuation token as its own node. See the
  [AST learning guide](../compiler/ast-guide.md).

addressable
: Describes an expression that identifies existing storage and can therefore
  be referenced. Every place is addressable; temporary calculations are not.

aggregate type
: A type composed of subordinate values. Vortex v0.1 aggregate types are fixed
  arrays and structs.

argument
: An expression supplied by a call for one function parameter.

array
: A homogeneous value with a fixed element type and compile-time shape. See
  [Arrays and shapes](arrays.md).

associativity
: The rule that determines grouping when operators at the same precedence
  level repeat. Most binary Vortex operators group left; prefix unary
  operators group right. Equality, comparison and range operators do not
  associate: repeating one without parentheses is a syntax error
  ([why](../decisions/operators.md#d37)).

binding
: The association between a name and a declaration or storage location.

block
: A brace-delimited ordered sequence of statements that creates a local scope.
  A function's body block shares one scope with the function's parameters; see
  [Scopes](declarations.md#36-scopes).

borrow
: The part of a program during which a reference made by `&` or `&mut` is
  live: to the end of the enclosing block for a `let` binding, or for the whole
  call for an argument. See
  [References and mutability 9.8](references.md#98-aliasing).

built-in function
: A function the language provides without a declaration. Vortex v0.1 has one,
  `print`; see [Programs and declarations](declarations.md#39-built-in-functions).

cast
: An expression that converts one value to a numeric type, written as a
  numeric type keyword and one parenthesized operand, such as `f32(count)`.
  See [Casts and conversions](types-and-values.md#412-casts-and-conversions).

checked operation
: An operation that must detect an invalid case, such as integer overflow,
  division by zero or an out-of-bounds index, and fail with a diagnostic
  instead of producing a wrong value. See
  [Expressions 5.5](expressions.md#checked-integer-operations) and
  [Arrays and shapes 7.6](arrays.md#76-indexing).

compatible
: Describes two types that are equal after literal typing. Vortex v0.1 has no
  implicit conversions, so wherever a rule requires compatible types they must
  be the same type. See [Type equality](types-and-values.md#411-type-equality)
  and the [decision record](../decisions/numbers.md#d30).

compile time
: The period in which the compiler translates and validates a program before
  that program executes.

constant evaluation
: Evaluation performed by the compiler before the program runs: of fixed array
  dimensions, and of every checked operation whose deciding operands are
  integer constant expressions, where a failure is a constant-evaluation
  error. See [Expressions 5.12](expressions.md#512-constant-expressions).

constant expression
: In v0.1, an integer constant expression: integer literals, each optionally
  preceded by unary `-`, combined with parentheses and the binary operators
  `+`, `-`, `*`, `/` and `%`. Names and calls are never constant expressions.
  See [Expressions](expressions.md#512-constant-expressions).

deciding operand
: An operand whose value decides whether a checked operation fails: the
  divisor for division by zero, the count for a shift, the index for bounds,
  the operand for a cast, and every operand for overflow. See
  [Expressions 5.12](expressions.md#512-constant-expressions).

declaration
: A construct that introduces a name or definition. Top-level Vortex
  declarations are functions and structs; locals are declaration statements.

default type
: The type a numeric literal has when its context gives it no target type:
  `i32` for an integer literal and `f32` for a floating literal. See
  [Literal typing](types-and-values.md#literal-typing).

dimension
: One extent in an array shape. It is written as an integer constant
  expression and evaluates to a value of at least 1.

dynamic rule
: A requirement involving values that may only be known during execution.

element type
: The single type shared by every element of an array.

exit status
: The number a finished program hands back to whatever started it. A Vortex
  program exits with 0 when `main` returns and with 101 after a runtime error;
  see [Diagnostics](diagnostics.md#106-runtime-reporting).

expression
: A construct that computes, constructs, or refers to a value. See
  [Expressions](expressions.md).

field
: A named component declared by a struct.

fragment
: An example block that shows a type, an expression or a partial construct for
  its syntax. A fragment is never compiled and has no result. See
  [Specification examples](conformance.md#18-specification-examples).

grammar
: The formal rules describing which token sequences form valid source
  constructs. See the [formal grammar](grammar.md).

identifier
: A programmer-defined, case-sensitive name with the spelling rules in
  [Lexical structure](lexical-structure.md).

ill-formed
: Describes source that violates at least one required language rule and must
  be rejected.

implementation-defined
: Describes behavior for which the specification permits a choice and requires
  the implementation to document that choice. Every such behavior, and every
  implementation limit, is listed in
  [Implementation-defined behavior and limits](conformance.md#110-implementation-defined-behavior-and-limits).

implicit dereference
: The rule that a name of type `&T` or `&mut T` denotes its referent: used as
  a value, the name reads the referent, and as the root of an assignment
  target, it writes the referent. Vortex v0.1 has no dereference operator. See
  [Using a reference](references.md#using-a-reference).

initializer
: The expression that supplies the starting value of a local binding or one
  named field value in struct construction.

label
: The comment on the first line of every `vortex` example block, such as
  `// statements: type error`, that names what the block contains and what a
  conforming implementation must do with it. See
  [Specification examples](conformance.md#18-specification-examples).

lexing
: Converting source characters into tokens.

lifetime
: The interval during which a value or storage location exists and may be
  accessed safely.

literal typing
: The rule that gives an integer or floating literal its type from a peer
  operand, an expected type, or its default type. See
  [Literal typing](types-and-values.md#literal-typing).

lowering
: Translating a validated representation into a lower-level intermediate or
  target representation.

mutable
: Describes a place that may be changed: its root is a variable declared with
  `let mut`, or a name of type `&mut T`. Parameters are immutable. See
  [References and mutability 9.3](references.md#93-local-mutability).

name resolution
: Connecting each name use to one visible declaration and diagnosing unknown,
  duplicate, shadowing, or out-of-scope names.

named type
: A type written as an identifier and preserved until name resolution selects
  its declaration.

namespace
: A set of names in which each name has one meaning. All top-level functions
  and structs share one namespace, and each struct's field names form a
  separate one. See [Scopes](declarations.md#36-scopes).

normative
: Required for language conformance. Informative explanations help readers but
  do not create additional requirements. The specification chapters are
  normative; see [Document authority](conformance.md#11-document-authority).

parameter
: A named, typed input declared in a function signature.

parsing
: Checking the grammatical structure of a token stream and building the AST.

place
: An expression that names storage: a name followed by zero or more index and
  field suffixes, such as `points[index].x`. The name is the place's root.
  Assignment targets and the operands of `&` and `&mut` are places. See
  [References and mutability 9.3](references.md#93-local-mutability).

planned
: Describes syntax, behavior or an example that is not part of v0.1 but shows
  a direction for a later version. A v0.1 implementation must not accept
  planned syntax. See
  [Specification examples](conformance.md#18-specification-examples) and
  [record 50](../decisions/documentation.md#d50).

precedence
: The rule that determines which operator groups more tightly when parentheses
  do not state the grouping explicitly.

primitive type
: One built-in type named directly by the language, such as `i32`, `f32`, or
  `bool`.

range
: An expression `start..end`, which excludes `end`, or `start..=end`, which
  includes it. In v0.1 a range may appear only as the iterable of a `for`
  statement; it is not a value. See [Expressions 5.8](expressions.md#58-ranges).

rank
: The number of dimensions in an array type.

reference
: A safe connection to existing storage, written `&T` or `&mut T`. See
  [References and mutability](references.md).

referent
: The storage a reference refers to. Using a reference's name reads or writes
  its referent. See
  [References and mutability 9.4](references.md#94-shared-references).

reserved word
: A spelling set aside for a future version, such as `i64` or `const`. It has
  no meaning in v0.1, and using it outside a comment or literal is a lexical
  error. See [Lexical structure](lexical-structure.md#24-keywords) and
  [decision 29](../decisions/lexical.md#d29).

row-major order
: The array layout in which the last index varies fastest: a `[T; 2, 2]` is
  stored as `[0, 0]`, `[0, 1]`, `[1, 0]`, `[1, 1]`. Every v0.1 array uses it.
  See [Arrays and shapes](arrays.md#78-memory-and-layout).

run time
: The period in which a compiled Vortex program executes.

scope
: The source region in which a declaration's name is visible.

semantic analysis
: Static checks that require context beyond grammar, including mutability,
  control-flow placement, and entry-point validation.

shadowing
: Declaring a name while another declaration of the same name is visible, so
  that the new one would hide the old one. Vortex v0.1 forbids it; see
  [Scopes](declarations.md#36-scopes).

shape
: The ordered list of fixed extents for an array. Shape is part of an array
  type in v0.1.

source span
: A byte offset from the start of the source file and a length in bytes,
  identifying the source text of a token, AST node, or diagnostic. Diagnostics
  show the start as a line and column, both counted from 1; see
  [Conformance 1.7](conformance.md#17-source-locations).

standard error
: The output stream a program uses for error reports, kept separate from
  standard output. A runtime error is reported there.

standard output
: The output stream for a program's normal results. `print` writes there.

statement
: A construct that performs an action or changes control flow. See
  [Statements](statements.md).

static error
: The example-label group for code that parses but must be rejected before it
  runs: a name, type, semantic or constant-evaluation error. It is not a
  diagnostic category; each example names its exact category. See
  [Specification examples](conformance.md#18-specification-examples) and
  [record 51](../decisions/documentation.md#d51).

static rule
: A rule that a conforming implementation can check before program execution.

struct
: A nominal, named value type containing ordered named fields.

terminating statement
: A statement after which execution cannot continue with the next statement:
  a `return`, a non-empty block whose last statement terminates, or an `if`
  with an `else` whose branches both terminate. Loops never terminate. See
  [Statements 6.10](statements.md#610-return).

token
: One lexical unit with a kind, source spelling, and source span.

type checking
: Determining expression types and validating that operations, calls,
  assignments, and returns use compatible types.

undefined behavior
: Behavior for which a language imposes no requirements. Vortex v0.1 does not
  intentionally expose undefined behavior to well-formed safe programs.

Unicode scalar value
: A Unicode code point other than a surrogate: U+0000 to U+D7FF or U+E000 to
  U+10FFFF. Each source character and each `char` value is one Unicode scalar
  value. See [Lexical structure](lexical-structure.md#source-encoding) and
  [decision 15](../decisions/lexical.md#d15).

value
: The result of evaluating an expression. A value has one Vortex type.

warning
: A diagnostic that points out a likely mistake without rejecting the program.
  Warnings are optional and never change whether a program is accepted. See
  [Diagnostics 10.1](diagnostics.md#101-required-diagnostic-data).

well-formed
: Describes a program that satisfies every applicable v0.1 rule.

## Reference sources

The language-specific rules in this glossary are defined by the linked Vortex
chapters. The following external references provide background terminology but
do not override Vortex rules:

- [LLVM Language Reference](https://llvm.org/docs/LangRef.html) for
  intermediate-representation and lowering terminology.
- [IEEE 754 overview](https://standards.ieee.org/ieee/754/6210/) for the
  floating-point model referenced by v0.1.
- [C++ object model reference](https://en.cppreference.com/w/cpp/language/object)
  for concepts encountered in the compiler implementation.

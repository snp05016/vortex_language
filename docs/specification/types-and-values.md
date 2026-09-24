# 4. Types and values

Every Vortex expression has a type known before executable code is produced,
except a range, which is not a value in v0.1
([Expressions 5.8](expressions.md#58-ranges)). Types determine valid
operations, storage layout, calling behavior, and the checks required by later
compiler stages.

## 4.1 Type grammar

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

Named types are preserved as identifiers until name resolution. The parser
must not assume that an identifier names a struct merely because structs are
the only user-defined type form in v0.1.

## 4.2 Primitive types

| Type | Meaning | Default literal source |
| --- | --- | --- |
| `void` | No returned value | None |
| `bool` | `true` or `false` | Boolean literal |
| `char` | One Unicode scalar value | Character literal |
| `i32` | Signed 32-bit integer | Integer literal with no target type |
| `u32` | Unsigned 32-bit integer | Context only |
| `usize` | Unsigned size or index; implementation-defined width, 64 bits on every v0.1 target | Context only |
| `f32` | 32-bit floating-point number | Floating literal with no target type |
| `f64` | 64-bit floating-point number | Context only |
| `String` | UTF-8 text value | String literal |

`void` may appear only as a function's return type, written as `-> void` or
implied by omitting the return type. Using it anywhere else, such as a
parameter type, a `let` annotation, a struct field, an array element or a
referenced type, is a type error. `void` has no values: a call to a `void`
function may appear only as the whole expression of an expression statement,
optionally in parentheses. Anywhere else, such as `let x = f();`,
`return f();` or `g(f())`, it is a type error.
([Why](../decisions/operators.md#d44))

The operators each kind of operand accepts:

| Operand type | `+ - * /` | `%` | `& | ^ ~ << >>` | `== !=` | `< <= > >=` | `! && ||` |
| --- | --- | --- | --- | --- | --- | --- |
| `bool` | no | no | no | yes | no | yes |
| `char` | no | no | no | yes | no | no |
| `i32`, `u32`, `usize` | yes | yes | yes | yes | yes | no |
| `f32`, `f64` | yes | no | no | yes | yes | no |
| `String`, arrays, structs | no | no | no | no | no | no |

Both operands of a binary operator must have the same type, except the count
of a shift ([Expressions](expressions.md#55-arithmetic-and-bitwise-expressions)).
Unary `-` accepts `i32`, `f32` and `f64`, and unary `+` any numeric type. An
operand of reference type reads the value it refers to.
([Why](../decisions/operators.md#d35))

### Literal typing {#literal-typing}

An integer or floating literal has no type of its own; its context gives it
one. An expression built only from literals, parentheses, and unary and binary
operators is **literal-only**: `2 * (3 + 1)` is literal-only, while
`count * 2` and `f64(1)` are not. A literal-only expression takes a
**target type** from the first of these rules that applies:

1. **Peer.** It is one operand of an arithmetic (`+ - * / %`), bitwise
   (`& | ^`), equality or comparison operator, or one endpoint of a range, and
   the other operand is not literal-only. The target type is the other
   operand's type. A shift count is not an operand for this rule.
2. **Expected type.** Its position expects a type: the written type of a
   `let`; the type of an assignment or compound-assignment target; the
   parameter type, for a call argument; the function's result type, for a
   `return` value; the element type, for an element or the repeated value of
   an array expression whose array type is expected; the field type, for a
   field value in a struct expression; `usize`, for an array dimension.
3. Otherwise it has no target type. The operand of a cast, an index, a shift
   count and an argument to `print` have no expected type.

The target type passes down through parentheses, unary `+`, `-` and `~`, both
operands of a binary arithmetic or bitwise operator, and the left operand of a
shift, until it reaches each literal. A literal takes the target type when
that type is of its own kind: an integer type for an integer literal, a
floating-point type for a floating literal. Any other literal takes its
**default type**: `i32` for an integer literal and `f32` for a floating
literal. The ordinary type rules then check the result, so
`let ratio: f32 = 1;` is a type error.

After a literal has its type, its value must be representable in that type
([4.3](#43-integers), [4.4](#44-floating-point-values)).
[Decision record](../decisions/numbers.md#d31).

## 4.3 Integers

`i32` represents signed values from `-2^31` through `2^31 - 1`. `u32`
represents values from `0` through `2^32 - 1`. `usize` represents nonnegative
sizes and indices. Its width in bits is implementation-defined
([Conformance 1.6](conformance.md#16-implementation-defined-behavior)); every
v0.1 target uses 64 bits, so there `usize` holds values from `0` through
`2^64 - 1`. [Decision record](../decisions/numbers.md#d42).

An integer literal has the type that [literal typing](#literal-typing) gives
it, `i32` by default. Its value must be representable in that type; otherwise
the program has a type error at the literal. This includes values larger than
any fixed-width integer, which the lexer accepts as ordinary tokens. When a
unary `-` is written directly before an integer literal, the negated value is
checked instead: `-2147483648` is a valid `i32`, while `2147483648` on its own
is not. Unary `-` remains a separate operator in the syntax tree.
[Decision record](../decisions/numbers.md#d32).

Integer arithmetic, bitwise operations, shifts, remainder, and comparisons are
available only where the operand types support them ([4.2](#42-primitive-types)).
Vortex v0.1 has no implicit conversions between types. Only
[literal typing](#literal-typing) chooses a type without a cast, and only a
[cast](#412-casts-and-conversions) changes the type of a value
([decision record](../decisions/numbers.md#d30)).

## 4.4 Floating-point values

A floating literal has the type that [literal typing](#literal-typing) gives
it, `f32` by default. Its exact decimal value is rounded once, to the nearest
value of that type with ties to even; it is never rounded to another format
first. A finite literal that rounds to an infinity is a type error at the
literal. A literal too small for the type becomes a subnormal value (a tiny
value stored with reduced precision) or zero and is valid. A cast operand has
no expected type, so `f64(0.1)` converts the `f32` nearest to 0.1, while
`let x: f64 = 0.1;` gives the `f64` nearest to 0.1.
[Decision record](../decisions/numbers.md#d32).

Floating-point values support arithmetic and compatible comparisons.
Remainder, bitwise, and shift operations are not defined for floating-point
values in v0.1.

`f32` is the IEEE 754 binary32 format and `f64` is binary64. Each
floating-point operation (unary `-`; binary `+`, `-`, `*` and `/`; and each
cast to a floating-point type) must produce the IEEE 754 result rounded to
nearest with ties to even, in the operation's type. An implementation must not
contract operations (for example, fuse a multiplication and an addition into
one fused multiply-add, which rounds once), reassociate or reorder them,
evaluate them in a wider format, or flush subnormal inputs or results to zero.
The same holds for any floating-point value computed during compilation.
Relaxed floating-point modes may be added in a future version only as an
explicit opt-in. [Decision record](../decisions/numbers.md#d56).

Floating-point arithmetic never causes a runtime error. A nonzero finite value
divided by zero gives an infinity with the sign IEEE 754 specifies,
`0.0 / 0.0` gives NaN (not a number), overflow gives an infinity, and
operations on NaN and infinities follow IEEE 754. NaN is unordered: `==` with
a NaN operand is `false`, `!=` is `true`, and `<`, `<=`, `>` and `>=` are
`false`, so a NaN is not equal to itself. A cast of NaN or an infinity to an
integer type is a runtime error ([4.12](#412-casts-and-conversions)). `print`
writes NaN and the infinities as `NaN`, `inf` and `-inf`.
[Decision record](../decisions/numbers.md#d24).

## 4.5 Boolean values

`bool` has exactly two values, `true` and `false`. Vortex does not implicitly
interpret integers, pointers, strings, or collections as booleans. Conditions
for `if` and `while` must have type `bool`.

Logical `&&` and `||` short-circuit from left to right. `!` computes logical
negation.

## 4.6 Characters and strings

A `char` stores one Unicode scalar value: a Unicode code point other than a
surrogate (U+D800 to U+DFFF), as defined in
[Lexical structure 2.1](lexical-structure.md#source-encoding). Its storage
representation is an implementation detail, but it is not defined as a
one-byte value ([decision 15](../decisions/lexical.md#d15)).

A `String` stores UTF-8 text. Vortex v0.1 specifies string literals and
passing a `String` to the built-in `print` function, which writes its bytes
unchanged ([Programs and declarations 3.9](declarations.md#39-built-in-functions)).
String mutation, indexing, interpolation, concatenation, searching, and
numeric parsing are not yet specified.

## 4.7 Arrays

An array type has one element type and one or more dimensions. Each dimension
is an integer constant expression whose value is at least 1
([Arrays and shapes, 7.2](arrays.md#72-dimension-rules);
[decision 11](../decisions/arrays.md#d11)). Dimensions are part of the type.

```vortex
// fragment
[f32; 4]
[f32; 4, 4]
[[i32; 2]; 3]
```

See [Arrays and shapes](arrays.md) for construction, indexing, and dimension
rules.

## 4.8 Struct values

A struct declaration introduces a named value type with ordered named fields.
Struct values are constructed by naming each field. Vortex v0.1 defines no
inheritance or object identity model.

See [Structs](structs.md) for full rules.

## 4.9 References

`&T` is a shared reference to an existing `T`. `&mut T` is a mutable
reference, which also permits writing the referent (the storage the reference
refers to). A reference type may appear only as the type of a parameter or of
a `let` binding declared without `mut`. It is not a storable value type
anywhere else: using it as a return type, a struct field, an array element, or
the referenced type of another reference is a type error. The borrow rules
are in [References and mutability](references.md) 9.7 and 9.8
([record 41](../decisions/references.md#d41)).

References are not raw integer addresses and do not permit pointer arithmetic.
See [References and mutability](references.md). A name of reference type used
as a value reads the referent; there is no dereference operator
([References and mutability](references.md) 9.4;
[record 40](../decisions/references.md#d40)).

## 4.10 Type inference

Local variable type inference uses the initializer:

```vortex
// statements: valid
let count = 10;       // i32
let weight = 0.5;     // f32
let ready = false;    // bool
let name = "Vortex"; // String
```

Inference does not make the language dynamically typed. Once inferred, the
variable has one fixed type. Function parameters, struct fields, and explicit
function return types are written in source.

An element-list array with no expected type takes its type from its elements,
so a list of lists has a nested array type: `let grid = [[1, 2], [3, 4]];`
gives `grid` the type `[[i32; 2]; 2]`. With an expected multidimensional type,
the same literal fills that type instead
([Arrays and shapes, 7.3](arrays.md#73-element-list-construction)).

An empty array expression is excluded from v0.1 because local inference has no
element value from which to determine its element type.

## 4.11 Type equality

Primitive types are equal when their primitive kinds match. Named types are
equal when name resolution identifies the same declaration. Reference types
include mutability and referenced type. Array types include element type,
rank, and the value of every dimension, which the compiler evaluates when it
resolves the array type.

For example, `[f32; 2 + 2]` and `[f32; 4]` are the same type, while
`[f32; 4]` and `[f32; 2, 2]` are not
([decision 52](../decisions/arrays.md#d52)). A nested array type such as
`[[f32; 2]; 2]` and the multidimensional type `[f32; 2, 2]` are different
types, and neither converts to the other
([decision 21](../decisions/arrays.md#d21)).

Two types are **compatible** when they are equal under this section. Wherever
this specification requires compatible types, for operands, initializers,
assignment values, arguments, return values, array elements or struct field
values, the types must be equal after [literal typing](#literal-typing);
there is no implicit conversion. A rule that accepts different types says so
explicitly, as the count of a shift and an array index do.
[Decision record](../decisions/numbers.md#d30).

## 4.12 Casts and conversions

A cast converts one value to a numeric type. It is written as a numeric type
keyword followed by exactly one parenthesized operand:

```ebnf
cast_expression ::=
    numeric_type, "(", expression, ")" ;

numeric_type ::=
      "i32"
    | "u32"
    | "usize"
    | "f32"
    | "f64" ;
```

```vortex
// statements: valid
let count: i32 = 10;
let value: f32 = f32(count);
```

Because the numeric type names are keywords, the parser recognizes a cast from
its first token. A cast is not a call, and name resolution never looks up its
type name. `void`, `bool`, `char` and `String` cannot begin an expression, so
`bool(flag)` is a syntax error. [Decision record](../decisions/numbers.md#d1).

The operand must have type `i32`, `u32`, `usize`, `f32` or `f64`; any other
operand type is a type error. There are no casts to or from `bool`, `char`,
`String`, arrays or structs. The operand has no expected type, so a literal
operand takes its default type ([literal typing](#literal-typing)). A cast
never wraps or saturates:

| Operand type | Target type | Result |
| --- | --- | --- |
| integer | integer | The same value. If the target type cannot represent it, a runtime error. |
| integer | floating-point | The nearest value of the target type, ties to even. |
| `f32` | `f64` | The same value. |
| `f64` | `f32` | The nearest `f32` value, ties to even. A finite value too large for `f32` becomes an infinity of the same sign; NaN and infinities keep their kind. |
| floating-point | integer | The value truncated toward zero. If the operand is NaN or an infinity, or the target type cannot represent the truncated value, a runtime error. |
| any numeric type | the same type | The operand, unchanged. |

When the operand is an integer constant expression
([Expressions 5.12](expressions.md#512-constant-expressions)), the cast is
evaluated during compilation and a failure is a constant-evaluation error
([decision record 39](../decisions/diagnostics.md#d39)); `u32(-1)` is
therefore rejected before the program runs.
[Decision record](../decisions/numbers.md#d27).

## 4.13 Types outside v0.1

Tuples, unions, enums, vectors, slices, runtime-sized arrays, function types,
raw pointers, generic types, aliases, `i8`, `i16`, `i64`, `u8`, `u16`, `u64`,
`f16`, and `bf16` are not v0.1 types. The spellings of these eight numeric
types, and `const`, are reserved for a future version
([Lexical structure 2.4](lexical-structure.md#24-keywords)), so a v0.1 program
cannot use them as names either ([decision 29](../decisions/lexical.md#d29)).

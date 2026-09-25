# Expressions

## Learning goals

After this chapter, you should be able to identify every v0.1 expression form,
separate unary from binary operators, read precedence, and explain which stage
checks an expression.

An expression produces a value. A statement performs an action. Vortex programs
use both of them together.

An expression is any piece of code that produces a value. The value can be a
number, a boolean, a string, an array, or another Vortex type.

For example, every line below contains an expression:

```vortex
// fragment
42
width
2 + 3
square(4.0)
values[index]
x > y
```

--8<-- "includes/remember/language-tour__08-expressions.md"

## Expression quick reference

| Form | Syntax example | Result |
| --- | --- | --- |
| Literal | `42`, `true`, `"text"` | The literal's value. |
| Name | `width` | The value bound to a visible name. |
| Unary | `-value`, `!ready`, `&mut item` | One operation applied to one operand. |
| Binary | `left + right`, `a && b` | One operation applied to two operands. |
| Call | `square(4.0)` | A function result. |
| Cast | `f32(count)` | The value converted to a numeric type. |
| Index/field | `values[index]`, `point.x` | One part of a compound value. |
| Array/struct | `[1, 2]`, `Point { x: 1.0, y: 2.0 }` | A newly constructed value. |
| Range | `0..10`, `0..=10` | The values a `for` loop visits; allowed only after `in`. |
| Group | `(left + right)` | The enclosed expression with explicit precedence. |

Assignment (`=`) is a statement operator, not a binary expression. A complete
expression also does not end in `;`; the containing statement supplies it.

## Literal expressions

A literal is a value written directly in the program:

```vortex
// fragment
42                  // i32
3.14                // f32
true                // bool
'V'                 // char
"hello from Vortex" // String
[1, 2, 3, 4]        // array
```

The scalar literal forms are integer, floating-point, boolean, character, and
string literals. An array is a constructed expression, even though programmers
often call it an array literal informally.

The types in the comments above are the defaults. A number takes another type
when its surroundings ask for one: in `let size: usize = 42;` the literal is a
`usize`, and in `count + 1` it takes the type of `count`. A whole number never
becomes a float, so write `1.0`, not `1`, where an `f32` is expected.
([Literal typing](../specification/types-and-values.md#literal-typing).)

Malformed delimiters, unsupported escapes, empty character literals, and
unsupported literal spellings are invalid:

```vortex
// statements: lexical error
let pair = 'AB';
let mask = 0xFF;   // hexadecimal integer literals are not in v0.1
let octal = 010;   // leading zeros are not allowed
let big = 1e5;     // an exponent needs a decimal point: write 1.0e5
let text = "unterminated;
```

The lexer (the [compiler stage](../compiler/guide/index.md) that splits source
text into tokens) reads a number-like run such as `0xFF` or `1e5` as one piece
and reports one error for the whole piece
([decision 17](../decisions/lexical.md#d17)).

The lexer recognizes scalar literals.
The parser (the stage that checks how tokens fit together) wraps them in
expression nodes, and type checking determines or verifies their Vortex types.

??? check "In a call `square(4)` where `square` takes an `f32` parameter, what type does the literal `4` have?"

    `f32`, not the default `i32`. An integer literal takes whatever type its
    surroundings ask for; here that is the parameter's declared type.

## Name expressions

Writing the name of a variable reads its value:

```vortex
// statements: valid
let width = 128;
let area = width * width;
```

Here, both uses of `width` are expressions.

Using an undeclared or out-of-scope name is invalid. The parser can still build
a name expression; name resolution is the stage that reports the missing
declaration.

## Unary expressions

A unary expression applies one prefix operator to one operand:

| Operator | Allowed operand | Example |
| --- | --- | --- |
| `+` | Numeric | `+value` |
| `-` | Signed integer or floating point | `-value` |
| `!` | Boolean | `!finished` |
| `~` | Integer | `~flags` |
| `&` | A place: a variable, element or field | `&value` |
| `&mut` | A mutable place | `&mut value` |

There is no `*` operator: a reference's name reads and writes the value it
refers to ([decision](../decisions/references.md#d40)).

The operand is itself an expression, so nesting is allowed:

```vortex
// fragment
let negative = -(left + right);
let not_ready = !is_ready;
```

Invalid examples:

```vortex
// statements: type error
let bad = !42;       // ! requires bool
let bits = ~3.14;    // ~ requires an integer
```

```vortex
// statements: semantic error
let value = 10;
let bad_ref = &mut value; // value is not mutable
```

The parser records the operator and operand. Type checking validates the
operand. Reference analysis additionally checks addressability, mutability, and
where the reference appears: `&value` and `&mut value` may be written only as a
whole call argument or as the initializer of a `let` without `mut`
([decision](../decisions/references.md#d41)).

??? check "Can a reference expression such as `&value` be stored in a struct field?"

    No. A reference expression may appear only as a whole call argument or as
    the complete initializer of a `let` without `mut`. Anywhere else, such as
    a struct field value, it is a type error.

## Arithmetic expressions

Vortex supports the usual arithmetic operators:

| Operator | Meaning | Example |
| --- | --- | --- |
| `+` | addition | `a + b` |
| `-` | subtraction | `a - b` |
| `*` | multiplication | `a * b` |
| `/` | division | `a / b` |
| `%` | remainder | `a % b` |

The `+` and `-` operators can also be placed before one value:

```vortex
// statements: valid
let value = 5;
let positive = +value;
let negative = -value;
```

Both operands of an arithmetic operator must have the same type. Vortex never
converts a value to another type on its own, even when nothing would be lost:
with an `i32` `count`, `count * 2.5` is a type error, and `f32(count) * 2.5` is
the fix. ([Why](../decisions/numbers.md#d30).)

`%` requires integer operands. Integer division rounds toward zero, so
`-7 / 2` is -3, and `%` takes the sign of the left operand, so `-7 % 2` is -1
([why](../decisions/operators.md#d33)). Division by zero and integer overflow
follow the [runtime and numerical rules](06-runtime-and-numerical-rules.md);
the specification lists every checked operation in
[Expressions 5.5](../specification/expressions.md#checked-integer-operations).
The type checker rejects mismatched or nonnumeric operands; it does not treat
strings as numbers.

## Comparison expressions

Comparisons produce a `bool`:

| Operator | Meaning |
| --- | --- |
| `==` | equal |
| `!=` | not equal |
| `<` | less than |
| `<=` | less than or equal |
| `>` | greater than |
| `>=` | greater than or equal |

```vortex
// fragment
let same = left == right;
let in_bounds = index < length;
```

Both operands must have the same type. `==` and `!=` work on `bool`, `char`,
integers and floating-point numbers; `<`, `<=`, `>` and `>=` work on integers
and floating-point numbers only. Strings, arrays and structs cannot be compared
at all in v0.1, and the type checker rejects such comparisons. Floating-point
comparisons follow IEEE 754, so a NaN (not a number) value is not even equal to
itself ([why](../decisions/operators.md#d35)).

## Logical expressions

Logical operators work with boolean values:

| Operator | Meaning | Example |
| --- | --- | --- |
| `!` | not | `!finished` |
| `&&` | and | `ready && valid` |
| `\|\|` | or | `failed \|\| cancelled` |

```vortex
// fragment
let ready = is_ready && has_capacity;
let can_continue = ready || failed;
```

`&&` and `||` use short-circuit evaluation. This means Vortex stops as soon as
it knows the answer:

```vortex
// statements: valid
let values = [1.0, 2.0, 3.0, 4.0];
let index = 5;
let valid = index < 4 && values[index] > 0.0;
```

If the index is outside the array, the second part is not evaluated.

Both operands must be `bool`. `1 && 2` is invalid because Vortex has no implicit
integer-to-boolean conversion. Code generation preserves short-circuit order.

## Bitwise expressions

Bitwise operators work with the individual bits of integer values. They are
useful for low-level CPU and GPU work:

| Operator | Meaning |
| --- | --- |
| `&` | bitwise and |
| `\|` | bitwise or |
| `^` | bitwise exclusive or |
| `~` | bitwise not |
| `<<` | shift bits left |
| `>>` | shift bits right |

```vortex
// statements: valid
let flags = 0b10100110;
let value = 3;
let masked = flags & 0b1111;
let next_bit = value << 1;
```

Bitwise operators are different from the logical operators `&&`, `||`, and `!`.

Their operands must be integers of the same type, except for the shifts
described next. Floating-point, boolean, string, and struct operands are
invalid. The parser uses precedence to distinguish prefix reference `&value`
from infix bitwise `left & right`.

The count on the right of `<<` or `>>` may be any integer type, and the result
has the type of the value on the left. The count must be at least 0 and less
than the number of bits in that type, so `value << 32` on an `i32` is an error;
like integer overflow, any other count is a checked error
([record 34](../decisions/diagnostics.md#d34)). Bits that `<<` pushes out are
dropped, which is not overflow. `>>` keeps the sign of a signed value:
`-8 >> 1` is -4 ([why](../decisions/operators.md#d22)).

??? check "Is `mask << 32` valid when `mask` has type `u32`?"

    No. The rule applies to every integer type, not only `i32`: the shift
    count must be less than the bit width of the left operand's type, 32 for
    `u32` as well. A count of 32 or more is a checked error, the same kind of
    failure as integer overflow.

## Grouped expressions

Parentheses make the order of a calculation clear:

```vortex
// statements: valid
let first = 2 + 3 * 4;    // 14
let second = (2 + 3) * 4; // 20
```

Grouping decides which values each operation combines, but the parts are still
evaluated from left to right. In `first() + second() * third()`, the calls run
in the order `first`, `second`, `third`; then the multiplication happens, then
the addition
([Expressions 5.10](../specification/expressions.md#510-evaluation-order),
[decision 38](../decisions/statements.md#d38)).

## Function-call expressions

A function call is an expression when the function returns a value:

```vortex
// fragment
let result = square(4.0);
let larger = max(left, right);
```

A function returning `void` can still be called, but only as a statement of its
own, because the call produces no value:

```vortex
// statements: valid
print("starting");
```

The number and types of arguments must match the function parameters. These
are invalid:

- calling an undeclared name;
- passing the wrong number of arguments;
- calling a struct or a variable, since neither is a function;
- using a `void` call as a value: storing it, passing it as an argument, or
  returning it ([why](../decisions/operators.md#d44)).

The parser builds the call; name resolution finds the
callee; type checking validates arguments and the result.

## Array expressions

An array literal creates an array:

```vortex
// statements: valid
let values = [1.0, 2.0, 3.0, 4.0];
```

A repeat-array expression accepts one value expression followed by one or more
dimension expressions:

```vortex
// statements: valid
let row = [0.0; 2 + 2];
let matrix = [0.0; 2 * 2, 8 / 2];
```

Each dimension is parsed as a full expression, but it may contain only integer
literals, `+`, `-`, `*`, `/`, `%` and parentheses, and its value must be at
least 1 and fit `usize`. Names, calls, and noninteger dimensions are invalid
([decision](../decisions/arrays.md#d11)):

```vortex
// items: constant-evaluation error
fn make(size: usize) {
    let values = [0.0; size];
    // constant-evaluation error: size is a name
}
```

```vortex
// statements: constant-evaluation error
let bad = [0.0; 2.5]; // constant-evaluation error: 2.5 is not an integer
```

An explicit element list must contain at least one element, and all elements
must have the same type. Empty `[]` is not supported because it provides no
element type for local inference.

An indexing expression reads one element:

```vortex
// fragment
let first = values[0];
let current = values[index];
let cell = matrix[row, column];
```

An index may have any integer type: `i32`, `u32` or `usize`. Vortex checks that
each index is inside the array along its dimension, and a negative index is
always outside. When an index is made only of integer literals, such as
`values[3]`, the check happens during compilation and a bad index is a
constant-evaluation error; every other index is checked while the program runs
([decision](../decisions/arrays.md#d12)). The compiler must not reject a
program only because it can prove that a variable index will fail; that index
keeps its runtime check ([record 39](../decisions/diagnostics.md#d39)). When
the compiler can prove an index is safe, it may remove the unnecessary runtime
check in optimized code.

The number of indices must equal the array [rank](../specification/glossary.md)
(its number of dimensions), so `matrix[row]` on a two-dimensional array is a
type error. An array of arrays takes one pair of brackets per level, as in
`grid[1][0]` ([decision](../decisions/arrays.md#d47)).

## Struct expressions and field access

A struct expression creates a value, and a field-access expression reads part
of it:

```vortex
// fragment
let point = Point {
    x: 10.0,
    y: 20.0,
};

let horizontal = point.x;
```

A struct expression must provide every declared field exactly once, in any
order, with a value of the field's type. Unknown, duplicate, or missing fields
are type errors. A struct is not a function, so `Point(10.0, 20.0)` is a type
error too ([why](../decisions/operators.md#d7)). Field access requires a field
that exists on the value's resolved struct type.

## Range expressions

Ranges are used only by `for` loops:

```vortex
// fragment
0..10   // includes 0, but does not include 10
0..=10  // includes both 0 and 10
```

For example:

```vortex
// statements: valid
let values = [1.0, 2.0, 3.0, 4.0];
for index in 0..4 {
    print(values[index]);
}
```

Vortex v0.1 ranges have both endpoints; open-ended ranges such as `..10`, `0..`, and
`..` are not supported. A range can appear only after `in` in a `for` loop:
`let span = 0..10;` is a type error, because a range is not a value in v0.1.
Both endpoints must have the same integer type
([decision 36](../decisions/statements.md#d36)).

## Cast expressions

A cast converts a value to another numeric type. Write the type's name, then
the value in parentheses:

```vortex
// statements: valid
let count: i32 = 10;
let count_as_float = f32(count);

let temperature: f32 = 21.8;
let whole_degrees = i32(temperature);
```

Every cast follows one table
([the full rules](../specification/types-and-values.md#412-casts-and-conversions)):

- integer to integer keeps the value, and stops the program with a runtime
  error when the value does not fit, as in `u32(x)` when `x` is negative;
- integer to floating-point rounds to the nearest value the float can hold, so
  large integers may lose their last digits;
- `f32` to `f64` keeps the value exactly;
- `f64` to `f32` rounds to the nearest `f32`, and a value too large for `f32`
  becomes an infinity;
- floating-point to integer drops the fractional part (rounding toward zero),
  and stops the program when the value is NaN, infinite or out of range;
- casting a value to its own type changes nothing;
- `bool`, `char` and `String` values cannot be cast: `i32(true)` is a type
  error.

When the value being cast is made only of integer literals, such as `-1`, the
compiler does the conversion during compilation and reports a failure as a
constant-evaluation error before the program runs.

For example, this conversion is invalid because a negative value cannot fit in
an unsigned `u32`:

```vortex
// statements: constant-evaluation error
let value = u32(-1); // constant-evaluation error: -1 does not fit in u32
```

Vortex never wraps or clamps the value into a different number. Explicit
wrapping and saturating conversions may be added later as separately named
operations. ([Decision record](../decisions/numbers.md#d27).)

The value inside a cast gets no type from the cast, so a literal there keeps
its default type: `f64(0.1)` converts the `f32` nearest to 0.1, which is not
the `f64` nearest to 0.1. Write `let precise: f64 = 0.1;` instead.
([Why](../decisions/numbers.md#d31).)

Converting text into numbers is a different operation because the text may not
contain a valid number. A future version should use an operation such as
`i32::parse(text)` instead of treating text parsing as an ordinary cast.

Only the five numeric types `i32`, `u32`, `usize`, `f32` and `f64` can be cast
targets. A cast looks like a call, but the compiler tells them apart from the
first word: those type names are keywords, so `f32(` always starts a cast, and
`bool(flag)` is a syntax error.
([Why casts are written this way](../decisions/numbers.md#d1).)

??? check "In `i32(50000.0)`, does the literal `50000.0` first become `f64`?"

    No. A cast gives the value inside it no type, so the literal keeps its
    default type, `f32`; the cast then converts that `f32` to `i32`. Only an
    annotation such as `let x: f64 = 50000.0;` would make the literal `f64`.

## Operator precedence

Precedence decides how operators group when parentheses are not used.
Operators near the top of this table bind more tightly than operators near the
bottom, so `a + b * c` groups as `a + (b * c)`. Precedence does not decide the
order in which operands are evaluated: that is always left to right
([Evaluation order](../specification/expressions.md#510-evaluation-order),
[decision 54](../decisions/documentation.md#d54)).

| Level | Operators | Purpose |
| --- | --- | --- |
| 1 | `()` `[]` `.` | calls, indexing, and field access |
| 2 | unary `+` `-` `!` `~` `&` `&mut` | operations on one value, and taking a reference |
| 3 | `*` `/` `%` | multiplication, division, and remainder |
| 4 | `+` `-` | addition and subtraction |
| 5 | `<<` `>>` | bit shifts |
| 6 | `<` `<=` `>` `>=` | ordered comparisons |
| 7 | `==` `!=` | equality comparisons |
| 8 | `&` | bitwise and |
| 9 | `^` | bitwise exclusive or |
| 10 | `\|` | bitwise or |
| 11 | `&&` | logical and |
| 12 | `\|\|` | logical or |
| 13 | `..` `..=` | ranges (only in a `for` loop header) |

Comparisons do not chain. `0 <= index < length` is a syntax error; write
`0 <= index && index < length`. Mixing `==` with `<`, as in `a < b == c`, also
needs parentheses. Bitwise `&`, `^` and `|` come after the comparisons, so
`flags & mask == 0` means `flags & (mask == 0)` and is rejected; write
`(flags & mask) == 0` ([why](../decisions/operators.md#d37)).

When the intended order is not obvious, use parentheses. Clear code is more
important than memorizing this table.

## Expressions that can wait

The first version of Vortex does not need every possible expression. These can
be considered later:

- conditional expressions such as `condition ? yes : no`;
- `if` blocks that directly produce values;
- anonymous functions and closures;
- pattern matching;
- overloaded operators for user-created types;
- named constants (`const`), planned as the first addition after v0.1, and
  constant expressions that use names or calls (v0.1 evaluates only integer
  constant expressions made of literals);
- tensor expressions that operate on complete tensors at once.

## Compiler handling summary

<details markdown="1">
<summary>Which compiler stage enforces each rule (optional reading)</summary>

1. The lexer recognizes literal, identifier, delimiter, and operator tokens.
2. The parser applies precedence and builds expression [AST](../specification/glossary.md)
   (abstract syntax tree) nodes.
3. Name resolution binds names, calls, fields, and named types.
4. Type checking evaluates array dimensions while it reads array types
   ([decision](../decisions/arrays.md#d52)), validates operands, and assigns
   every expression a result type.
5. Constant evaluation checks every operation whose deciding operands are made
   only of integer literals, such as `10 / 0` or `values[3]`, and rejects those
   that fail ([decision](../decisions/diagnostics.md#d39)).
6. Code generation preserves evaluation order, short circuiting, and required
   runtime checks.

</details>

## Practice and self-check

Classify each line by its outermost expression form and decide whether it is
valid:

```vortex
// fragment
-(left + right)
values[index + 1]
[0.0; 2 + 2, rows]
ready && count
Point { x: 1.0, y: 2.0 }
```

Answers:

1. Valid unary expression if `left` and `right` have the same signed integer or
   floating-point type.
2. Valid index expression if `index + 1` is an integer. Whether the access is
   in bounds is checked when the program runs, because `index` is a name.
3. A repeat-array expression, always invalid in v0.1: `rows` is a name, and a
   dimension may use only integer literals (a constant-evaluation error).
4. Invalid unless `count` is `bool`.
5. Valid struct expression when `Point` declares exactly the fields `x` and
   `y`, both with a floating-point type.

## Key ideas

!!! recap

    - **What is the difference between an expression and a statement?** An
      expression produces a value; a statement performs an action. A complete
      expression never ends in `;`, its containing statement supplies that.
    - **Does a whole-number literal ever become a float on its own?** No.
      Write `1.0`, not `1`, where a float is expected; Vortex never converts a
      value to another type automatically.
    - **What is `-7 % 2`?** -1. Integer division truncates toward zero and
      `%` takes the sign of the left operand.
    - **What decides how operators group when there are no parentheses?**
      Precedence. It never changes evaluation order, which is always left to
      right.
    - **Which dimensions are allowed in a repeat-array expression, such as
      `[0.0; n]`?** Only integer constant expressions built from literals,
      `+`, `-`, `*`, `/`, `%` and parentheses; a name or a call is invalid.
    - **When is an out-of-range index caught during compilation instead of
      while the program runs?** Only when the index is made only of integer
      literals, such as `values[3]`; an index that is a name or expression
      with a name is checked at run time.
    - **What can a cast convert to?** Only one of the five numeric types
      `i32`, `u32`, `usize`, `f32` and `f64`; `bool`, `char` and `String` are
      never cast targets.

## Where this comes back

--8<-- "includes/next/language-tour__08-expressions.md"

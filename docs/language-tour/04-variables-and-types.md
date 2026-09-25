# Variables and types

## Learning goals

After this chapter, you should be able to declare mutable and immutable local
variables, choose a v0.1 type, read an array type, and explain how the compiler
validates dimension expressions and references.

--8<-- "includes/remember/language-tour__04-variables-and-types.md"

## Variable declaration syntax

```text
let [mut] name [: type] = expression;
```

The brackets explain optional pieces; they are not typed in the program. Every
v0.1 local variable needs an initializer expression. A type annotation is
optional when the compiler can infer the type from that initializer.

### Allowed

```vortex
// statements: valid
let width = 128;
let height: usize = 64;
let mut total: f32 = 0.0;
```

### Not allowed

```vortex
// statements: syntax error
let missing;              // no initializer
```

```vortex
// statements: type error
let count: i32 = "five"; // initializer has the wrong type
```

```vortex
// statements: semantic error
let width = 128;
width = 256;              // width was not declared mut
```

The parser (the [compiler stage](../compiler/guide/index.md) that checks how tokens fit together and builds the program's structure) checks that the declaration has the required pieces. Type checking
infers or verifies the type. Assignment checking later enforces mutability.

## Automatic type inference

Vortex infers local-variable types when you use `let` with a starting value.
This saves you from writing obvious types repeatedly, while the program remains
statically typed.

```vortex
// program: valid
fn add(a: i32, b: i32) -> i32 {
    return a + b;
}

fn main() {
    let x = 5;               // inferred as i32
    let result = add(5, 10); // inferred as i32
}
```

Vortex does not use `auto` for local variables. `let` is the one clear way to
ask for local type inference.

The compiler cannot infer a type when there is no starting value:

```vortex
// statements: syntax error
let x; // syntax error: no starting value
```

??? check "Why is `let x;` rejected before type checking ever runs?"

    Every local variable declaration needs an initializer expression, and
    `let x;` has none, so there is no expression for the compiler to read a
    type from. The parser rejects the declaration as a syntax error, so type
    checking never gets a chance to run on it.

Every value in Vortex has a type. A type tells the compiler what kind of value it
is working with and how that value can be used.

You can write the type yourself:

```vortex
// statements: valid
let width: i32 = 128; // i32 is here for a 32 bit signed integer
```

When the type is obvious, Vortex can figure it out for you:

```vortex
// statements: valid
let width = 128; // automatically inferred as i32
```

Variables cannot be changed by default. Write `mut` when a variable needs to
change:

```vortex
// statements: valid
let width = 128;
let mut total = 0.0; // mut makes the variable mutable; its type is still f32

total = total + 1.0;
```

Mutable variables are variables that can be changed after they are
created. Immutable variables cannot be changed after they are created.

## v0.1 type quick reference

| Type form | Example | What it represents |
| --- | --- | --- |
| Primitive | `i32`, `bool`, `String` | A built-in scalar or string type. |
| Fixed-size array | `[f32; 4, 4]` | One element type and compile-time dimensions. |
| Shared reference | `&Point` | Read access to an existing value. |
| Mutable reference | `&mut [f32; 4]` | Read and write access to an existing mutable value. |
| Named type | `Point` | A type introduced by a struct declaration. |

Type aliases, generic types, slices, vectors, tuples, and function types are
not part of v0.1. [Types planned for later](05-types-planned-for-later.md)
separates future ideas from accepted syntax.

## Integer types

Integers are whole numbers. Vortex v0.1 starts with these integer types:

```text
i32   u32   usize
```

`i32` can hold negative and positive whole numbers. `u32` starts at zero and
cannot hold negative numbers. Both use 32 bits.

```vortex
// statements: valid
let temperature: i32 = -10;
let image_width: u32 = 1920;
```

Vortex also has `usize` for array lengths and indexes. Its width is chosen by
the implementation, which must document it; on every v0.1 target it is 64
bits. ([Why](../decisions/numbers.md#d42).)

```vortex
// statements: valid
let index: usize = 4;
```

Invalid uses include:

- assigning a negative value to `u32` or `usize`;
- applying integer-only operations to strings;
- relying on an integer value outside its type's range.

For example:

```vortex
// statements: type error
let count: u32 = -1; // type error: unsigned integers cannot be negative
let name = "Vortex" % 2; // type error: remainder requires integers
```

A literal that is too large for its type is also a type error, for example
`let big: i32 = 3000000000;`. ([Why](../decisions/numbers.md#d32).)

??? check "In `let limit: u32 = 100; let next = limit - 1;`, what type does the literal `1` have?"

    `u32`. `1` is combined with `limit` by `-`, and `limit` is not itself a
    literal-only value, so `1` takes its type from that peer instead of
    falling back to the `i32` default.

The lexer (the [compiler stage](../compiler/guide/index.md) that splits source text into tokens) records the integer literal. Unary `-` is represented separately.
Type checking chooses or verifies `i32`, `u32`, or `usize` and checks whether
the value and operation are legal.

A whole-number literal takes its type from where it is used: the declared type
in `let height: usize = 64;`, the parameter it is passed to, or the other
operand in `count + 1`. When nothing decides, it is an `i32`.
([Literal typing](../specification/types-and-values.md#literal-typing).)

The smaller and larger integer types `i8`, `i16`, `i64`, `u8`, `u16`, and
`u64` can be added after v0.1. Their names are already reserved, so a v0.1
program cannot use them as names ([decision 29](../decisions/lexical.md#d29)).
Until `u8` is added, Vortex does not expose a separate raw-byte type.

## Floating-point types

Floating-point types store numbers with a decimal part:

```text
f32
f64
```

`f32` is smaller and is commonly used for high-performance numerical work.
`f64` uses more memory but can represent numbers with greater precision.

```vortex
// statements: valid
let weight: f32 = 0.5;
let precise_value: f64 = 0.123456789;
```

When a decimal number does not have an explicit type, Vortex treats it as an
`f32`. This default fits Vortex's focus on numerical work across CPUs and GPUs.
A whole-number literal never becomes a float: `let ratio: f32 = 1;` is a type
error, so write `1.0`. ([Why](../decisions/numbers.md#d31).)

Types such as `f16` and `bf16` (brain float, with 7 mantissa bits, used for
machine learning) may be added later for machine-learning and GPU workloads.
They are not part of the first version of the language. Their names are
reserved too.

The `%`, bitwise, and shift operators are not defined for `f32` or `f64` in
v0.1. Type checking rejects uses such as `3.0 & 1.0`.

## Boolean type

A `bool` is either `true` or `false`:

```vortex
// statements: valid
let finished: bool = false;
```

A boolean contains one bit of information, but Vortex does not promise that it
will use exactly one bit of memory. The compiler may store it differently when
that is better for the target hardware.

Only `true` and `false` are boolean literals. Vortex does not use integers as
implicit booleans, so `if 1 { ... }` is invalid. The type checker requires a
`bool` condition.

## Void

`void` means that a function does not return a value. Vortex uses this name
because it is familiar to C and C++ programmers.

```vortex
// items: valid
fn show_result(value: f32) -> void {
    print(value);
}
```

The `-> void` can be left out when a function has no return value:

```vortex
// items: valid
fn show_result(value: f32) {
    print(value);
}
```

`void` describes the absence of a returned value, and it can only be written as
a function's return type. A call to a `void` function can only stand alone as
a statement, as in `show_result(1.5);`. It cannot initialize a variable, be
passed as an argument, or be returned with `return`
([why](../decisions/operators.md#d44)).

## Characters

A `char` stores one Unicode character; more exactly, one Unicode scalar value,
the number Unicode gives a single character such as `A` or `λ`:

```vortex
// statements: valid
let grade: char = 'A';
let symbol: char = 'λ';
```

Character values use single quotes. A character is not always one byte, because
some Unicode characters need more than one byte when stored in memory. A raw
`u8` byte type can be added after v0.1.

A character literal contains exactly one scalar value or one supported escape,
so an accented letter typed as a plain letter followed by a separate combining
accent does not fit in a `char`, while the single precomposed letter (one
character that already includes the accent, such as `é`) does
([decision 15](../decisions/lexical.md#d15)). `'AB'` and `''` are invalid
character literals; the lexer reports them.

## Strings

A `String` stores a piece of text:

```vortex
// statements: valid
let message: String = "matrix multiplication started";
print(message);
```

String values use double quotes. Vortex strings use UTF-8, so they can contain
text from many languages:

```vortex
// statements: valid
let message = "Hello from Vortex!";
let status = "GPU kernel ready";
```

Strings are useful for normal output, error messages, and debugging information.
The built-in `print` function can print strings as well as basic values:

```vortex
// statements: valid
let rows: usize = 128;
let elapsed: f32 = 0.42;

print("starting matrix multiplication");
print("rows:", rows);
print("elapsed time:", elapsed);
```

`print` writes its arguments on one line, with one space between them, and
then ends the line. The three calls above write:

```text
starting matrix multiplication
rows: 128
elapsed time: 0.42
```

A call needs at least one argument, and each argument must be a number, a
`bool`, a `char` or a `String`; an array or a struct cannot be printed whole. A
floating-point value always prints with a decimal point, so `16.0` prints as
`16.0`, not `16`
([Programs and declarations 3.9](../specification/declarations.md#39-built-in-functions),
[decision](../decisions/program.md#d4)).

The first version of Vortex only needs simple string creation and printing.
More advanced operations, such as changing parts of a string or searching
inside it, can be added later.

String indexing, interpolation, mutation, concatenation rules, and parsing text
as a number are not defined in v0.1. Use string literals and `print` as
described in
[Programs and declarations 3.9](../specification/declarations.md#39-built-in-functions).

## Fixed-size arrays

An array stores a fixed number of values of the same type next to each other in
memory. Its length is known when the program is compiled.

The type `[f32; 4]` means "an array containing four `f32` values":

```vortex
// statements: valid
let values: [f32; 4] = [1.0, 2.0, 3.0, 4.0];
```

Vortex also supports fixed-size multidimensional arrays. Write each dimension
after the semicolon:

```vortex
// statements: valid
let matrix: [f32; 2, 2] = [
    [1.0, 2.0],
    [3.0, 4.0],
];

let value = matrix[1, 0];
```

The type `[f32; 2, 2]` means a two-row, two-column array of `f32` values. Every
dimension is part of the type and must be known during compilation. This gives
the compiler the [shape](../specification/glossary.md) information (the list of dimension sizes) it
needs for matrix operations.

The list of two lists works because the written type tells the compiler to
read each inner list as one row of type `[f32; 2]`. Without the annotation,
`[[1.0, 2.0], [3.0, 4.0]]` is an array of arrays, type `[[f32; 2]; 2]`, which
is a different type and never converts to `[f32; 2, 2]`
([decision](../decisions/arrays.md#d21)).

A multidimensional array is stored row by row: all of row 0, then all of row 1.
The last index changes fastest as you move through memory, so a loop that
varies the last index innermost reads neighboring values
([decision](../decisions/arrays.md#d43)).

### Dimension expressions

Each dimension position accepts a full Vortex expression, not only a number
written directly in the source:

```vortex
// statements: valid
let row: [f32; 2 + 2] = [0.0; 2 + 2];
let matrix: [f32; 2 * 2, 8 / 2] = [0.0; 2 * 2, 8 / 2];
```

Although the parser accepts an expression, the compiler must be able to
evaluate it during compilation. Its result must be a whole number of at least 1
that fits `usize`. The type `[f32; 2 + 2]` is therefore the same type as
`[f32; 4]`.

A dimension may contain only integer literals, the operators `+`, `-`, `*`, `/`
and `%`, and parentheses. It may not use a name, not even the name of an
immutable variable, and it may not call a function, because v0.1 has no named
constants ([decision](../decisions/arrays.md#d11)). Floating-point, boolean,
string, and negative dimensions are invalid too:

```vortex
// items: constant-evaluation error
fn make(size: usize) {
    let values: [f32; size] = [0.0; size];
    // constant-evaluation error: size is a name, not a constant
}
```

```vortex
// statements: constant-evaluation error
let fractional: [f32; 2.5] = [0.0; 2.5];
// constant-evaluation error: 2.5 is not an integer
```

A dimension must also be at least 1. `[f32; 0]` and `[0.0; 4 - 4]` are
constant-evaluation errors, because v0.1 has no empty arrays
([decision](../decisions/arrays.md#d10)).

The parser stores each dimension as an expression
[AST](../specification/glossary.md) (abstract syntax tree, the tree the parser builds from source). Type checking
evaluates each dimension when it reads the array type, then uses the values
when comparing array shapes ([decision](../decisions/arrays.md#d52)). Code
generation receives the final fixed layout rather than evaluating dimensions at
runtime.

??? check "Is `[f32; 4 / 0]` a valid array type?"

    No. A dimension is evaluated with checked `usize` arithmetic at compile
    time, and dividing by zero fails that check. The array type is a
    constant-evaluation error, the same category as a dimension that names a
    variable or holds a non-integer value.

## Repeat array expressions

Vortex also supports Rust-style repeat array expressions. Write one value, then
a semicolon, then the number of times it should appear:

```vortex
// statements: valid
let zeros = [0.0; 1024];
let flags = [false; 8];
let zero_matrix = [0.0; 4, 4];
```

The first example creates an array of 1,024 `f32` values, all set to `0.0`.
The second creates eight `bool` values, all set to `false`. The final example
creates a four-row, four-column array filled with `0.0`.

Repeat dimensions use the same expression rules as array-type dimensions. Each
one must evaluate during compilation to a whole number of at least 1 that fits
`usize`:

```vortex
// statements: valid
let values = [0.0; 8 + 8];
let tiles = [0.0; 2 * 2, 2 + 2];
```

The value expression before `;` is evaluated once, and its value is used to
fill the complete array. A dimension that uses a name, such as a parameter, is
not allowed:

```vortex
// items: constant-evaluation error
fn make(size: usize) {
    let values = [0.0; size];
    // constant-evaluation error: size is a name
}
```

Named compile-time constants are not part of v0.1. They are planned as the
first addition after v0.1. Use a future `Vector` when the number of values is
only known while the program is running.

## Structs

A struct groups related values into a new type:

```vortex
// program: valid
struct Point {
    x: f32,
    y: f32,
}

fn main() {
    let origin = Point {
        x: 0.0,
        y: 0.0,
    };
}
```

Vortex structs are simple value types. Inheritance and complicated class
systems are not part of the initial language.

All declared fields must be present exactly once when constructing a value.
Unknown, repeated, missing, or incorrectly typed fields are type errors. Struct
methods, inheritance, and anonymous structs are not supported.

## References

Vortex v0.1 has basic references so functions can use arrays, strings, and
structs without copying all their data.

A shared reference, written as `&T`, allows a function to read a value. A
mutable reference, written as `&mut T`, also allows the function to change it:

```vortex
// program: valid
fn scale(values: &mut [f32; 4], factor: f32) -> void {
    for index in 0..4 {
        values[index] *= factor;
    }
}

fn main() {
    let mut values = [1.0, 2.0, 3.0, 4.0];
    scale(&mut values, 2.0);
}
```

Inside `scale`, `values[index]` reaches the array that belongs to `main`. A
reference's name stands for the value it refers to: reading the name reads
that value, and assigning to it, or to one of its elements or fields, writes
that value, which needs a `&mut` reference. There is no `*` operator, and a
reference cannot later be pointed at different storage
([the decision](../decisions/references.md#d40)).

Vortex does not allow a value to be changed through a shared reference. Taking
a reference with `&` or `&mut` is called **borrowing** the variable. A borrow
made with `&mut` lasts until the end of the block that holds the reference, or
until the call returns when it is an argument; during that time the variable
can be used only through the reference. While a `&` borrow lasts, the variable
cannot be assigned or borrowed with `&mut`. References can be parameters and
`let` bindings without `mut`, but a function cannot return one and a struct or
array cannot store one. These rules prevent overlapping writes and dangling
references without a lifetime system
([the decision](../decisions/references.md#d41)).

The operand of `&` or `&mut` must be an addressable value such as a variable,
field, or array element. A mutable reference additionally requires mutable
storage:

```vortex
// statements: semantic error
let value = 10;
let shared = &value;      // valid: read-only reference
let changed = &mut value; // invalid: value was not declared mut
```

The parser records whether `mut` appears. [Semantic analysis](../specification/glossary.md) (the checks on meaning that run after
parsing) checks addressability, mutability, where references appear, and
conflicting access. A reference is a
safe connection to an existing value, not a raw integer memory address.

??? check "While `shared` borrows `value`, can `value` still be read directly, and can a second `&value` be taken?"

    Yes to both. A shared borrow only rules out assigning to the borrowed
    variable or borrowing it with `&mut`; reading the variable directly and
    taking further shared references are both still allowed.

## Practice and self-check

For each declaration, decide whether it is valid and, if it is not, name the
error category and the stage that reports it
([decision 51](../decisions/documentation.md#d51)):

```vortex
// fragment
let count: u32 = 4;
let grid: [f32; 2 + 2, 3] = [0.0; 4, 3];
let bad: [f32; 2.5] = [0.0; 2.5];
let name: String = 'V';
```

Answers:

1. Valid.
2. Valid; the type checker evaluates `2 + 2` to `4`.
3. Invalid: a constant-evaluation error, because `2.5` is not an integer
   literal, so it cannot be a dimension. Type checking reports it while it
   evaluates the dimensions of the array type.
4. Invalid: a type error, found by type checking, because `'V'` is a `char`,
   not a `String`.

## Key ideas

!!! recap "Questions you can now answer"

    - **What decides a local variable's type when you write `let width = 128;`
      with no annotation?** The initializer expression; here the literal's
      default type, `i32`, since nothing else decides it.
    - **Why must every array dimension be an integer constant expression,
      never a variable?** v0.1 has no named constants, and the compiler must
      evaluate every dimension before it can compare array types or lay out
      storage.
    - **What is the default type of a floating-point literal, and when does it
      change?** `f32`, unless a peer operand or an expected type (such as a
      `let`'s written type) gives it `f64` instead.
    - **Why can a call to a `void` function only stand alone as a
      statement?** `void` describes the absence of a value, so the call
      cannot appear anywhere a value is required, such as an initializer or
      an argument.
    - **What must be true of every field when constructing a struct value?**
      Every declared field must be present exactly once, with the right
      type; unknown, missing, repeated, or mistyped fields are type errors.
    - **What can a shared reference (`&T`) never do that a mutable reference
      (`&mut T`) can?** Write to its referent. Only a `&mut` reference lets
      the function change the value it refers to.

## Where this comes back

--8<-- "includes/next/language-tour__04-variables-and-types.md"

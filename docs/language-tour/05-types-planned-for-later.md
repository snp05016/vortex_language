# Types planned for later

--8<-- "includes/remember/language-tour__05-types-planned-for-later.md"

## Learning goals

After this chapter, you should be able to distinguish accepted v0.1 types from
future design sketches. Every code sample in this chapter is illustrative and
is **not valid v0.1 syntax** unless the text explicitly says otherwise.

## Current boundary

The first version of Vortex stays small. It supports primitive types,
fixed-size arrays, references, and named struct types. Slices, vectors, tensors,
matrix aliases, and additional numerical types are planned for later.

### What you can do in v0.1

```vortex
// statements: valid
let values: [f32; 4] = [1.0, 2.0, 3.0, 4.0];
let matrix: [f32; 2, 2] = [0.0; 2, 2];
```

### What you cannot do in v0.1

```vortex
// statements: planned
let values: Vector<f32> = Vector::new();
let view: &[f32] = &values;
let matrix: Tensor<f32, [128, 256]>;
```

These fail because the corresponding type and expression grammars do not yet
exist. They are not library calls that can be enabled with an import.

## Slices

A future slice would be a view into part of an array. Its length would be known
while the program runs rather than being part of its compile-time type.

```text
&[T]      proposed shared slice
&mut [T]  proposed mutable slice
```

The slice would view existing memory without owning or copying the values.
However, v0.1 array types always include dimensions, so `&[f32]` is currently
invalid. Use a reference to a fixed-size array such as `&[f32; 4]` instead.

??? check "Why is `&[f32]` invalid in v0.1, and what should you write instead?"

    A slice type would describe a view whose length is known only while the
    program runs, but every v0.1 array type carries its dimensions as part of
    the type. Write a reference to a fixed-size array instead, such as
    `&[f32; 4]`.

## Vectors

A future `Vector<T>` would own contiguous memory whose length can grow or
shrink while the program runs:

```vortex
// statements: planned
// proposed syntax, not valid v0.1
let values: Vector<f32> = Vector::new();
values.push(1.0);
values.push(2.0);
```

Unlike a fixed-size array, a vector would not require a compile-time length.
Vortex v0.1 has no generics, namespace operator `::`, methods, or vector allocation,
so every part of this example remains future work.

??? check "Besides vector allocation itself, name two other v0.1 gaps that `Vector<f32>::new()` and `values.push(1.0)` would each need filled."

    Any two of: generics (the `<f32>` type parameter), the namespace operator
    `::` (used to call `Vector::new()`), and methods (used to call
    `.push()`). None of the three exists in v0.1.

## Tensors and matrices

Vortex will eventually need a multidimensional tensor type for its main
numerical workloads. One possible design is:

```vortex
// statements: planned
// design sketch, not valid v0.1
let vector: Tensor<f32, [128]>;
let matrix: Tensor<f32, [128, 256]>;
let batch: Tensor<f32, [16, 128, 256]>;
```

`Matrix` may become a convenient name for a two-dimensional tensor:

```text
Matrix<T, M, N> might mean Tensor<T, [M, N]>
```

The exact tensor syntax has not been decided. Fixed-size multidimensional
arrays are the supported v0.1 replacement:

```vortex
// statements: valid
let matrix: [f32; 4, 4] = [0.0; 4, 4];
```

??? check "You need a 4-by-4 matrix of `f32` today, before `Tensor` or `Matrix` exist. What v0.1 type do you write?"

    `[f32; 4, 4]`, a fixed-size, two-dimensional array.

## Additional primitive types

Types such as `i8`, `i16`, `i64`, `u8`, `u16`, `u64`, `f16`, and `bf16` may be
added later. They are not aliases for current types. Their names are already
reserved: the lexer rejects them anywhere outside comments and literals in a
v0.1 program, even as the name of a struct or a variable, so adding these types
later cannot break an existing program. `const` is reserved the same way
([Keywords](../specification/lexical-structure.md#24-keywords),
[decision 29](../decisions/lexical.md#d29)).

??? check "A v0.1 program declares `struct i64 { ... }`. What happens, and why can giving `i64` a real meaning later never break a program that compiles today?"

    It is a lexical error: the lexer rejects `i64` as a reserved word before
    the parser or name resolution ever sees it, so no v0.1 program can compile
    today with `i64` as a name. Since no accepted program uses that name,
    defining it later changes no existing program's behavior.

## Compiler handling

<details markdown="1">
<summary>Which compiler stage enforces each rule (optional reading)</summary>

The parser (the [compiler stage](../compiler/guide/index.md) that checks how tokens fit together and builds the program's structure) may parse an unknown identifier in a type position as a named type.
Name resolution then rejects it when no matching struct exists. The reserved
names from the previous section, such as `i64` and `bf16`, never reach the
parser: the lexer rejects them first. Syntax such as
`Vector<f32>` fails earlier because generic type arguments are absent from the
v0.1 grammar. Clear diagnostics should say whether the failure is grammatical
or an unresolved type name.

</details>

## Practice and self-check

Classify each type as **v0.1** or **planned**:

```text
[f32; 4, 4]
&mut [f32; 8]
Vector<f32>
Tensor<f32, [8, 8]>
Point
```

The first, second, and fifth forms are v0.1, assuming `Point` was declared as a
struct. `Vector` and `Tensor` are planned.

## Key ideas

!!! recap

    - **Which types does v0.1 support?** Primitive types, fixed-size arrays,
      references, and named struct types.
    - **Why is `&[f32]` invalid, while `&[f32; 4]` is valid?** A slice, whose
      length is known only while the program runs, is not part of v0.1; every
      v0.1 array type must carry its dimensions.
    - **What three v0.1 gaps stand between today's grammar and
      `Vector<f32>::new()`?** Generics, the namespace operator `::`, and
      methods; v0.1 has none of the three.
    - **What is the v0.1 replacement for a future `Tensor<f32, [4, 4]>`?** A
      fixed-size multidimensional array, `[f32; 4, 4]`.
    - **What happens if a v0.1 program tries to name a struct or variable
      `i64`?** A lexical error. `i64`, like the other planned primitive types
      and `const`, is reserved before the parser or name resolution ever sees
      it.
    - **Why does reserving names like `i64` now protect programs written
      later?** Because no v0.1 program can compile with that name today,
      giving it a real meaning in a future version cannot change the
      behavior of any program accepted today.

## Where this comes back

--8<-- "includes/next/language-tour__05-types-planned-for-later.md"

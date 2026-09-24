# Types planned for later

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

## Additional primitive types

Types such as `i8`, `i16`, `i64`, `u8`, `u16`, `u64`, `f16`, and `bf16` may be
added later. They are not aliases for current types. Their names are already
reserved: the lexer rejects them anywhere outside comments and literals in a
v0.1 program, even as the name of a struct or a variable, so adding these types
later cannot break an existing program. `const` is reserved the same way
([Keywords](../specification/lexical-structure.md#24-keywords),
[decision 29](../decisions/lexical.md#d29)).

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

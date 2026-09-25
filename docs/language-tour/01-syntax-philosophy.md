# Syntax philosophy

--8<-- "includes/remember/language-tour__01-syntax-philosophy.md"

## Learning goals

After this chapter, you should be able to explain why Vortex is statically
typed, where its syntax is familiar, and which simplicity rules define v0.1.

## Core design

Vortex uses familiar ideas from Rust and C++, while leaving room for its own
numerical and CPU/GPU features.

- Vortex is statically typed. Every value has a known type before the program
  runs.
- Vortex uses `fn` for functions and `let` for local variables.
- Vortex can infer the type of a local variable from its starting value.
- Function parameter and return types are written explicitly in v0.1, except
  that an omitted function return type means `void`.
- Vortex uses `print` for simple output and debugging.
- Vortex uses `{}` for code blocks.

??? check "`fn log(message: String) { print(message); }` has no `->` in its signature. Can you write `let result = log(message);`?"

    No. Leaving out `->` makes `log`'s return type `void`, and a call to a
    `void` function can only stand alone as a statement. It cannot initialize
    a variable, be passed as an argument, or be returned
    ([why](../decisions/operators.md#d44)).

## What v0.1 allows

- Top-level function and struct declarations.
- Explicit parameter types and optional local-variable type annotations.
- Fixed-size arrays whose dimensions are positive whole numbers written with
  literals and arithmetic, such as `[f32; 4, 4]` or `[f32; 2 * 8]`
  ([decision](../decisions/arrays.md#d11)).
- Straightforward statement-based control flow.
- Shared and mutable references.

??? check "A function starts with `let tile = 4;` and later declares `let row: [f32; tile];`. Does this compile?"

    No. An array dimension must be an integer constant expression built only
    from literals, parentheses and the arithmetic operators. A name is never
    one, even an immutable `let` binding, so `tile` cannot stand in for `4`
    here; this is a constant-evaluation error
    ([decision](../decisions/arrays.md#d11)).

## What v0.1 does not allow

- Dynamic typing or changing a variable's type after declaration.
- Classes, inheritance, traits, generics, modules, or packages.
- Closures, pattern matching, or value-producing `if` expressions.
- Accelerator kernels or parallel-loop syntax. These are planned for later.

## Valid example

```vortex
// program: valid
fn square(value: f32) -> f32 {
    return value * value;
}

fn main() {
    let result = square(4.0);
    print(result);
}
```

This example is valid because every value has a statically known type, the
parameter and non-`void` return type are explicit, and statements use the
v0.1 syntax.

When it runs, it prints `16.0`: `result` is an `f32`, and a floating-point
value always prints with a decimal point
([how `print` writes values](../specification/declarations.md#39-built-in-functions)).

??? check "Why does `print(4)` write `4` while `print(4.0)` writes `4.0`?"

    A literal with no expected type takes its default type: a plain whole
    number defaults to `i32`, which prints as decimal digits, while a number
    written with a point defaults to `f32`, which always prints with a point.
    The two literals have different types and produce different values.

## Invalid example

```vortex
// program: type error
fn main() {
    let mut value = 10;
    value = "ten";      // type error: value is an i32, not a String
}
```

This fails because a variable inferred as `i32` cannot later contain a
`String`. `mut` lets a variable take a new value, never a value of another
type.

## Compiler handling

<details markdown="1">
<summary>Which compiler stage enforces each rule (optional reading)</summary>

The parser (the [compiler stage](../compiler/guide/index.md) that checks how tokens fit together and builds the program's structure) recognizes declarations, blocks, and statements. Name resolution
connects `square` and `value` to their declarations. Type checking assigns or
verifies every value's type and rejects incompatible operations before code
generation.

</details>

## Self-check

**Question:** Does type inference make Vortex dynamically typed?

**Answer:** No. Inference saves the programmer from writing an obvious type,
but the compiler still chooses and checks one fixed type before execution.

## Key ideas

!!! recap "Questions you can now answer"

    - **What does it mean for Vortex to be statically typed?** Every value has
      a type that is known and checked before the program runs, whether the
      programmer wrote it or the compiler inferred it.
    - **Does inferring a local variable's type ever let it change type
      later?** No. Inference only spares writing an obvious type; the
      compiler still fixes and checks one type for the variable's lifetime.
    - **What does an omitted `->` on a function mean, and what can you do
      with a call to that function?** The return type is `void`; the call can
      only stand alone as a statement, never initialize a variable or be
      passed as an argument.
    - **What may an array dimension be built from in v0.1?** Only literals,
      parentheses and arithmetic: an integer constant expression. A name,
      even an immutable one, is never one.
    - **What does `mut` add to a variable declaration?** The ability to take a
      new value of the same type later; it never lets the variable hold a
      value of a different type.
    - **Name two categories of syntax v0.1 leaves for later.** Any two of:
      generics and traits, closures and pattern matching, and accelerator
      kernels and parallel-loop syntax.

## Where this comes back

--8<-- "includes/next/language-tour__01-syntax-philosophy.md"

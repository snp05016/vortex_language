# Kernels and parallel execution planned for later

--8<-- "includes/remember/language-tour__07-kernels-and-parallel-execution.md"

## Learning goals

After this chapter, you should understand what a future kernel is intended to
do and, more importantly, that kernel and parallel syntax is not available in
v0.1.

## Proposed purpose

A kernel is a focused function that processes a large amount of numerical data.
It can eventually run on a GPU or another accelerator. Kernels give the compiler
clear opportunities for optimizations such as loop unrolling, tiling,
vectorization, parallel execution, and kernel fusion.

Vortex will add kernels after ordinary CPU functions, arrays, and a correct
matrix multiplication are working. This keeps v0.1 small and achievable.

??? check "Why does Vortex plan to add kernels only after CPU functions, arrays, and matrix multiplication already work?"

    Kernel support needs a correct, working base to optimize: without ordinary
    functions, arrays, and a working matrix multiplication first, there would
    be nothing for loop unrolling, tiling, vectorization, or fusion to improve.
    Building the base first keeps v0.1 small and achievable.

## Proposed example, not v0.1 syntax

```vortex
// items: planned
kernel double_values(input: &[f32], output: &mut [f32]) {
    for i in 0..input.len() {
        output[i] = input[i] * 2.0;
    }
}
```

This sketch uses several unavailable features: the `kernel` keyword, slices,
methods such as `.len()`, and accelerator execution. The v0.1 parser (the [compiler stage](../compiler/guide/index.md) that
checks how tokens fit together) should not accept it.

??? check "Besides the `kernel` keyword itself, name two other things in the sketch that v0.1 does not have."

    Any two of: slices (`&[f32]`, a view whose length is known only while the
    program runs), a method call such as `.len()`, and accelerator execution.
    v0.1 array types always carry their dimensions
    ([Types planned for later](05-types-planned-for-later.md)), and v0.1 has
    no methods at all.

## What you can do now

Write an ordinary CPU function using fixed-size arrays and explicit bounds:

```vortex
// items: valid
fn double_values(values: &mut [f32; 4]) {
    for index in 0..4 {
        values[index] *= 2.0;
    }
}
```

## What you cannot do now

- Declare a function with `kernel`.
- Launch work on a GPU or accelerator.
- Request parallel loop execution.
- Use slices or runtime `.len()` methods.
- Assume ordinary loops run in parallel automatically.

??? check "Does writing an ordinary `for` loop over a fixed-size array ask the compiler to run its iterations in parallel?"

    No. Every v0.1 loop runs its iterations in order, on the CPU. v0.1 has no
    parallel-loop or kernel-launch statement
    ([Statements and control flow](../specification/statements.md#612-excluded-statements)),
    so nothing in the language can request parallel execution yet.

## Compiler handling

<details markdown="1">
<summary>Which compiler stage enforces each rule (optional reading)</summary>

In v0.1, `kernel` is not a declaration form, so the frontend (the early compiler
stages that read and check source code) rejects the proposed example. Ordinary functions and loops follow the normal parser, type
checker, and CPU code-generation path. Future kernel support will need explicit
execution, memory, synchronization, and target rules before it becomes valid.

</details>

## Practice and self-check

**Question:** Will changing `fn` to `kernel` make a v0.1 function run on a GPU?

**Answer:** No. Kernel syntax and accelerator code generation are future work.

## Key ideas

!!! recap

    - **What is a kernel meant to be, once it exists?** A focused function
      that processes a large amount of numerical data, eventually on a GPU or
      another accelerator.
    - **Why do kernels matter to the compiler?** They give it a clear,
      bounded piece of code to apply optimizations to, such as loop
      unrolling, tiling, vectorization, parallel execution, and fusion.
    - **When will Vortex add kernels?** After ordinary CPU functions, arrays,
      and a correct matrix multiplication already work.
    - **Does v0.1 accept the `kernel` keyword?** No. `kernel` is not a
      declaration form; the frontend rejects it, the same as any other
      unknown syntax.
    - **Can a v0.1 program request parallel loop execution?** No. v0.1 has no
      parallel-loop or kernel-launch statement, so every loop runs its
      iterations in order on the CPU.
    - **What replaces a future slice such as `&[f32]` in v0.1?** A reference
      to a fixed-size array, such as `&[f32; 4]`, whose length is part of its
      type.
    - **Does changing `fn` to `kernel` make a function run on a GPU?** No.
      Kernel syntax and accelerator code generation are both future work.

## Where this comes back

--8<-- "includes/next/language-tour__07-kernels-and-parallel-execution.md"

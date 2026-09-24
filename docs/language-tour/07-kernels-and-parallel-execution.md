# Kernels and parallel execution planned for later

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

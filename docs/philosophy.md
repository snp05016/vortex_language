# Vortex language philosophy

Use this document when deciding whether a feature belongs in Vortex. For exact
syntax, use the [formal grammar](specification/grammar.md). For examples and
teaching material, use the [language tour](language-tour/README.md).

## Contents

- [Purpose](#purpose)
- [Target workloads](#target-workloads)
- [Design principles](#design-principles)
- [Performance philosophy](#performance-philosophy)
- [Safety philosophy](#safety-philosophy)
- [Hardware philosophy](#hardware-philosophy)
- [Programmer and compiler responsibilities](#programmer-and-compiler-responsibilities)
- [Non-goals](#non-goals)
- [Initial scope](#initial-scope)
- [Guiding test](#guiding-test)
- [Feature decision worksheet](#feature-decision-worksheet)

## At a glance

### Vortex v0.1 should allow

- statically typed scalar values, structs, fixed-size arrays, and references;
- explicit mutation and structured control flow;
- array dimension expressions built from integer literals and arithmetic, whose
  values the compiler computes;
- clear numerical kernels that expose useful type and shape information;
- safe CPU execution with documented runtime checks.

### Vortex v0.1 should not allow

- runtime-sized array types whose layout is unknown during compilation;
- implicit unsafe memory operations;
- optimizations that silently change documented program behavior;
- general-purpose features with no clear numerical-computing use;
- undocumented hardware assumptions or unverified performance claims.

## Purpose

Vortex is a statically typed, compiled programming language for writing
high-performance numerical kernels. It is designed to expose enough information
about types, shapes, memory access, and parallelism for the compiler to analyze
and optimize programs for both CPUs and GPUs.

Vortex should let a programmer express **what a computation does** separately
from **how that computation is scheduled on a particular machine**. The same
computation may therefore have different optimized implementations for a
multicore CPU, a SIMD-capable CPU, or a GPU without requiring the algorithm to
be rewritten each time.

Matrix multiplication is the first flagship workload. It is small enough to
understand completely, but rich enough to require important compiler techniques
such as loop transformations, tiling, vectorization, parallel execution, memory
layout selection, and hardware-specific code generation.

## Target workloads

Vortex is intended for compute-intensive programs that perform structured
operations over arrays, matrices, and tensors.

Its primary target workloads are:

- dense matrix multiplication and matrix-vector multiplication;
- element-wise array and tensor operations;
- reductions such as sum, maximum, and mean;
- tensor contractions and batched matrix operations;
- machine-learning kernels such as linear layers, convolutions, activations,
  normalization, and attention primitives;
- scientific-computing kernels with regular loops and predictable memory
  access, including stencil, signal-processing, and image-processing operations.

These workloads are a good fit for Vortex when they contain regular computation,
large amounts of data parallelism, and optimization opportunities that can be
reasoned about by the compiler.

## Design principles

### 1. Give the compiler useful information

The compiler can only optimize what it understands. Vortex should make useful
facts easy for the compiler to see, such as the type of each value, the size and
shape of an array, and which values are allowed to change.

The programmer should not have to explain every small detail. Vortex should
figure out these facts on its own when it safely can.

### 2. Say what to calculate, then decide how to run it

The programmer should first describe the answer they want. For example, they
might say that matrix `C` is the result of multiplying matrices `A` and `B`.

There are many ways to perform that multiplication. A CPU may work best with
small blocks of the matrices, several CPU cores, and SIMD instructions. A GPU
may work best by dividing the work among thousands of threads and using shared
memory.

Those choices change how the work is done, but they do not change the answer.
Vortex should keep the calculation separate from these hardware choices. This
lets the compiler choose a good method for the current machine without making
the programmer rewrite the calculation.

The programmer should still be able to guide or replace the compiler's choices
when they need more control.

### 3. Do not surprise the programmer

A Vortex program should have a clear meaning. Making the program faster must not
quietly change its answer.

Rules about number calculations, changing values, memory, and parallel work
should be written down clearly. If an optimization could slightly change a
floating-point result, Vortex should only use it when the programmer has allowed
that kind of change. In v0.1 no such permission exists: every floating-point
operation rounds once, in the written order
([floating-point strictness](decisions/numbers.md#d56)).

### 4. Make the simple version work well

A programmer should be able to write a clear, straightforward solution and get
reasonable performance. They should not need to understand every CPU or GPU
detail before their program can run well.

When performance really matters, experienced programmers should be able to see
what the compiler chose and give it more specific instructions.

### 5. Keep Vortex focused

Vortex does not need every feature found in a large general-purpose language.
New features should be added only when they make numerical code easier to write,
safer, or easier to optimize.

If a feature does not help Vortex's main kind of work, it can wait.

### 6. Explain performance decisions

The compiler should tell the programmer about important optimizations it made.
For example, it could report that it split a matrix into blocks, used SIMD
instructions, or ran work across several CPU cores.

If the compiler cannot apply an expected optimization, it should explain why.
The programmer should not have to guess why a program is slower than expected.

## Performance philosophy

Performance is a primary design constraint, but source code should not be tied
unnecessarily to one processor model.

On CPUs, Vortex should eventually support optimizations such as:

- loop interchange, fusion, unrolling, and tiling;
- cache-aware and register-aware blocking;
- SIMD vectorization;
- multicore parallel execution;
- specialization for data types, shapes, layouts, and available instructions.

On GPUs, Vortex should eventually support optimizations such as:

- mapping iteration spaces to threads, warps, and workgroups; this means
  dividing the computation's indexes among the GPU's parallel workers so that
  each worker performs a well-defined part of the work;
- shared-memory and register tiling; this means dividing data into reusable
  blocks and keeping those blocks in fast on-chip memory or registers to avoid
  repeated, slower accesses to global memory;
- coalesced global-memory access; this means arranging nearby GPU workers to
  read or write nearby memory locations together, allowing the hardware to
  transfer data efficiently;
- synchronization and memory-transfer planning; this means coordinating
  workers when they share data and scheduling data movement between memory
  levels so that workers do not use incomplete or stale results;
- kernel fusion; this means combining multiple GPU operations into one kernel
  when possible, reducing intermediate data, kernel-launch overhead, and
  unnecessary trips to global memory;
- selection of workgroup sizes and hardware-specific instructions; this means
  choosing how many workers should execute together and using specialized
  instructions when the target GPU supports them, while preserving the
  computation's declared semantics.

The compiler may use cost models, benchmarking, or auto-tuning to choose among
semantically equivalent schedules. Auto-tuning must not change the observable
meaning of a program.

## Safety philosophy

Vortex should prevent memory unsafety and data races in safe code. Its rules
should be strong enough for the compiler to reason about mutation and aliasing
without relying on undocumented programmer assumptions.

For v0.1 the reference model is deliberately small: references appear only as
parameters and immutable local bindings, a `let` borrow lasts to the end of its
block, and while a `&mut` borrow is live nothing else can reach the borrowed
variable ([decision](decisions/references.md#d41)). A fuller ownership and
memory model has not yet been selected. It should be designed around the needs
of numerical kernels rather than copied wholesale from an existing systems
language. Any future unsafe operations must be explicit, localized, and
justified by interoperability or low-level hardware access.

Numerical safety is separate from memory safety. Integer overflow, floating-point
reassociation, reduced precision, and non-deterministic parallel reductions must
have documented behavior. For v0.1 integers,
[Expressions 5.5](specification/expressions.md#checked-integer-operations)
lists every checked operation. More aggressive numerical transformations should
require an explicit language mode or programmer permission when they can change
observable results.

## Hardware philosophy

Vortex targets heterogeneous machines, beginning with CPUs and later expanding
to GPUs. The language should expose hardware concepts when they affect program
correctness or when explicit control is necessary, but routine code should not
need to name a particular GPU model, vector width, or cache size.

Hardware-independent computation is the default. Hardware-specific schedules
and specialized implementations are permitted, provided that they implement the
same declared semantics.

Portability in Vortex means that a correct program can be compiled for supported
targets. It does not mean that one schedule will perform equally well on every
target.

## Programmer and compiler responsibilities

The programmer is responsible for:

- expressing the intended computation and its required numerical behavior;
- providing constraints the compiler cannot safely infer;
- choosing explicit hardware-specific control only when necessary;
- measuring performance claims using representative inputs and hardware.

The compiler is responsible for:

- preserving the program's declared semantics;
- rejecting invalid or unsafe programs with useful diagnostics;
- validating scheduling choices and transformations;
- producing target-specific code from hardware-independent computations;
- explaining major optimization decisions and missed optimizations;
- never presenting an unverified performance estimate as a measured result.

## Non-goals

Vortex is not initially intended to be:

- a general-purpose replacement for C++, Rust, or Python, although it may 
  eventually be used as a language calling RT libraries written in those languages;
- a language for operating systems, device drivers, or embedded firmware;
- a web, application, or user-interface development language;
- a complete machine-learning framework;
- a distributed-computing platform;
- a language optimized primarily for dynamic typing or rapid scripting;
- an attempt to support every accelerator or numerical workload in its first
  release.

Sparse linear algebra, automatic differentiation, distributed execution, and
full-model machine learning may be explored later, but they are outside the
initial language scope.

## Initial scope

The first useful version of Vortex should be deliberately narrow. It should be
able to express and compile a correct dense matrix multiplication using:

- statically typed values including `void`, `bool`, `char`, `i32`, `u32`,
  `usize`, `f32`, and `f64`, together with basic UTF-8 strings for output and
  debugging;
- fixed-rank dense arrays with compiler-visible element types and shapes;
- functions, local variables, conditionals, and structured loops;
- explicit mutation and basic shared and mutable references;
- checked runtime behavior for invalid array access, division by zero, integer
  overflow, invalid shift counts, and invalid casts;
- a simple CPU backend that produces correct, unoptimized programs.

After v0.1 is correct, the first optimization milestone is to transform a
straightforward matrix multiplication into a faster CPU implementation while
preserving its semantics. Optimization diagnostics, SIMD, multicore execution,
GPU code generation, and automatic schedule search should be added incrementally
after the basic language and CPU implementation are correct and measurable.

## Guiding test

When considering a new feature, ask:

> Does this feature help programmers express numerical computation more clearly,
> help the compiler prove or optimize more, or provide necessary control over
> CPU and GPU execution?

If the answer is no, the feature probably does not belong in the initial Vortex
language.

## Feature decision worksheet

Before adding a feature, write down answers to all of these questions:

1. **Problem:** Which real Vortex program becomes clearer, safer, or faster?
2. **Example:** What is the smallest valid source example?
3. **Boundary:** What similar syntax remains invalid?
4. **Compiler knowledge:** What new fact does the compiler learn?
5. **Safety:** Can the feature create memory, numerical, or concurrency hazards?
6. **Targets:** Does it have consistent CPU and future GPU meaning?
7. **Diagnostics:** What should the compiler say when the feature is misused?
8. **Testing:** Which positive, negative, and boundary tests prove it works?

Example decision for expression-based array dimensions:

| Question | Decision |
| --- | --- |
| Useful program | `[f32; 16 * 2]` keeps a derived fixed shape readable; named sizes such as `tile_size * 2` wait for `const`, planned as the first addition after v0.1. |
| Allowed | Integer constant expressions: integer literals combined with `+`, `-`, `*`, `/`, `%` and parentheses. |
| Not allowed in v0.1 | Any name or call in a dimension, including a function parameter or an immutable variable. |
| Compiler responsibility | Parse an expression, check that it is an integer constant expression, evaluate it with checked `usize` arithmetic, then require a value of at least 1 ([decision](decisions/arrays.md#d11)). |
| Failure diagnostic | A constant-evaluation error that names the dimension and the reason: not a constant expression, not an integer, arithmetic that goes negative or too large, or zero ([decision](decisions/arrays.md#d10)). |

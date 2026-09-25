---
hide:
  - navigation
---

# Vortex

<p class="lead">Vortex is a small, statically typed language for math-heavy code like matrix multiplication.</p>

"Statically typed" means the compiler knows the type of every value, such as
`i32` (a 32-bit integer) or `f32` (a 32-bit decimal number), and checks that
each value is used correctly before the program ever runs.

## One complete example

```vortex
// program: valid
struct Point {
    x: f32,
    y: f32,
}

fn length_squared(point: Point) -> f32 {
    return point.x * point.x + point.y * point.y;
}

fn main() {
    let point = Point { x: 3.0, y: 4.0 };
    print(length_squared(point));
}
```

This program shows most of what a Vortex file contains:

- a `struct` declaration that groups two numbers into one value;
- a function, `length_squared`, that takes a `Point` and returns an `f32`;
- `main`, the function every executable program starts from;
- a local variable, `point`, whose type the compiler works out on its own.

<a class="start-tour" href="language-tour/">New here? Start the tour</a>

!!! important "Specification status"
    This site describes the intended v0.1 language. The compiler does not yet
    implement all of it. Check the [implementation roadmap](roadmap.md) before
    relying on a feature in the compiler.

## Read the documentation

Two paths through this site: one for learning the language and its
compiler, one for evaluating the project.

### For learners

<div class="document-index">
  <div class="document-link">
    <a href="language-tour/">Language tour</a>
    <span>Start here. Short chapters with examples that teach you to write Vortex programs, each with check questions and a recap.</span>
  </div>
  <div class="document-link">
    <a href="specification/">Language specification</a>
    <span>The exact rules for syntax, types, behavior, and the errors a compiler must report. These rules are normative: a compiler must follow them to count as a correct Vortex compiler.</span>
  </div>
  <div class="document-link">
    <a href="compiler/guide/">Build v0.1</a>
    <span>A stage-by-stage reading guide to what the compiler needs at each step, with a workbook to track your progress and recipes for common changes.</span>
  </div>
  <div class="document-link">
    <a href="backend/">Back end</a>
    <span>A native code generator by hand: the machine, a first back end, the classical pipeline, and how LLVM does it. Chapters A1 to E4.</span>
  </div>
  <div class="document-link">
    <a href="optimize/">Optimize</a>
    <span>How a compiler makes correct code fast, from the middle end to a fast CPU matrix multiplication. Chapters O1 to O12 and P1 to P16, plus the CPU matmul ladder.</span>
  </div>
  <div class="document-link">
    <a href="gpu/">GPU</a>
    <span>How GPUs execute, why they are fast, and a matmul ladder that puts it into practice. Chapters G1 to G15.</span>
  </div>
  <div class="document-link">
    <a href="mlir/">MLIR</a>
    <span>How MLIR represents and lowers programs, and how it could carry Vortex to GPUs. Chapters M1 to M12.</span>
  </div>
  <div class="document-link">
    <a href="progress/">Your progress</a>
    <span>Records which chapters you have opened and tells you when a review page is due. Kept in your browser only.</span>
  </div>
  <div class="document-link">
    <a href="concept-map/">Concept map</a>
    <span>Which ideas each idea needs first, drawn as one map per book.</span>
  </div>
  <div class="document-link">
    <a href="compiler/architecture/">Compiler internals</a>
    <span>How this repository's compiler is organized into passes, and how its parser and syntax tree are designed.</span>
  </div>
  <div class="document-link">
    <a href="language-and-compiler-cheatsheet/">Language and compiler cheat sheet</a>
    <span>A compact lookup page for language and compiler terminology.</span>
  </div>
</div>

### For evaluators

<div class="document-index">
  <div class="document-link">
    <a href="project/">For reviewers</a>
    <span>What exists in the repository today, what is being built next, and where the evidence for each claim lives.</span>
  </div>
</div>

## Specification chapters

<ol class="chapter-list">
  <li><span class="chapter-number">01</span><a href="specification/conformance/">Conformance and terminology</a><span>How to read the rules, and what it means for a compiler to conform (follow every required rule).</span></li>
  <li><span class="chapter-number">02</span><a href="specification/lexical-structure/">Lexical structure</a><span>Source text, identifiers, literals, comments, keywords, and punctuation.</span></li>
  <li><span class="chapter-number">03</span><a href="specification/declarations/">Programs and declarations</a><span>Whole programs (a program is everything in one source file), functions, structs, scopes, and the program entry point.</span></li>
  <li><span class="chapter-number">04</span><a href="specification/types-and-values/">Types and values</a><span>Primitive types, compound types, values, conversions, and type equivalence.</span></li>
  <li><span class="chapter-number">05</span><a href="specification/expressions/">Expressions</a><span>Operators, calls, casts, indexing, field access, evaluation, and precedence.</span></li>
  <li><span class="chapter-number">06</span><a href="specification/statements/">Statements and control flow</a><span>Bindings, assignment, return, blocks, branches, and loops.</span></li>
  <li><span class="chapter-number">07</span><a href="specification/arrays/">Arrays and shapes</a><span>Fixed-size array types, constant dimensions, construction, indexing, assignment, nested arrays, and memory layout.</span></li>
  <li><span class="chapter-number">08</span><a href="specification/structs/">Structs</a><span>Struct declarations, construction, field access and assignment, value semantics, and layout.</span></li>
  <li><span class="chapter-number">09</span><a href="specification/references/">References and mutability</a><span>Borrowed access, mutation permissions, assignment targets, and lifetime boundaries.</span></li>
  <li><span class="chapter-number">10</span><a href="specification/diagnostics/">Diagnostics</a><span>Error categories, what each diagnostic must contain, recovery, runtime error reporting, and optional warnings.</span></li>
  <li><span class="chapter-number">11</span><a href="specification/grammar/">Formal grammar</a><span>The parser-facing grammar and its relationship to semantic requirements.</span></li>
  <li><span class="chapter-number">12</span><a href="specification/glossary/">Glossary</a><span>Definitions of the terms used throughout the specification.</span></li>
</ol>

## v0.1 at a glance

| Area | Included in v0.1 |
| --- | --- |
| Program structure | Top-level functions and structs, with exactly one valid `main` for an executable |
| Primitive types | `void`, `bool`, `char`, `i32`, `u32`, `usize`, `f32`, `f64`, and `String` |
| Compound types | Fixed-size arrays, user-defined structs, and basic references |
| Values | Literals, names, array values, repeat arrays, and struct values |
| Expressions | Unary, binary, range (only as a `for` loop's iterable), call, cast, index, field access, and grouping |
| Statements | Local declarations, assignment, return, expression statements, blocks, conditionals, and loops |
| Safety model | Explicit mutation, name and type checks, fixed-shape validation, and documented runtime checks |

The first goal is a correct compiler for ordinary processors (CPUs) that handles straightforward fixed-size
matrix multiplication. GPU execution, runtime-sized collections, tensors,
generics, modules, and advanced scheduling are later design work.

## How the documents relate

```text
Language specification
    defines accepted programs and their required meaning

Formal grammar
    defines the exact token structure accepted by the parser

Language tour
    teaches the same rules through examples and exercises

Compiler guide and internals
    explain how a compiler can implement and test those rules

Decision records
    explain why each rule was chosen
```

A few words in that picture may be new. A **token** is one word or symbol of
the language, such as `let`, `point`, or `+`. The **parser** is the part of the
compiler that checks whether tokens are arranged in a valid structure. Every
other term used on this site is defined in the
[glossary](specification/glossary.md).

The specification chapters, including the
[formal grammar](specification/grammar.md), are normative: they are the rules.
Every other page explains them. When two pages disagree,
[Document authority](specification/conformance.md#11-document-authority)
decides which is right; please report the mismatch. The reasons are in
[record 49](decisions/documentation.md#d49).

## Design direction

Vortex is designed around three commitments:

1. Expose useful information about types, shapes (the fixed sizes of arrays),
   memory access, and mutation.
2. Keep the mathematical computation separate from hardware scheduling.
3. Prefer explicit, testable behavior over hidden performance assumptions.

Read the [language philosophy](philosophy.md) for the full rationale and the
[v0.1 roadmap](roadmap.md) for the implementation sequence.

## Authorship and methodology

Large language models helped draft these documents, with human review of the
language decisions; see the full
[authorship and methodology statement](authorship.md).

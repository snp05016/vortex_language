# Vortex v0.1 roadmap

This roadmap is organized like an implementation assignment. Each milestone has
an objective, required work, and an observable completion condition. Do not mark
a milestone complete merely because its main code path exists.

Each milestone has a matching chapter in the
[compiler guide](compiler/guide/index.md), which explains in plain language
what the milestone needs, what it can leave out, and where to read more.

## Contents

- [How to use this roadmap](#how-to-use-this-roadmap)
- [Goal](#goal)
- [Milestones 0-3: compiler front end](#milestone-0-project-foundation)
- [Milestones 4-5: names and types](#milestone-4-names-and-scopes)
- [Milestones 6-10: executable programs](#milestone-6-basic-cpu-code-generation)
- [Milestone 11: release gate](#milestone-11-v01-release-gate)
- [Definition of done](#definition-of-done)
- [After v0.1](#after-v01)

## How to use this roadmap

For every milestone:

1. Read the matching specification chapters, including the grammar, and the
   language-tour sections that explain them
   ([record 49](decisions/documentation.md#d49)).
2. Write at least one valid example and one invalid example before implementation.
3. Implement the smallest complete vertical slice.
4. Add positive, negative, and source-location tests.
5. Run all earlier milestone tests to detect regressions.
6. Record known limitations rather than silently accepting partial behavior.

### Evidence required before calling a milestone complete

- The project builds from a clean configuration.
- All automated tests pass.
- Every documented example for the milestone gives the result its label names
  (see [Specification examples](specification/conformance.md#18-specification-examples)
  and [record 28](decisions/documentation.md#d28)).
- Diagnostics point at the relevant source span.
- The AST or generated output is stable enough to inspect.

### Scope rule

Milestones are cumulative. Work from a later milestone may be prototyped, but it
must not weaken or bypass an earlier milestone's correctness rules.

## Goal

Vortex v0.1 should compile a small Vortex source file into a correct CPU
executable. The final demonstration is a straightforward, unoptimized matrix
multiplication using fixed-size multidimensional arrays.

The first release is about correctness, useful errors, and a complete compiler
pipeline. Performance optimization begins after v0.1.

## Milestone 0: Project foundation

- Choose the implementation language and build system.
- Create a `vortex` command that accepts a source-file path and follows the
  [command-line decision](decisions/program.md#d20).
- Add an automated test command.
- Create folders for valid programs, invalid programs, and expected output.
- Make one command build the compiler and run all tests.

This milestone is complete when the empty compiler builds reliably and the test
runner can report a passing and a failing test.

## Milestone 1: Source files and diagnostics

- Read a Vortex source file and reject bytes that are not valid UTF-8
  ([decision 15](decisions/lexical.md#d15)).
- Track file names, line numbers, and column numbers, counted as
  [Conformance 1.7](specification/conformance.md#17-source-locations) defines
  ([record 16](decisions/diagnostics.md#d16)).
- Create one consistent diagnostic format for errors.
- Show the relevant source line and point to the problem.

This milestone is complete when the compiler can report a readable error at an
exact location in a source file.

## Milestone 2: Lexer

The lexer turns source text into tokens.

- Recognize identifiers and v0.1 keywords.
- Recognize integer, floating-point, boolean, character, and string literals.
- Recognize operators, punctuation, braces, and semicolons.
- Handle whitespace, `//` comments, and escape sequences.
- Report every lexical error in the
  [specification's list](specification/lexical-structure.md#27-lexical-errors),
  such as invalid characters, unfinished literals, malformed numbers and
  reserved words ([decision 19](decisions/lexical.md#d19)).
- Add tests for every token and lexer error.

This milestone is complete when a Vortex file can be printed as a correct token
stream with accurate source locations.

## Milestone 3: Parser and syntax tree

The parser turns tokens into a syntax tree.

- Parse functions, parameters, return types, and `main`.
- Parse variable declarations, assignments, blocks, and return statements.
- Parse `if`, `else`, `while`, `for`, `break`, and `continue`.
- Parse expressions using the documented operator precedence.
- Parse function calls, casts, arrays, indexing, structs, and references.
- Recover from simple syntax errors so one mistake does not hide every later
  error.
- Add parser tests for valid and invalid programs.

This milestone is complete when the compiler can print a stable syntax tree for
every v0.1 language construct.

## Milestone 4: Names and scopes

- Build a symbol table for functions, variables, parameters, and struct fields.
- Reject unknown names.
- Reject duplicate names in the same scope, and any declaration that reuses a
  visible name, since Vortex has no shadowing
  ([decision record](decisions/names.md#d2)).
- Keep variables inside the blocks where they were declared.
- Validate that the program has exactly one valid `main` function.
- Add tests for shadowing, duplicate names, and out-of-scope variables; each
  expects a name error.

This milestone is complete when every name in a program resolves to one known
declaration.

## Milestone 5: Types, mutability, and control-flow checks

- Implement the v0.1 types: `void`, `bool`, `char`, `i32`, `u32`, `usize`,
  `f32`, `f64`, `String`, fixed-size arrays, structs, and basic references.
- Infer local-variable types from their starting values.
- Check operators, assignments, function arguments, and return values.
- Check array dimensions and indexes.
- Reject a range used anywhere except as the iterable of a `for` loop
  ([decision 36](decisions/statements.md#d36)).
- Parse array dimensions as expressions; require each to be an integer constant
  expression (integer literals with `+`, `-`, `*`, `/`, `%` and parentheses)
  and evaluate it with checked `usize` arithmetic
  ([decision](decisions/arrays.md#d11)).
- Reject dimensions that use names or calls, that are not integers, or that go
  negative or overflow, with dimension-specific constant-evaluation errors.
- Reject zero extents, written or computed, with a constant-evaluation error
  ([decision](decisions/arrays.md#d10)); the parser must still accept them.
- Check every operation whose deciding operands are integer constant
  expressions, such as `10 / 0`, `u32(-1)` or a constant index, and reject
  failures as constant-evaluation errors
  ([record 39](decisions/diagnostics.md#d39)).
- Reject changes to immutable values.
- Enforce the reference rules of
  [References and mutability](specification/references.md): where references
  may appear, lexical borrows, and whole-variable conflicts
  ([decision](decisions/references.md#d41)).
- Verify that the body of every non-`void` function ends in a terminating
  statement ([decision 9](decisions/statements.md#d9)).
- Reject `break` and `continue` outside loops.
- Add a negative test for every rule.

This milestone is complete when invalid programs are rejected before code
generation with clear explanations.

Required dimension examples:

```vortex
// statements: valid
let matrix: [f32; 2 + 2, 8] = [0.0; 2 + 2, 8]; // valid: constant expressions
```

```vortex
// items: constant-evaluation error
fn invalid(rows: usize) {
    let matrix: [f32; rows, 8] = [0.0; rows, 8];
    // invalid in v0.1: rows is only known at runtime
}
```

## Milestone 6: Basic CPU code generation

- Generate CPU code for `main`, literals, arithmetic, variables, and `return`.
- Emit an object file or executable using the chosen backend.
- Link the generated code with Vortex's small runtime.
- Run the generated program and capture its output and its exit status (0 when
  `main` returns).

Use this program as the first end-to-end test:

```vortex
// program: valid
fn main() {
    let result = 2 + 3 * 4;
    print(result);
}
```

This milestone is complete when the program compiles, runs, prints `14`, and
exits with status 0.

## Milestone 7: Functions and control flow

- Generate calls to user-defined functions.
- Generate `if`, `else`, `while`, and `for` control flow.
- Generate `break`, `continue`, and early `return` behavior.
- Support local variables inside nested blocks.
- Test nested loops and nested conditionals.

This milestone is complete when functions and every v0.1 control-flow statement
work in compiled programs.

## Milestone 8: Strings, arrays, structs, and references

- Support basic UTF-8 string literals for printing and diagnostics.
- Support fixed-size one-dimensional and multidimensional arrays.
- Support array literals and repeat expressions such as `[0.0; 16]`.
- Support expression dimensions in array types and repeat-array expressions,
  including multidimensional forms such as `[0.0; 2 + 2, 8]`.
- Support struct creation and field access.
- Support shared and mutable references in function calls.
- Implement the specified layout (contiguous row-major arrays, struct fields in
  declaration order) and document sizes, alignment, padding, and how references
  are represented ([decision](decisions/arrays.md#d43)).
- Add tests that pass arrays into functions through references, without
  copying them.

This milestone is complete when compiled programs can safely create, read,
change, and pass v0.1 data types.

## Milestone 9: Runtime safety

- Check array bounds unless the compiler can prove an access is safe.
- Detect integer division and remainder by zero.
- Detect integer overflow in every operation that
  [Expressions 5.5](specification/expressions.md#checked-integer-operations)
  lists, including negation and compound assignment
  ([record 34](decisions/diagnostics.md#d34)).
- Detect shift counts that are negative or not less than the bit width of the
  shifted type.
- Detect invalid numerical casts.
- Stop the program with a clear runtime error when a check fails: one line on
  standard error, then exit status 101
  ([decision](decisions/program.md#d14)).
- Stop the program with a `stack` report when it runs out of stack, and
  document the stack size ([record 46](decisions/diagnostics.md#d46)).
- Test each failure and each successful boundary case.

This milestone is complete when the runtime behavior matches the
specification's runtime rules: the required checks in
[Diagnostics](specification/diagnostics.md#runtime-error) and the rules in
[Runtime reporting](specification/diagnostics.md#106-runtime-reporting).

## Milestone 10: Matrix multiplication

- Write matrix multiplication using ordinary Vortex functions and loops.
- Use fixed-size multidimensional `f32` arrays.
- Pass inputs through shared references and the output through a mutable
  reference.
- Compare the result with a known correct answer.
- Test square and rectangular matrix shapes.
- Report shape or type mistakes during compilation.

This milestone is complete when Vortex compiles and correctly runs the matrix
multiplication example on the CPU without optimization.

## Milestone 11: v0.1 release gate

- Run every valid and invalid compiler test.
- Compile every v0.1 example from the documentation, and check that each gives
  the result its label names.
- Verify diagnostic source locations and runtime error messages.
- Document the compiler command ([decision](decisions/program.md#d20)),
  supported features, and known limitations.
- Confirm that a clean checkout can build and run the test suite.
- Mark the release as `v0.1.0` only after every item above passes.

## Definition of done

Vortex v0.1 is finished when it can:

1. Read, tokenize, parse, and type-check a documented Vortex program.
2. Reject invalid programs with useful source-based diagnostics.
3. Produce and link a CPU executable.
4. Run the documented scalar, control-flow, function, string, struct, array,
   and reference examples.
5. Detect the documented runtime safety errors.
6. Compile and correctly run naive matrix multiplication.

## After v0.1

These features are deliberately outside the first release:

- named constants (`const` declarations), the first language addition after
  v0.1, so that array dimensions can use names;
- dead-code elimination and constant folding;
- loop transformations, tiling, and fusion;
- SIMD vectorization;
- multicore CPU execution;
- vectors, slices, and higher-level tensor types;
- kernels and GPU code generation;
- optimization diagnostics and cost models;
- auto-tuning;
- modules, packages, generics, traits, and advanced ownership.

# Diagnostics and runtime checks

These records settle how Vortex reports problems: how source positions are
counted, which integer operations are checked, which failures the compiler must
catch before a program runs, what happens when a program runs out of stack, and
what warnings may do. The specification chapters, mainly
[Conformance](../specification/conformance.md),
[Expressions](../specification/expressions.md) and
[Diagnostics](../specification/diagnostics.md), now state the rules; each
record below explains why the rule was chosen and what it changed.

## 16. Source positions {#d16}

**Status:** Accepted, 2026-09-23. **Applies to:** v0.1. **Resolves:** none.
**Guide stage:** 1, 2.

**Question.** What does a source span measure, and how does a position become
the line and column that a diagnostic prints?

**Before this decision.**
[Conformance 1.7](../specification/conformance.md#17-source-locations) and the
[glossary](../specification/glossary.md) gave a span a start and a length but
never said what they count. Guide
[stage 1](../compiler/guide/stage-1-source-and-diagnostics.md#positions-and-spans)
left the unit, the first number, line breaks and tabs open.

**Options.**

- Columns in bytes, as the file stores them.
- Columns in characters.
- Display columns, which depend on each editor's tab width.

**Elsewhere.** Go's [`token.Position`][go-token] counts columns in bytes,
from 1. GCC counts [display columns by default, or bytes][gcc-colunit], and
its [columns start at 1][gcc-colorigin]. Rust [replaces each CR LF pair with
LF][rs-input] before reading tokens.

**Decision.**

- A span is a byte offset (the number of bytes before it in the file) and a
  length in bytes. It may be empty, marking a point. The end of the file is a
  position too.
- Lines and columns count from 1.
- A line break is LF, or CR followed by LF, counted as one break. A CR without
  LF is a lexical error.
- A column is one more than the number of characters (Unicode scalar values,
  [record 15](lexical.md#d15)) between the start of the line and the position.
  A tab counts as one, and so does each byte of invalid UTF-8. A leading byte
  order mark counts in offsets but not in columns.

**Why.** Byte offsets are cheap to store and compare. Character columns give
the same answer in every editor, whatever its tab width, and differ from bytes
only on lines with non-ASCII text.

**Consequences.** In `let s = 'λ';` the semicolon is at column 12; counting
bytes would give 13, because `λ` takes two bytes. Stage 1 turns offsets into
lines and columns; stage 2 rejects a lone CR. Pages changed: Conformance 1.7,
the glossary, Lexical structure 2.1, Diagnostics 10.1, Milestone 1, the cheat
sheet, the AST guide, the word list, and guide stages 0 to 2.

## 34. Checked integer operations {#d34}

**Status:** Accepted, 2026-09-23. **Applies to:** v0.1. **Resolves:** none.
**Guide stage:** 5, 9.

**Question.** Which integer operations must be checked, and what counts as
overflow (a result outside its type's range)?

**Before this decision.**
[Expressions 5.5](../specification/expressions.md#55-arithmetic-and-bitwise-expressions)
said only that overflow, shift counts and division by zero "are checked";
[Diagnostics 10.2](../specification/diagnostics.md#runtime-error) and
[Milestone 9](../roadmap.md#milestone-9-runtime-safety) omitted shifts. Nothing
settled unary `-`, compound assignment or casts.

**Options.**

- One general sentence, leaving each operation to the implementation.
- A list with one row per checked operation.

**Elsewhere.** The Rust reference [lists which integer operations
overflow][rs-overflow] and panics on them in debug builds. Swift [traps on
overflow][sw-overflowops] unless code uses a wrapping operator such as `&+`.
In Zig, [integer overflow][zig-overflow] is safety-checked illegal behavior.

**Decision.** For `i32`, `u32` and `usize`, these operations are checked:

- binary `+`, `-`, `*`: the exact result must fit the type;
- unary `-`: the same; only -2147483648 fails;
- `/`: the divisor must not be zero, and the result must fit (only
  -2147483648 / -1 fails, [record 33](operators.md#d33));
- `%`: the divisor must not be zero;
- `+=`, `-=`, `*=`, `/=`, `%=`: the checks of their operators;
- a cast to an integer type: the value must be representable
  ([record 27](numbers.md#d27));
- `<<`, `>>`: the count must be at least 0 and below the bit width
  ([record 22](operators.md#d22)).

A failed check is a runtime error, or a constant-evaluation error when its
deciding operands are constant ([record 39](#d39)). Bits that `<<` shifts out
are dropped, which is not overflow. A `for` loop never overflows its counter
([record 13](statements.md#d13)). Floating-point operations are not checked.

**Why.** One row per operation gives one test per row, matching Milestone 9,
and leaves no case to the back end, where several have no defined result.

**Consequences.**

```vortex
// statements: runtime error
let mut count: u32 = 0;
count -= 1; // runtime error: 0 - 1 does not fit in u32
```

Stage 9 adds the checks; stage 5 evaluates the constant cases. Pages changed:
Expressions 5.5, Diagnostics 10.2, the glossary, the philosophy, Milestone 9,
tour chapters 6, 8 and 9, and guide stages 9 and 11.

## 39. Provable runtime failures {#d39}

**Status:** Accepted, 2026-09-23. **Applies to:** v0.1. **Resolves:** N1.
**Guide stage:** 5, 9.

**Question.** When a checked operation is certain to fail, must the compiler
reject the program, or wait until it runs?

**Before this decision.**
[Conformance 1.4](../specification/conformance.md#14-static-and-dynamic-rules)
said "may", [Diagnostics 10.2](../specification/diagnostics.md#runtime-error)
"should", [Arrays 7.6](../specification/arrays.md#76-indexing) "rejects", and
guide
[stage 10](../compiler/guide/stage-10-matrix-multiplication.md#shape-mistakes-are-compile-time-errors)
accepted either outcome. Acceptance depended on the compiler's cleverness, so
no test could be written.

**Options.**

- Reject whatever the compiler can prove will fail.
- Reject nothing; check everything at run time.
- Check during compilation exactly when the deciding operands are constant.

**Elsewhere.** Go rejects a [constant zero divisor][go-arith], a [constant its
type cannot represent][go-constexpr] and a [constant index out of
range][go-index]. Rust makes such failures errors only [in constant
contexts][rs-consteval], and elsewhere through [lints that are errors by
default][rs-lints].

**Decision.**

- The deciding operands are the divisor for division by zero, the count for a
  shift, the index for indexing, the operand for a cast, and every operand for
  overflow.
- When every deciding operand of a checked operation ([record 34](#d34), or
  indexing) is an integer constant expression (integer literals, unary `-`,
  parentheses and `+ - * / %`; [record 11](arrays.md#d11)), the compiler must
  perform the check by the run-time rules, even if the operation never runs. A
  failure is a constant-evaluation error.
- Every other failure is a runtime error. An implementation must not reject a
  program because it can prove that a run-time check will fail; it may warn
  ([record 48](#d48)).

**Why.** Every implementation accepts the same programs, and each case can be
tested.

**Consequences.**

```vortex
// statements: constant-evaluation error
let values = [10, 20, 30];
let last = values[3]; // index 3 is outside extent 3
```

With a variable index holding 3, it compiles and fails when it runs.
`u32(-1)` is rejected, but `i32(3.0e10)` fails only at run time, because a
float literal is not an integer constant expression. Stage 5 evaluates constant
operands; stage 9 keeps every other check. Pages changed: Conformance,
Expressions 5.12, Diagnostics, the glossary and overview, the tour, the cheat
sheet, the architecture page, the roadmap, and guide stages 5, 9 and 10.

## 46. Stack exhaustion {#d46}

**Status:** Accepted, 2026-09-23. **Applies to:** v0.1. **Resolves:** N18.
**Guide stage:** 9.

**Question.** What must happen when a program runs out of call stack, the
memory that holds each active call's parameters and local variables?

**Before this decision.**
[Conformance 1.5](../specification/conformance.md#15-undefined-behavior)
promised a well-formed program no undefined behavior, and recursion is allowed
([record 3](names.md#d3)), but no page said what deep recursion or large local
arrays do when the stack runs out. The
[implementation-limit category](../specification/diagnostics.md#implementation-limit-error)
covered only features a compiler had not built yet.

**Options.**

- Leave the outcome unspecified.
- Require the program to stop with a report.
- Forbid recursion, so that stack use is known before the program runs.

**Elsewhere.** Rust documents a [default stack of 2 MiB][rs-stack] for the
threads it spawns. Go [crashes the program][go-maxstack] when a goroutine's
stack grows past its limit.

**Decision.**

- Exhausting the stack must stop the program. Execution must not continue past
  that point, and the program must not end without a report.
- The stop is reported like a failed runtime check, with the kind `stack`
  ([record 14](program.md#d14)). Its category is implementation-limit: the
  program broke no language rule; it exceeded a documented limit of the
  implementation.
- An implementation must document its stack size
  ([record 55](documentation.md#d55)).
- A compiler must not reject a program because it might exhaust the stack; it
  may warn ([record 48](#d48)).

**Why.** It keeps the promise of no undefined behavior without forbidding
recursion, and it gives tests one observable outcome.

**Consequences.** A function whose recursion never ends, such as
`fn spin() { spin(); }`, still compiles. When it runs, the program writes a
line starting `runtime error[stack]` to standard error and exits with status
101 instead of crashing. The same holds for one call whose local arrays are
larger than the stack. How exhaustion is detected is the implementation's
choice. Stage 9 adds the check and a test; stage 7, which introduces the call
stack, mentions the limit. Pages changed: Conformance 1.5, Diagnostics 10.2
and 10.6, Milestone 9, the word list, and guide stages 1, 7, 9 and 11.

## 48. Warnings {#d48}

**Status:** Accepted, 2026-09-23. **Applies to:** v0.1. **Resolves:** N5.
**Guide stage:** 0, 1.

**Question.** May a Vortex compiler print warnings, and may a warning change
the result of compiling?

**Before this decision.**
[Statements 6.5](../specification/statements.md#65-expression-statements) and
the [grammar](../specification/grammar.md#expression-statements) let an
implementation warn about a discarded value, and the
[home page](../index.md#specification-chapters) said the Diagnostics chapter
covers warnings, though that chapter never mentioned them. Guide
[stage 1](../compiler/guide/stage-1-source-and-diagnostics.md#words-for-this-stage)
said "the specification defines no warnings".

**Options.**

- Forbid warnings.
- Allow optional warnings that change nothing.
- Allow warnings that can reject a program.

**Elsewhere.** Rust sorts its lints (named checks the compiler runs) into
levels, and [some are errors by default][rs-lints], so they decide whether a
program compiles. Go's specification lets a compiler [reject a local variable
that is never used][go-vardecl].

**Decision.** An implementation may issue warnings: diagnostics that point out
a likely mistake without rejecting the program. No warning is required. A
warning must not change whether a program is accepted, the compiler's exit
status or the executable it produces. Warnings are not one of the eight
diagnostic categories, and conformance tests never check for them. Which
warnings exist, and their wording, are implementation-defined
([record 55](documentation.md#d55)).

**Why.** It removes the contradiction between the guide and the specification
and keeps one rule for acceptance: every conforming compiler accepts the same
programs, whatever warnings it adds.

**Consequences.**

```vortex
// statements: valid
let width = 128;
width * 2; // a compiler may warn that this value is discarded
```

A test of this block passes whether or not a warning appears. Test runners
judge a result by the exit status and the error diagnostics, and ignore
warning lines. Stage 1 needs no warning support. Pages changed: Diagnostics
10.1, Statements 6.5, the grammar, the glossary, the home page, tour chapter 9,
the cheat sheet, the word list, and guide stages 0, 1 and 5.

[go-token]: https://pkg.go.dev/go/token#Position
[gcc-colunit]: https://gcc.gnu.org/onlinedocs/gcc/Diagnostic-Message-Formatting-Options.html#index-fdiagnostics-column-unit
[gcc-colorigin]: https://gcc.gnu.org/onlinedocs/gcc/Diagnostic-Message-Formatting-Options.html#index-fdiagnostics-column-origin
[rs-input]: https://doc.rust-lang.org/reference/input-format.html
[rs-overflow]: https://doc.rust-lang.org/reference/expressions/operator-expr.html#overflow
[sw-overflowops]: https://docs.swift.org/swift-book/documentation/the-swift-programming-language/advancedoperators#Overflow-Operators
[zig-overflow]: https://ziglang.org/documentation/0.16.0/#Integer-Overflow
[go-arith]: https://go.dev/ref/spec#Arithmetic_operators
[go-constexpr]: https://go.dev/ref/spec#Constant_expressions
[go-index]: https://go.dev/ref/spec#Index_expressions
[rs-consteval]: https://doc.rust-lang.org/reference/const_eval.html
[rs-lints]: https://doc.rust-lang.org/rustc/lints/listing/deny-by-default.html
[rs-stack]: https://doc.rust-lang.org/std/thread/index.html#stack-size
[go-maxstack]: https://pkg.go.dev/runtime/debug#SetMaxStack
[go-vardecl]: https://go.dev/ref/spec#Variable_declarations

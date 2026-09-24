# Programs, output and the driver

These records settle how a Vortex program starts and ends: the rules for
`main`, what `print` writes, how a failed runtime check is reported, and how
the `vortex` command is run. The specification chapters now state the language
rules and the compiler guide states the command-line rules; each record below
explains why the rule was chosen and what it changed.

## 4. What `print` writes {#d4}

**Status:** Accepted, 2026-09-23. **Applies to:** v0.1. **Resolves:** none.
**Guide stage:** 4, 5, 6, 8.

**Question.** What may `print` take, and what exactly does it write?

**Before this decision.** [Types and values](../specification/types-and-values.md)
section 4.6 named `print` only among "supported functions", and
[stage 6](../compiler/guide/stage-6-first-machine-code.md) could not say what
`print(14)` writes.

**Options.**

- One argument per call.
- One or more arguments, separated by spaces.
- Leave the format to each implementation.

**Elsewhere.** Go's `print` and `println` [leave formatting to the
implementation][go-boot]. Swift's `print` [puts a space between items and ends
the line][sw-print] by default.

**Decision.** `print` is a built-in function declared in a scope around the
program. It takes one or more arguments, each of type `bool`, `char`, `i32`,
`u32`, `usize`, `f32`, `f64` or `String`, and returns `void`. Zero arguments,
or any other argument type (such as an array, a struct or `&x`), is a type
error. Arguments are evaluated left to right, then written to standard output
with one space between them and a line feed after the last.

- Integers are written in decimal, `bool` as `true` or `false`, and `char`
  and `String` as their UTF-8 bytes, without quotes.
- A float is written as the shortest decimal (the closest, if several) that
  reads back as the same value of its type: positional when its decimal
  exponent is from -4 to 15 (`16.0` has exponent 1), exponent form otherwise
  (`1.0e16`, `2.5e-7`), always with a digit after the point. Zero is `0.0` or
  `-0.0`; the special values are `NaN`, `inf` and `-inf`.

**Why.** Output tests compare bytes, so the format cannot be left open.
Spaced arguments keep the tour's `print("rows:", rows)` valid without adding
string formatting.

**Consequences.** Stage 4 resolves `print`; declaring that name is a name error
(records [2](names.md#d2) and [5](names.md#d5)). Stage 5 checks the
arguments; a literal argument keeps its default type
([record 31](numbers.md#d31)). Stages 6 and 8 add the output. Comments show
the line written:

```vortex
// statements: valid
print("rows:", 128);         // rows: 128
print(16.0, true, 1.0e20);   // 16.0 true 1.0e20
```

Pages changed: [Programs and declarations](../specification/declarations.md)
3.9 (new), Types and values 4.6, the tour, guide stages 4, 6, 8, 9 and 11.

## 6. The `main` check {#d6}

**Status:** Accepted, 2026-09-23. **Applies to:** v0.1. **Resolves:** N23 (the
`main` half), N32. **Guide stage:** 4.

**Question.** Which stage checks the entry point, what category does each
mistake get, and may a program call `main`?

**Before this decision.** [Programs and declarations](../specification/declarations.md#33-entry-point)
section 3.3 rejected a bad `main` "during semantic analysis". The guide put
the check in [stage 4](../compiler/guide/stage-4-names-and-scopes.md#exactly-one-main),
but [stage 3](../compiler/guide/stage-3-parser-and-tree.md) said stage 5. A
second `fn main` could be reported twice, and no page said whether `main` may
be called.

**Options.**

- Category: name error or semantic error.
- Stage: 4 (names) or 5 (types).
- Calls to `main`: allowed or forbidden.

**Elsewhere.** Rust gives a missing `main` [its own error, E0601][rs-e0601].
C++ [forbids calling `main` or taking its address][cpp-main]. Go's `main`
[takes no arguments and returns no value][go-progexec].

**Decision.** Name resolution checks the entry point once it has collected the
top-level names; the parser never does.

- A program with no function named `main` is a semantic error, reported at the
  start of the file (line 1, column 1).
- A `main` with parameters, or with a written return type other than `void`, is
  one semantic error at that declaration.
- A second function named `main` is reported once, as the usual duplicate
  declaration (a name error) at the second one, and causes no entry-point
  error.
- In every other way `main` is an ordinary function: other functions may call
  it, and it may call itself.

**Why.** Each check reads only names and written signatures, so it needs no
types and fits stage 4, where the roadmap puts it. One report per mistake keeps
test expectations exact. Forbidding calls to `main` would add a rule without
preventing a real error.

**Consequences.** Stage 4 reports all three mistakes, and stage 5 has no
`main` check.

```vortex
// program: name error
fn main() { }
fn main() { }   // name error: a second function named `main`
```

Pages changed: Programs and declarations 3.3, Diagnostics 10.2, the grammar's
program structure, the tour's hello-world chapter, and guide stages 3 and 4.

## 14. Exit status and runtime error output {#d14}

**Status:** Accepted, 2026-09-23. **Applies to:** v0.1. **Resolves:** N34.
**Guide stage:** 6, 9.

**Question.** What exit status (the number a finished program hands back) does
a compiled program return, and what happens when a runtime check fails?

**Before this decision.** [Diagnostics](../specification/diagnostics.md#106-runtime-reporting)
section 10.6 said a runtime failure must stop the program and name its
category, but fixed no exit status, stream, format or flushing. Stages
[6](../compiler/guide/stage-6-first-machine-code.md) and
[9](../compiler/guide/stage-9-runtime-safety.md#stopping-with-a-clear-runtime-error)
left all four open.

**Options.**

- Failure status: 1, 101, or implementation-defined.
- Error stream: standard output, or standard error (a second stream kept apart
  from normal output).
- Pending output: written first, or possibly lost.

**Elsewhere.** C names the two outcomes [`EXIT_SUCCESS` and
`EXIT_FAILURE`][cpp-exit]. Rust [exits with 101][rs-exit101] when a panic ends
the program.

**Decision.** A program whose `main` returns exits with status 0. When a
required runtime check fails, the program must write out everything it has
printed so far, then write exactly one line to standard error, then exit with
status 101. Nothing else runs.

```text
runtime error[<kind>]: <message> at <file>:<line>:<column>
```

`<kind>` is `bounds`, `divide-by-zero`, `overflow`, `shift` or `cast` for the
[runtime checks](../specification/diagnostics.md#runtime-error), or `stack` for
stack exhaustion ([record 46](diagnostics.md#d46)). `<file>` is the source path
as given to the compiler, and `<line>:<column>` is where the failing
operation's span starts ([record 16](diagnostics.md#d16)); when no position is
known, ` at ...` is left out. `<message>` is implementation-defined
([I7](implementation.md#i7)).

**Why.** A test can tell a normal finish (0), a rejected program (the
compiler's 1, [record 20](#d20)) and a failed run (101) apart without reading
any text. Writing pending output first keeps the order the program asked for.

**Consequences.** Stage 6 exits with 0; stage 9 gives every check its kind and
source position.

```vortex
// program: runtime error
fn pick(values: [i32; 3], index: usize) -> i32 {
    return values[index];   // runtime error: index 3 is out of bounds
}

fn main() {
    print("start");
    print(pick([10, 20, 30], 3));
}
```

Saved as `pick.vx`, it writes `start`, then
`runtime error[bounds]: ... at pick.vx:3:12`, and exits with 101. Pages
changed: Diagnostics 10.6, Conformance 1.3, the tour, guide stages 6, 9 and
11, and the roadmap.

## 20. The `vortex` command line {#d20}

**Status:** Accepted, 2026-09-23. **Applies to:** v0.1. **Resolves:** none.
**Guide stage:** 0, 1, 2, 3, 6.

**Question.** What arguments does the `vortex` command take, where do its
messages go, and what exit statuses does it return?

**Before this decision.** The [roadmap](../roadmap.md#milestone-0-project-foundation)
asked for a command that "can accept a source-file path", but no page gave its
options, exit statuses or diagnostic stream.
[Stage 0](../compiler/guide/stage-0-workbench.md#what-you-need-to-have)
suggested only "zero for success and non-zero for any error".

**Options.**

- One failure status, or separate ones for a rejected program and a bad
  command.
- Token and tree printouts as separate tools, or as options of `vortex`.

**Elsewhere.** `rustc` [names its output with `-o` and asks for extra outputs
with `--emit`][rs-cli]; [`go build`][go-build] also takes `-o`.

**Decision.**

```text
vortex <source> [-o <output> | --tokens | --ast]
```

- `<source>` is the path of one source file; the `.vx` extension is
  conventional, not required.
- `-o` names the executable. Without it, the executable takes the source file's
  name minus its extension, in the current directory; if that would overwrite
  the source, it is a usage error.
- `--tokens` stops after lexing and `--ast` after parsing; each prints to
  standard output and writes no executable.
- Diagnostics go to standard error. A successful compile prints nothing.
- The exit status is 0 on success, 1 when the source has any error, and 2 for a
  usage error (with a short usage message) or a failure outside the source,
  such as an unreadable file. After any error, no output file is created or
  changed.

**Why.** A test harness can judge every run from the exit status and the two
streams alone, and a mistyped command is never confused with a rejected
program.

**Consequences.** Stage 0 handles arguments and exit statuses; stage 1 gives
an unreadable file status 2; stages 2 and 3 add `--tokens` and `--ast`, whose
formats are implementation choices ([I4](implementation.md#i4),
[I5](implementation.md#i5)); stage 6 adds `-o`. The diagnostic layout is
[I2](implementation.md#i2). Pages changed: the roadmap (Milestones 0 and 11)
and guide stages 0, 1, 2, 3, 6 and 11.

[rs-cli]: https://doc.rust-lang.org/rustc/command-line-arguments.html
[rs-e0601]: https://doc.rust-lang.org/error_codes/E0601.html
[rs-exit101]: https://rust-cli.github.io/book/in-depth/exit-code.html
[go-boot]: https://go.dev/ref/spec#Bootstrapping
[go-progexec]: https://go.dev/ref/spec#Program_execution
[go-build]: https://pkg.go.dev/cmd/go#hdr-Compile_packages_and_dependencies
[cpp-main]: https://en.cppreference.com/cpp/language/main_function
[cpp-exit]: https://en.cppreference.com/cpp/utility/program/EXIT_status
[sw-print]: https://docs.swift.org/swift-book/documentation/the-swift-programming-language/thebasics#Printing-Constants-and-Variables

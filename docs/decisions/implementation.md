# Implementation choices

These records cover choices that the [compiler guide](../compiler/guide/index.md)
leaves to the implementer: the back end, the layout of diagnostics and of the
token and tree printouts, what tests compare, the memory layout document,
runtime error messages, where runtime checks live, and how the test suite
handles unfinished work and documentation examples. None of them is a language
rule. The specification chapters state the rules; each record here suggests a
default that fits those rules and explains why, and an implementer may choose
differently as long as the choice is written down.

## I1. Back end and target {#i1}

**Status:** Suggested default. The implementer decides; this is not a language
rule. **Guide stage:** 6, 10.

**Question.** Which back end (the part of the compiler that turns the checked
program into machine code) should v0.1 use: generated LLVM IR (the input
language of the LLVM toolkit), generated C, or assembly written directly? And
which processors and operating systems should it support?

**Suggested default.** No back end is preferred: the owner chooses after
weighing the costs that stage 6 lists. Whatever the choice, its written
decision should show that the compiler:

- computes each `f32` and `f64` operation as one IEEE 754 operation (the
  standard for floating-point arithmetic), with contraction (fusing a multiply
  and an add into one instruction) and fast-math options (settings that let a
  compiler reorder floating-point arithmetic) turned off, for example with
  `-ffp-contract=off` for a C compiler ([record 56](numbers.md#d56));
- produces programs that exit with status 0 when `main` returns, and with
  status 101 after writing the runtime error line when a check fails
  ([record 14](program.md#d14));
- supports only 64-bit targets, so `usize` is 64 bits
  ([record 42](numbers.md#d42));
- still builds and tests with Milestone 0's one command from a fresh copy of
  the project, with every outside tool it needs named;
- lists its targets and back end among the implementation-defined behaviors
  ([record 55](documentation.md#d55)).

**Why.** A program cannot tell which back end compiled it, so the language
constrains only what a test can observe: the digits a program prints, its exit
status and the width of `usize`. Contraction is the constraint most often broken
by accident: by default [GCC fuses outside strict ISO modes][gcc-fpcontract]
and [Clang fuses within an expression][clang-fpcontract], and either would
change the last bits of the stage 10 sums. The rest of the choice weighs
dependencies against what the implementer wants to learn, which only the owner
can judge.

**Where the guide uses it.**
[Stage 6](../compiler/guide/stage-6-first-machine-code.md#choosing-a-back-end)
compares the options and lists what the decision must settle.
[Stage 10](../compiler/guide/stage-10-matrix-multiplication.md#comparing-with-a-known-answer)
relies on the floating-point settings for its known answers.

## I2. Diagnostic format {#i2}

**Status:** Suggested default. The implementer decides; this is not a language
rule. **Guide stage:** 1.

**Question.** How is a diagnostic (the compiler's report of a problem in the
source) laid out: in what order are its parts, how are its position and
category written, and how do notes appear?

**Suggested default.** A header line, then the source line as written, with a
marker (a row of `^`) under the primary span, the stretch of source the
diagnostic is about:

```text
scale.vx:3:5: error[semantic]: cannot assign to immutable variable `value`
    3 |     value = 20;
      |     ^^^^^
scale.vx:2:9: note: `value` is declared here without `mut`
```

- The header is `<file>:<line>:<column>: error[<category>]: <message>`: the
  path as given to the compiler, the position counted as
  [record 16](diagnostics.md#d16) says, and one of `lexical`, `syntax`, `name`,
  `type`, `semantic`, `constant-evaluation` or `implementation-limit`.
- A note is a line `<file>:<line>:<column>: note: <message>`, and may quote its
  own line.
- Messages are lowercase, end without a full stop, and put code in backticks.
- A marker is at least one `^` wide. A span over several lines is marked on
  its first line. Tabs before the span are copied into the marker line, so the
  marker lines up.
- Diagnostics go to standard error ([record 20](program.md#d20)), stage by
  stage, and in source order within a stage.
- A problem outside the source has no position:
  `vortex: error: cannot read missing.vx`.

**Why.** A header that starts with `file:line:column:` is the layout Clang
uses, as
[stage 1](../compiler/guide/stage-1-source-and-diagnostics.md#how-others-teach-this-stage)
notes, and one pattern then finds every error in the output ([I3](#i3)). The
bracketed category matches the runtime error line of
[record 14](program.md#d14). The quoted line is what
[Milestone 1](../roadmap.md#milestone-1-source-files-and-diagnostics) asks for,
and the message style follows the Rust compiler's guide. Wording, notes and
recovery remain implementation-defined ([record 55](documentation.md#d55)).

**Where the guide uses it.**
[Stage 1](../compiler/guide/stage-1-source-and-diagnostics.md#one-format-for-every-diagnostic)
builds the format and
[shows the source line](../compiler/guide/stage-1-source-and-diagnostics.md#showing-the-source-line);
every later stage reports through it.

## I3. What invalid-program tests compare {#i3}

**Status:** Suggested default. The implementer decides; this is not a language
rule. **Guide stage:** 0, 11.

**Question.** When a test expects the compiler to reject a program, what must
match: the whole message, or only parts of it?

**Suggested default.** The exit status, and for every error, in order, its
category and the position where its primary span starts; never the message
text.

- The expected file lists one line per error, such as `type 2:23` for a type
  error whose primary span starts at line 2, column 23. A program written to
  break one rule expects one line, so an extra report, such as a cascade (a
  false follow-on error, [I11](#i11)), fails the test.
- The runner reads only the header lines of [I2](#i2). Notes, quoted lines,
  markers and warnings ([record 48](diagnostics.md#d48)) are ignored.
- A rejected program must end with exit status 1 ([record 20](program.md#d20)).
- A runtime-error test compares what the program printed before the failure,
  the kind and position in the runtime error line, and exit status 101
  ([record 14](program.md#d14)), but not the message ([I7](#i7)).

**Why.** The [Diagnostics](../specification/diagnostics.md) chapter treats
exact prose as an implementation detail, and
[record 55](documentation.md#d55) lists wording as implementation-defined, so a
test that froze every word would fail each time a message improved while
checking nothing the specification requires. The category and the primary
span are what
[Diagnostics 10.7](../specification/diagnostics.md#107-verification-requirements)
asks each test to check. Comparing every error, not only the first, catches a
mistake reported twice and a false follow-on error. rustdoc draws a similar
line: a [`compile_fail`][rs-doctest] block must fail to compile, whatever the
message says. Wording is still reviewed, by reading the output.

**Where the guide uses it.**
[Stage 0](../compiler/guide/stage-0-workbench.md#what-goes-in-a-test-case)
asks for this decision, and
[stage 11](../compiler/guide/stage-11-release.md#valid-and-invalid-pairs)
checks every rejection this way at the release gate.

## I4. Token printout {#i4}

**Status:** Suggested default. The implementer decides; this is not a language
rule. **Guide stage:** 2.

**Question.** What does `vortex --tokens` print, so that lexer tests can
compare its output with an expected file?

**Suggested default.** One token per line, in source order, ending with the
end-of-file token. For `let total = 0b101;`:

```text
1:1 KEYWORD 'let'
1:5 IDENTIFIER 'total'
1:11 OPERATOR '='
1:13 INTEGER '0b101'
1:18 PUNCTUATION ';'
1:19 EOF ''
```

- Each line is `<line>:<column> <KIND> '<spelling>'`, where the position is the
  token's first character ([record 16](diagnostics.md#d16)).
- `KIND` is one fixed word per row of the stage 2 table: `IDENTIFIER`,
  `KEYWORD`, `INTEGER`, `FLOAT`, `BOOL`, `CHAR`, `STRING`, `OPERATOR`,
  `PUNCTUATION` or `EOF`. `true` and `false` print as `KEYWORD` or `BOOL`,
  whichever the lexer produces ([record 18](lexical.md#d18)).
- The spelling is exactly as written, escapes included, and is not escaped
  itself: it runs from the first `'` after the kind to the last `'` on the
  line, which is safe because no token contains a line break. So `'A'` prints
  as `''A''`.
- Lexical errors go to standard error in the [I2](#i2) format.

**Why.** Kind, spelling and position are what
[Lexical structure 2.8](../specification/lexical-structure.md#28-token-source-data)
says every token keeps, and what
[Milestone 2](../roadmap.md#milestone-2-lexer) asks the printout to show. One
line per token makes an expected file quick to write by hand and a difference
quick to find in a line-by-line comparison. Printing the spelling rather than
a decoded value shows what the lexer kept, so a lost `0b` or a decoded `\n` is
visible.

**Where the guide uses it.**
[Stage 2](../compiler/guide/stage-2-lexer.md#from-characters-to-tokens) adds
the printout, and every lexer test from then on compares against it.

## I5. Tree printout {#i5}

**Status:** Suggested default. The implementer decides; this is not a language
rule. **Guide stage:** 3.

**Question.** What does `vortex --ast` print, so that parser tests can compare
the syntax tree with an expected file?

**Suggested default.** An indented S-expression, the nested list notation of
Lisp. Each node is a list in parentheses that starts with its kind, then what
the node stores and `@line:column`, where its span starts. Each child follows
on its own line, indented two more spaces. For `let result = 2 + 3 * 4;`,
written as line 2 of `main` with four spaces of indentation:

```text
(let result @2:5
  (binary + @2:18
    (int 2 @2:18)
    (binary * @2:22
      (int 3 @2:22)
      (int 4 @2:26))))
```

- Kinds are short lowercase words. What a node stores is its name, its
  operator, `mut` when it was written, and a literal's spelling.
- Children appear in source order. Nothing printed depends on memory addresses
  or on the order of an unordered collection, so the same input always prints
  the same text.

**Why.** Stage 3 asks for each node's kind, its details, its children in
order and its position, and for output that never changes between runs. This
form shows all of them in plain ASCII with one node per line, so a change in
the parser shows up as a few changed lines. One short recursive function
prints it, and the parentheses mark exactly where each node ends. It is also
the notation of the printer that *Crafting Interpreters* builds, which stage 3
cites. Checking that `2 + 3 * 4` puts `*` under `+` takes two lines of reading.

**Where the guide uses it.**
[Stage 3](../compiler/guide/stage-3-parser-and-tree.md#printing-the-tree) adds
the printer, and every parser test from then on compares against it.

## I6. Layout document {#i6}

**Status:** Suggested default. The implementer decides; this is not a language
rule. **Guide stage:** 8.

**Question.** What goes in the layout document that
[Milestone 8](../roadmap.md#milestone-8-strings-arrays-structs-and-references)
asks for, now that [record 43](arrays.md#d43) fixes the order of array elements
and struct fields?

**Suggested default.** One page in the compiler documentation. It gives every
v0.1 type a size and an alignment (the number of bytes a value's address must
be a multiple of), and records these answers:

- `i32`, `u32` and `f32` take 4 bytes; `f64` and `usize` take 8
  ([record 42](numbers.md#d42)); `bool` takes 1 byte holding 0 or 1; `char`
  takes 4 bytes holding one Unicode scalar value, that is, one character
  ([record 15](lexical.md#d15)). Each is aligned to its own size.
- An array is contiguous and row-major, with the last index varying fastest
  ([record 43](arrays.md#d43)), and aligned like its element type. The
  document states the largest total size accepted.
- A struct keeps its fields in declaration order, each at the next offset its
  alignment allows. The struct takes its largest field's alignment, and its
  size is rounded up to a multiple of it, so array elements stay aligned.
- A `String` is the address of its UTF-8 bytes and a length in bytes. Every
  count the runtime keeps is in bytes, never in characters.
- A reference is the address of the storage it refers to, 8 bytes.
- An array or struct passed by value is either copied or passed by a hidden
  address; the document says which. Both keep the copy behavior of
  [record 25](references.md#d25).

**Why.** [Arrays 7.8](../specification/arrays.md#78-memory-and-layout) and
[Structs 8.8](../specification/structs.md#88-layout) ask for one documented
layout, applied consistently, and [record 55](documentation.md#d55) lists
padding and alignment as implementation-defined, so each entry needs a written
answer a reader can check against the tests. Aligning each value to its
own size is the usual choice on 64-bit targets. A stored byte length lets
`print` write a string without searching for an end marker, and settles the
question stage 8 raises about `λ`: one character, stored in two bytes.

**Where the guide uses it.**
[Stage 8](../compiler/guide/stage-8-data-in-memory.md#memory-addresses-and-layout)
writes the document, and
[stage 11](../compiler/guide/stage-11-release.md#writing-down-what-the-compiler-does)
links it from the compiler documentation.

## I7. Runtime error message {#i7}

**Status:** Suggested default. The implementer decides; this is not a language
rule. **Guide stage:** 9.

**Question.** [Record 14](program.md#d14) fixes the line that a failed runtime
check writes, `runtime error[<kind>]: <message> at <file>:<line>:<column>`, but
leaves `<message>` implementation-defined. What should it say?

**Suggested default.** A short phrase in the style of the compile-time
messages ([I2](#i2)): lowercase, with no final full stop, naming what failed
and, when the check has them, the values involved.

| Kind | Message |
| --- | --- |
| `bounds` | `index 3 is out of bounds for extent 3`, adding `in dimension 2` when the array has more than one dimension |
| `divide-by-zero` | `division by zero`, or `remainder by zero` |
| `overflow` | `2147483647 + 1 does not fit in i32` |
| `shift` | `shift count 32 is out of range for i32` |
| `cast` | `-1 does not fit in u32`, or `NaN does not fit in i32` |
| `stack` | `stack exhausted` |

The example in record 14 then reads:

```text
runtime error[bounds]: index 3 is out of bounds for extent 3 at pick.vx:3:12
```

**Why.** The kind and the position already tell a test what failed and where
([I3](#i3)), so the message is written for the person reading it, and the
values are what that person needs next: which index, and how large the array
was. Using the style of the compile-time messages makes both kinds of report
read alike. Because the text is implementation-defined
([record 55](documentation.md#d55)) and no test compares it, it can improve
later without breaking anything.

**Where the guide uses it.**
[Stage 9](../compiler/guide/stage-9-runtime-safety.md#stopping-with-a-clear-runtime-error)
writes the messages, and
[stage 11](../compiler/guide/stage-11-release.md#writing-down-what-the-compiler-does)
documents them.

## I8. Where checking code lives {#i8}

**Status:** Suggested default. The implementer decides; this is not a language
rule. **Guide stage:** 6, 9.

**Question.** Should each runtime check be generated in place, immediately
before the operation it guards, or should the generated code call safe
versions of the operations kept in the runtime library?

**Suggested default.** Generate each check inline: a comparison and a branch
before the operation. On failure, the branch calls one runtime reporting
function with the check's kind, its source position and the values the message
shows ([I7](#i7)). That function is the only code that writes a runtime error:
it writes out pending standard output, writes the one line to standard error,
and exits with status 101 ([record 14](program.md#d14)). Stack exhaustion
([record 46](diagnostics.md#d46)), however it is detected, ends in the same
function.

**Why.**

- The compiler knows each check's kind and position when it generates the
  check, so both travel as constants, and every report points at the right
  operation.
- With one reporting function, record 14's order (pending output, one line,
  exit) is written once and tested once.
- The check stays visible in the generated code, where a later optimizer can
  remove it once it has proved the operation safe; a call into the runtime
  would hide the operation from it.
- The check comes first because the back end may give the unchecked operation
  no meaning: C++ leaves
  [signed overflow and division by zero undefined][cpp-arith], and stage 9
  notes the same for LLVM.

The alternative that stage 9 describes, safe operations in the runtime, also
conforms; its source, Ghuloum, judges it slower but simpler.

**Where the guide uses it.**
[Stage 9](../compiler/guide/stage-9-runtime-safety.md#stopping-with-a-clear-runtime-error)
places the checks and
[explains when one may be removed](../compiler/guide/stage-9-runtime-safety.md#when-a-check-can-be-skipped).
[Stage 6](../compiler/guide/stage-6-first-machine-code.md#the-small-runtime)
starts the runtime library that holds the reporting function.

## I9. Expected-failure marking {#i9}

**Status:** Suggested default. The implementer decides; this is not a language
rule. **Guide stage:** 0, 11.

**Question.** How does the test suite hold a test for v0.1 behavior that the
compiler does not handle yet, without its failure turning into noise?

**Suggested default.** Such behavior is "Specified, not yet implemented"
([record 50](documentation.md#d50)).

- Its test keeps the specification's expected result and gains an
  expected-failure mark in its own expected file, naming the stage that will
  make it pass, such as `xfail: stage 8`. Adding or removing a mark never means
  editing the runner.
- The runner reports a marked test that fails as XFAIL (an expected failure),
  which does not fail the run, and a marked test that passes as XPASS (an
  unexpected pass), which does fail it until the mark is removed.
- Meanwhile the compiler rejects the construct with an
  [implementation-limit error](../specification/diagnostics.md#implementation-limit-error),
  never a crash.
- Planned examples are never marked: a v0.1 compiler must reject them, so
  their tests pass today.
- At the release gate, every mark still present is listed as a known
  limitation (something the specification describes that the compiler does not
  do yet).

**Why.** Writing tests early, as stage 0 suggests, helps only if the report
stays honest. An unmarked failure hides new failures among old ones, and a
test left out until later is soon forgotten. Failing the run on XPASS turns
"this works now" into an event, so the mark and the list of limitations change
together with the feature. The two names come from LLVM's lit test runner,
which stage 0 describes.

**Where the guide uses it.**
[Stage 0](../compiler/guide/stage-0-workbench.md#a-runner-that-has-failed-at-least-once)
chooses how to hold tests for later stages, and
[stage 11](../compiler/guide/stage-11-release.md#traps) keeps known failures
from turning into noise.

## I10. Keeping documentation examples in sync {#i10}

**Status:** Suggested default. The implementer decides; this is not a language
rule. **Guide stage:** 0, 11.

**Question.** How do the `vortex` examples in the documentation become tests,
so that an edited example cannot drift away from what the compiler does?

**Suggested default.** A checker (a small script run with the other tests)
reads the examples straight from the documentation; nothing is copied.

- It finds every `vortex` block and reads the label on its first line
  ([record 28](documentation.md#d28)). A block without a label is an error.
- It completes the block by its kind: a `statements` block is wrapped in
  `fn main() { ... }`, an `items` block without `main` gets `fn main() {}`
  appended, a `program` block is used as written, and a `fragment` is skipped.
- It runs `vortex` on the result and checks the label. `valid` needs exit
  status 0; the program is not run. An error label needs status 1 and a first
  error of that category. `runtime error` needs a compiled program that ends
  with status 101 and a `runtime error[` line. `planned` needs status 1, with
  any category ([record 50](documentation.md#d50)).
- Any other block that draws an implementation-limit error counts as an
  expected failure ([I9](#i9)); at the release gate, each one left is a known
  limitation.

**Why.** Readers learn the rules from these examples, so a wrong example does
the most harm there. Testing each example where it lives leaves no copy to
drift, and every edit is retested on the next run. rustdoc
[tests the examples in Rust documentation the same way][rs-doctest], wrapping
a block that has no `fn main`. Only the first error is compared because
recovery after an error is implementation-defined
([record 55](documentation.md#d55)), and [I2](#i2) prints errors stage by
stage, so an error always comes before anything it causes in a later stage.

**Where the guide uses it.**
[Stage 0](../compiler/guide/stage-0-workbench.md#why-the-tests-come-first)
turns examples into tests, and
[stage 11](../compiler/guide/stage-11-release.md#compiling-every-example-in-the-documentation)
compiles every example at the release gate.

## I11. Cascades from unparsed declarations {#i11}

**Status:** Suggested default. The implementer decides; this is not a language
rule. **Guide stage:** 3, 4, 5.

**Question.** When the parser could not build a declaration, how do later
stages avoid a cascade (a burst of false errors caused by one real mistake),
such as reporting every use of the declared name as unknown?

**Suggested default.** Mark the broken declaration as poisoned (known to be
broken, and already reported), and report nothing that follows from it.

- If the parser read the declaration's name before the error, as in
  `let total = 4 +;`, it still records the declaration, marked as poisoned.
- Name resolution links each use of a poisoned name to it and reports no name
  error.
- Type checking gives a poisoned name an unknown type that accepts every use,
  so it reports no type error either. A value computed from it is unknown too.
- Every other error is reported as usual.

```vortex
// statements: syntax error
let total = 4 +;           // syntax error: expected an expression after `+`
let doubled = total * 2;   // no second error: `total` is poisoned
```

**Why.** The user sees the real mistake first, as stage 4 asks, while
independent errors elsewhere in the file are still found. Stopping after the
first stage that reports an error also prevents cascades, and is simpler, but
it hides every name and type error until the syntax error is fixed. Poisoning
cannot help when recovery skipped a name entirely, as in stage 3's example of a
missing `;`. A false unknown name is still possible there, which is one reason
recovery should stay quiet when it is unsure.

**Where the guide uses it.**
[Stage 3](../compiler/guide/stage-3-parser-and-tree.md#when-the-source-is-wrong)
recovers from the syntax error and records the poisoned declaration.
[Stage 4](../compiler/guide/stage-4-names-and-scopes.md#traps) and
[stage 5](../compiler/guide/stage-5-types-and-rules.md#traps) report nothing
about it.

[gcc-fpcontract]: https://gcc.gnu.org/onlinedocs/gcc/Optimize-Options.html#index-ffp-contract
[clang-fpcontract]: https://clang.llvm.org/docs/UsersManual.html#cmdoption-ffp-contract
[rs-doctest]: https://doc.rust-lang.org/rustdoc/write-documentation/documentation-tests.html
[cpp-arith]: https://en.cppreference.com/cpp/language/operator_arithmetic

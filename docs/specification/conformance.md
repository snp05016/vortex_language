# 1. Conformance and terminology

This chapter defines how to interpret the Vortex specification and what it
means for a source program or implementation to conform to v0.1.

## 1.1 Document authority

<a class="vx-rule" id="conform.authority.sole-statement" href="#conform.authority.sole-statement">conform.authority.sole-statement</a> This section is the only statement of which page decides a question about
Vortex v0.1. Other pages link here instead of stating their own order.

<a class="vx-rule" id="conform.authority.normative-chapters" href="#conform.authority.normative-chapters">conform.authority.normative-chapters</a> The specification chapters are **normative**: they state the rules. They are
this chapter and the other chapters listed in the
[specification index](index.md), including the [formal grammar](grammar.md)
and the [glossary](glossary.md). <a class="vx-rule" id="conform.authority.informative-pages" href="#conform.authority.informative-pages">conform.authority.informative-pages</a> Every other page is **informative**: the
language tour, the compiler guide and compiler internals, the cheat sheet, the
philosophy, the roadmap, the decision records and the site's front pages
explain the rules and must not add to them or change them.

<a class="vx-rule" id="conform.authority.conflict-resolution" href="#conform.authority.conflict-resolution">conform.authority.conflict-resolution</a> When an informative page disagrees with a normative one, the normative page is
correct. When two normative statements disagree, the grammar's productions
decide which token sequences are syntactically valid, and the other chapters
decide everything else. Either kind of disagreement is a documentation defect:
report it so that it can be fixed.

<a class="vx-rule" id="conform.authority.compiler-not-normative" href="#conform.authority.compiler-not-normative">conform.authority.compiler-not-normative</a> Current compiler behavior is evidence of implementation status, not a rule.

<a class="vx-rule" id="conform.authority.no-silent-fix" href="#conform.authority.no-silent-fix">conform.authority.no-silent-fix</a> An inconsistency between the implementation and the specification is either an
implementation defect or an explicit proposal to revise the specification. It
must not be resolved silently.

The reasons are in [record 49](../decisions/documentation.md#d49).

## 1.2 Programs

<a class="vx-rule" id="conform.programs.source-file" href="#conform.programs.source-file">conform.programs.source-file</a> A **source file** is a sequence of bytes provided to the compiler. It must be
valid UTF-8, and its source characters are the Unicode scalar values those
bytes encode ([Lexical structure 2.1](lexical-structure.md#source-encoding),
[decision 15](../decisions/lexical.md#d15)). <a class="vx-rule" id="conform.programs.definition" href="#conform.programs.definition">conform.programs.definition</a> A **program** is the collection
of top-level declarations parsed from that source file.

<a class="vx-rule" id="conform.programs.well-formed" href="#conform.programs.well-formed">conform.programs.well-formed</a> A program is **well-formed** when it satisfies all applicable lexical,
syntactic, name, type, semantic, and constant-evaluation rules in this
specification.

<a class="vx-rule" id="conform.programs.entry-point" href="#conform.programs.entry-point">conform.programs.entry-point</a> An executable Vortex program additionally must contain exactly one function
named `main` with no parameters and a `void` return type. An empty source file
is grammatically valid but does not satisfy the executable entry-point rule.

## 1.3 Implementation conformance

A conforming Vortex v0.1 implementation must:

- <a class="vx-rule" id="conform.impl.accept-well-formed" href="#conform.impl.accept-well-formed">conform.impl.accept-well-formed</a> accept every well-formed v0.1 program within its documented implementation
  limits;
- <a class="vx-rule" id="conform.impl.reject-ill-formed" href="#conform.impl.reject-ill-formed">conform.impl.reject-ill-formed</a> reject every ill-formed program rather than silently changing its meaning;
- <a class="vx-rule" id="conform.impl.preserve-behavior" href="#conform.impl.preserve-behavior">conform.impl.preserve-behavior</a> preserve the specified observable behavior of accepted programs;
- <a class="vx-rule" id="conform.impl.diagnostic-location" href="#conform.impl.diagnostic-location">conform.impl.diagnostic-location</a> issue a diagnostic with a source location for each detected source error;
- <a class="vx-rule" id="conform.impl.no-planned-syntax" href="#conform.impl.no-planned-syntax">conform.impl.no-planned-syntax</a> avoid accepting planned syntax as though it were standardized v0.1 syntax;
- <a class="vx-rule" id="conform.impl.document-limits" href="#conform.impl.document-limits">conform.impl.document-limits</a> document any implementation limit that is narrower than the language design
  (see [1.10](#110-implementation-defined-behavior-and-limits)).

<a class="vx-rule" id="conform.impl.observable-behavior" href="#conform.impl.observable-behavior">conform.impl.observable-behavior</a> The observable behavior of a program is what it writes to standard output, the
runtime error line it writes to standard error, and its exit status
([Diagnostics 10.6](diagnostics.md#106-runtime-reporting);
[decision 14](../decisions/program.md#d14)).

<a class="vx-rule" id="conform.impl.incomplete-compiler" href="#conform.impl.incomplete-compiler">conform.impl.incomplete-compiler</a> A compiler under development may be incomplete. It should report an explicit
"not implemented" diagnostic when it recognizes specified syntax that it
cannot yet lower. Crashing or silently omitting the construct is not conforming
behavior.

## 1.4 Static and dynamic rules

<a class="vx-rule" id="conform.rules.static-defined" href="#conform.rules.static-defined">conform.rules.static-defined</a> A **static rule** can be checked before execution. Syntax, name visibility,
types, mutability, and fixed array extent requirements are static rules.

<a class="vx-rule" id="conform.rules.dynamic-defined" href="#conform.rules.dynamic-defined">conform.rules.dynamic-defined</a> A **dynamic rule** concerns values only known during execution. Examples
include an index that is out of bounds for a particular runtime value or
integer division by zero when the divisor is not constant.

<a class="vx-rule" id="conform.rules.constant-eval-timing" href="#conform.rules.constant-eval-timing">conform.rules.constant-eval-timing</a> A dynamic rule is checked during compilation when every operand that decides
it is an integer constant expression; a failure is then a constant-evaluation
error ([Expressions 5.12](expressions.md#512-constant-expressions)). Every
other dynamic rule is checked at run time. <a class="vx-rule" id="conform.rules.no-preemptive-rejection" href="#conform.rules.no-preemptive-rejection">conform.rules.no-preemptive-rejection</a> An implementation must not reject a
program because it can prove that a run-time check will fail; it may warn. It
may omit a run-time check only when it proves that the check cannot fail.
Decision record: [Provable runtime failures](../decisions/diagnostics.md#d39).

## 1.5 Undefined behavior

<a class="vx-rule" id="conform.ub.no-undefined-behavior" href="#conform.ub.no-undefined-behavior">conform.ub.no-undefined-behavior</a> Vortex v0.1 does not intentionally expose undefined behavior to a well-formed
program. Operations with invalid dynamic conditions must either be rejected
statically or fail through the
[documented runtime diagnostic mechanism](diagnostics.md#106-runtime-reporting).
[1.4](#14-static-and-dynamic-rules) says which.

<a class="vx-rule" id="conform.ub.stack-exhaustion" href="#conform.ub.stack-exhaustion">conform.ub.stack-exhaustion</a> Exhausting the call stack, through deep recursion or large local values, is
covered by this rule. Execution must stop at that point with an
implementation-limit report
([Diagnostics 10.6](diagnostics.md#106-runtime-reporting)); it must not
continue, and the program must not end without a report. The implementation
documents its stack size ([1.6](#16-implementation-defined-behavior)). A
compiler must not reject a program because it might exhaust the stack.
Decision record: [Stack exhaustion](../decisions/diagnostics.md#d46).

<a class="vx-rule" id="conform.ub.goal-not-guarantee" href="#conform.ub.goal-not-guarantee">conform.ub.goal-not-guarantee</a> This rule does not promise that the current compiler is free from bugs. It
defines the language goal and the behavior later implementation work must
preserve.

## 1.6 Implementation-defined behavior

<a class="vx-rule" id="conform.impl-defined.permitted-scope" href="#conform.impl-defined.permitted-scope">conform.impl-defined.permitted-scope</a> Implementation-defined behavior is permitted only where this specification
explicitly allows it. The implementation must document the selected behavior
and apply it consistently. Section
[1.10](#110-implementation-defined-behavior-and-limits) lists every such
behavior and every implementation limit.

<a class="vx-rule" id="conform.impl-defined.choices-no-semantic-change" href="#conform.impl-defined.choices-no-semantic-change">conform.impl-defined.choices-no-semantic-change</a> Host architecture, target architecture, object-file format, optimization
strategy, register allocation, and internal AST representation are
implementation choices. They must not change the specified meaning of a
program.

## 1.7 Source locations

<a class="vx-rule" id="conform.locations.retained-spans" href="#conform.locations.retained-spans">conform.locations.retained-spans</a> Every token and AST node required by the implementation must retain enough
source information to identify its originating source span. Diagnostics should
report at least the source location and a plain description of the violated
rule.

<a class="vx-rule" id="conform.locations.span-definition" href="#conform.locations.span-definition">conform.locations.span-definition</a> A source span is a byte offset, the number of bytes that come before the span
in the source file, and a length in bytes. A span may be empty (length 0),
marking a point such as the place where a missing `;` belongs. The end of the
file is a position like any other: its offset is the length of the file in
bytes.

<a class="vx-rule" id="conform.locations.line-column-base" href="#conform.locations.line-column-base">conform.locations.line-column-base</a> A location shown to a person is a line and a column, both counted from 1:

- <a class="vx-rule" id="conform.locations.line-break-definition" href="#conform.locations.line-break-definition">conform.locations.line-break-definition</a> A line break is LF (U+000A), or CR (U+000D) immediately followed by LF;
  either form counts as one line break. A CR that is not immediately followed
  by LF is a lexical error
  ([Lexical structure 2.1](lexical-structure.md#21-whitespace-and-line-boundaries)).
- <a class="vx-rule" id="conform.locations.column-definition" href="#conform.locations.column-definition">conform.locations.column-definition</a> A column is one more than the number of characters (Unicode scalar values)
  between the start of the line and the position. A horizontal tab counts as
  one column, and so does each byte that is not part of valid UTF-8.
- <a class="vx-rule" id="conform.locations.bom-excluded" href="#conform.locations.bom-excluded">conform.locations.bom-excluded</a> A byte order mark at the start of the file counts in byte offsets but not in
  columns.

<a class="vx-rule" id="conform.locations.compound-span" href="#conform.locations.compound-span">conform.locations.compound-span</a> For a compound construct, the stored span should cover the complete construct.
For example, the location of `left + right` covers both operands and the
operator, while the child nodes retain their own narrower spans.

The decision record [Source positions](../decisions/diagnostics.md#d16)
explains these choices.

## 1.8 Specification examples

<a class="vx-rule" id="conform.examples.label-required" href="#conform.examples.label-required">conform.examples.label-required</a> Every `vortex` code block on this site is an example. Its first line is a
**label**: a comment that names what the block contains and what a conforming
v0.1 implementation must do with it.

```vortex
// statements: type error
let ready: bool = 10; // an integer literal cannot initialize a bool
```

<a class="vx-rule" id="conform.examples.label-form" href="#conform.examples.label-form">conform.examples.label-form</a> A label has the form `// <kind>: <result>`, except that a fragment's label is
`// fragment`. The kind says how a checker (a tool that compiles each example
as a test) completes the block:

| Kind | The block contains | How it is compiled |
| --- | --- | --- |
| `program` | A complete source file | As written |
| `items` | Top-level declarations only | With `fn main() {}` appended when the block declares no `main` |
| `statements` | Statements only | Inside `fn main() { ... }` |
| `fragment` | A type, an expression or a partial construct, shown for its syntax | Never compiled; a fragment has no result |

The result says what a conforming implementation must do with the completed
block:

| Result | Label group | Required outcome |
| --- | --- | --- |
| `valid` | Valid | Accept it. When run, it finishes without a runtime error. |
| `lexical error`, `syntax error` | Invalid syntax | Reject it while lexing or parsing, with a diagnostic of that category. |
| `name error`, `type error`, `semantic error`, `constant-evaluation error` | Static error | Parse it, then reject it before execution with a diagnostic of that category. |
| `runtime error` | Runtime error | Accept it. When run, it stops with a runtime error. |
| `planned` | Planned | Do not accept it: it shows a design direction that is not part of v0.1. Any error category is correct. |

<a class="vx-rule" id="conform.examples.label-group-definition" href="#conform.examples.label-group-definition">conform.examples.label-group-definition</a> A label group is a name used in prose, not a diagnostic category. "Static
error" covers the four categories that
[Diagnostics](diagnostics.md#102-categories) reports after parsing.

Every example follows these rules:

- <a class="vx-rule" id="conform.examples.no-mixed-forms" href="#conform.examples.no-mixed-forms">conform.examples.no-mixed-forms</a> A block must not mix top-level declarations with top-level statements.
- <a class="vx-rule" id="conform.examples.valid-self-contained" href="#conform.examples.valid-self-contained">conform.examples.valid-self-contained</a> A `valid` block must be accepted exactly as the checker completes it, so it
  declares every name it uses. A block that relies on declarations shown
  elsewhere is a `fragment`.
- <a class="vx-rule" id="conform.examples.single-error-category" href="#conform.examples.single-error-category">conform.examples.single-error-category</a> A block with an error result must contain mistakes of that category only. A
  block that shows mistakes of different categories is split.
- <a class="vx-rule" id="conform.examples.whole-file-as-program" href="#conform.examples.whole-file-as-program">conform.examples.whole-file-as-program</a> A rule about a whole file, such as the ban on top-level statements or the
  need for `main`, is shown with a `program` block, because completing an
  `items` or `statements` block would change its result.
- <a class="vx-rule" id="conform.examples.practice-block-fragment" href="#conform.examples.practice-block-fragment">conform.examples.practice-block-fragment</a> A practice block whose lines have different answers is a `fragment`, so its
  label does not give the answers away.
- <a class="vx-rule" id="conform.examples.mistake-comment-allowed" href="#conform.examples.mistake-comment-allowed">conform.examples.mistake-comment-allowed</a> A comment on the line that holds a mistake may explain it.

The reasons are in [record 28](../decisions/documentation.md#d28),
[record 50](../decisions/documentation.md#d50) and
[record 51](../decisions/documentation.md#d51).

## 1.9 Change discipline

<a class="vx-rule" id="conform.change.complete-when-agreed" href="#conform.change.complete-when-agreed">conform.change.complete-when-agreed</a> A language change is complete only when the grammar, affected specification
chapters, valid and invalid examples, diagnostics, parser or AST design, and
tests agree. A code-only change does not revise the language specification.

## 1.10 Implementation-defined behavior and limits

<a class="vx-rule" id="conform.limits.exhaustive-list" href="#conform.limits.exhaustive-list">conform.limits.exhaustive-list</a> This list is complete. An implementation may choose only the behaviors and
limits listed here; it must document each choice and apply it consistently
(1.6). Every other observable behavior of a v0.1 program is fully specified.

| Entry | What the implementation documents | Rules |
| --- | --- | --- |
| `usize` width | The number of bits in `usize`, which also bounds array extents and index values; every v0.1 target uses 64 bits | [Types and values 4.3](types-and-values.md#43-integers) and the [decision record](../decisions/numbers.md#d42) |
| Stack size | The stack available to a running program; exhausting it stops the program with an implementation-limit report | [Diagnostics 10.6](diagnostics.md#106-runtime-reporting) |
| Array limits | The largest array extent and the largest total array size | [Arrays and shapes 7.2](arrays.md#72-dimension-rules) and [decision 11](../decisions/arrays.md#d11) |
| Source limits | The longest identifier and literal, and the deepest nesting of blocks and expressions | [Lexical structure](lexical-structure.md) |
| Diagnostics | Their layout, wording and notes, how the compiler recovers after an error, and the most errors it reports before stopping | [Diagnostics 10.1](diagnostics.md#101-required-diagnostic-data) and [10.5](diagnostics.md#105-recovery) |
| Warnings | Which warnings exist, if any, and their wording | [Diagnostics 10.1](diagnostics.md#101-required-diagnostic-data) |
| Runtime error messages | The message text of a runtime error report | [Diagnostics 10.6](diagnostics.md#106-runtime-reporting) |
| Data layout | The sizes and alignment of all types, and the padding inside and after struct fields | [Arrays and shapes 7.8](arrays.md#78-memory-and-layout), [Structs 8.8](structs.md#88-layout) and [decision 43](../decisions/arrays.md#d43) |
| Targets | The supported targets and the back end; neither may change a program's meaning | [1.6](#16-implementation-defined-behavior) |

<a class="vx-rule" id="conform.limits.definition" href="#conform.limits.definition">conform.limits.definition</a> A limit is an implementation limit (1.3): a program within every documented
limit must be accepted, and a program that exceeds one must be diagnosed, never
miscompiled.

The reasons are in [record 55](../decisions/documentation.md#d55).

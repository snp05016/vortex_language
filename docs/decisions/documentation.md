# How the documents work

These records settle how the documentation itself works: how examples are
labelled and tested, which page decides when two disagree, what "Planned" and
"Static error" mean, how the cheat sheet shows literal storage, ten wording
fixes, and the list of behavior an implementation may choose. The
specification chapters now state the rules; each record below explains why the
rule was chosen and what it changed.

## 28. Example labels {#d28}

**Status:** Accepted, 2026-09-23. **Applies to:** v0.1. **Resolves:** N25, N26.
**Guide stage:** 0, 11.

**Question.** How can a reader, or a test, tell what a `vortex` block is and
what a compiler must do with it?

**Before this decision.** [Specification examples](../specification/conformance.md#18-specification-examples)
defined five labels, but few blocks used them. Several blocks shown as valid
put a `let` beside `fn` declarations, which the
[grammar](../specification/grammar.md#program-structure) rejects, and the
examples in [Diagnostics](../specification/diagnostics.md#104-examples) are
single statements, not programs. Yet
[stage 0](../compiler/guide/stage-0-workbench.md#why-the-tests-come-first)
called turning examples into tests "mechanical".

**Options.**

- Leave blocks unlabelled.
- Make every block a complete program.
- Label each block with its kind and expected result, and let a checker (a
  script that compiles each example as a test) complete it.

**Elsewhere.** rustdoc [tests the code blocks in Rust documentation][rs-doctest].
It wraps a block without `fn main` in `fn main() { ... }`, and marks blocks
that must not compile (`compile_fail`), must panic (`should_panic`) or are
incomplete (`ignore`).

**Decision.** The first line of every `vortex` block must be a label comment:
`// <kind>: <result>`, or `// fragment`.

- Kinds: `program`, a complete file; `items`, top-level declarations only, to
  which the checker appends `fn main() {}` when there is no `main`;
  `statements`, which the checker wraps in `fn main() { ... }`; and
  `fragment`, syntax shown on its own, never compiled and without a result.
- Results: `valid`, `lexical error`, `syntax error`, `name error`,
  `type error`, `semantic error`, `constant-evaluation error`, `runtime error`
  and `planned`.
- A block must not mix top-level declarations with top-level statements.
- A `valid` block must be accepted exactly as completed, so it declares every
  name it uses.
- An error block must contain mistakes of the named category only.
- A practice block with several answers is a `fragment`, so the label does not
  give them away.

**Why.** A label turns every example into a test with a known answer, and a
wrong example fails a check instead of misleading a reader.

**Consequences.** Every block gains a label, and mixed blocks are split or move
their statements into `main`:

```vortex
// statements: type error
let ready: bool = 10;   // an integer literal cannot initialize a bool
```

The compiler checks nothing new; a checker runs the examples at the release
gate ([I10](implementation.md#i10)). Pages changed: Conformance 1.8, every
page with examples, and guide stages 0 and 11, which describe testing them.

## 49. One statement of document authority {#d49}

**Status:** Accepted, 2026-09-23. **Applies to:** v0.1. **Resolves:** N4.
**Guide stage:** none.

**Question.** When two pages disagree, which one decides?

**Before this decision.** Five pages gave five answers.
[Document authority](../specification/conformance.md#11-document-authority)
ranked the specification above the grammar. The
[tour index](../language-tour/README.md) said the formal grammar wins. The
[docs README](../README.md) said the tour and the grammar define behavior, and
told implementers to confirm rules against the tour. The
[roadmap](../roadmap.md#milestone-9-runtime-safety) judged Milestone 9 against
the tour, and the [home page](../index.md#how-the-documents-relate) made the
grammar authoritative for syntax.

**Options.**

- Keep a ranked list of every document.
- Make the specification chapters, including the grammar, the only normative
  text (text that states requirements), and every other page informative
  (text that explains them).
- Make the tour normative, because most readers learn from it.

**Elsewhere.** No precedent is needed: this is a rule about this project's own
pages.

**Decision.** Conformance 1.1 is the only statement of document authority.
Every other page that mentions authority must link to it instead of stating
its own order.

- The specification chapters, including the grammar and the glossary, are
  normative.
- Every other page is informative, including the tour, the compiler guide and
  internals, the cheat sheet, the roadmap and these decision records: they
  explain the rules and must not add to them.
- When an informative page disagrees with a chapter, the chapter is right.
- When two chapters disagree, the grammar's productions decide which token
  sequences are valid syntax, and the other chapters decide everything else.
  Either kind of disagreement must be reported and fixed.
- The roadmap measures milestones against specification chapters.

**Why.** One source of truth. Readers still learn from the tour, but a sentence
in a tutorial can no longer change the language.

**Consequences.** No program changes meaning and no compiler stage changes.
Pages changed: Conformance 1.1; the tour index, the docs README, the home page,
the grammar's introduction and the authorship statement, which now link to
1.1; and the roadmap and guide stage 9, which measure Milestone 9 against the
specification.

## 50. What "Planned" means {#d50}

**Status:** Accepted, 2026-09-23. **Applies to:** v0.1. **Resolves:** N3.
**Guide stage:** 0, 11.

**Question.** Does "Planned" mean "not part of v0.1", or "part of v0.1 but not
built yet"?

**Before this decision.** [Specification examples](../specification/conformance.md#18-specification-examples)
used Planned for a design direction that is not part of v0.1, and
[stage 11](../compiler/guide/stage-11-release.md#compiling-every-example-in-the-documentation)
told the compiler to reject planned examples. The
[status vocabulary](https://github.com/snp05016/vortex_language/blob/main/docs/README.md#v01-status-vocabulary) in the docs README
used the same word for behavior that is "intended but not yet implemented".

**Options.**

- Keep the conformance meaning, and give the README's meaning a new name.
- Keep the README's meaning, and rename the conformance label.
- Let one word carry both meanings.

**Elsewhere.** No language precedent is needed. Test tools keep the second case
apart: LLVM's lit test runner reports a test that is known to fail as an
expected failure (XFAIL), as
[stage 0](../compiler/guide/stage-0-workbench.md#a-runner-that-has-failed-at-least-once)
describes.

**Decision.** "Planned" keeps its conformance meaning: not part of v0.1, a
direction for a later version. A v0.1 compiler must not accept a block labelled
`planned`, and any error category is correct. Behavior that is part of v0.1
but that the current compiler does not handle yet is "Specified, not yet
implemented". Its tests are marked as expected failures
([I9](implementation.md#i9)), so a test that starts passing is noticed.

**Why.** The two meanings need opposite tests. A planned example must stay
rejected. A specified feature must eventually be accepted, and until then its
failing test is expected rather than noise.

**Consequences.** No program changes meaning. A checker expects every
`planned` block to be rejected and does not check the category:

```vortex
// statements: planned
let values: Vector<f32> = Vector::new();   // no generics or `::` in v0.1
```

Pages changed: the README's status vocabulary, Conformance 1.8, and the
glossary, which gains "planned".

## 51. The "Static error" label {#d51}

**Status:** Accepted, 2026-09-23. **Applies to:** v0.1. **Resolves:** N2, with
[record 11](arrays.md#d11). **Guide stage:** 0, 11.

**Question.** The example label "Semantic error" and the diagnostic category
"semantic error" share a name. Which one does an example mean?

**Before this decision.** In [Specification examples](../specification/conformance.md#18-specification-examples),
the label meant "parses, then rejected by any later static phase".
[Diagnostics](../specification/diagnostics.md#semantic-error) uses the same
words for one of its categories: contextual rules, such as assigning to an
immutable variable. So [Arrays](../specification/arrays.md#72-dimension-rules)
section 7.2 and the
[grammar](../specification/grammar.md#parsing-versus-semantic-checking) called
a dimension that uses a parameter a "semantic error", while Diagnostics section
10.4 requires a constant-evaluation error for the same code. The tour used
other words again, such as "invalid during constant/type checking".

**Options.**

- Rename the label, and have each example name its exact category.
- Rename the category.
- Keep both names and explain the difference.

**Elsewhere.** rustdoc has [one `compile_fail` attribute][rs-doctest] for
every kind of compile error, so a Rust documentation test cannot tell a type
error from a name error. Vortex examples can, because the categories are part
of the specification.

**Decision.** The label for code that parses but must be rejected before it
runs is "Static error"; a static phase is one that runs before the program
does. The label groups four diagnostic categories: name, type, semantic and
constant-evaluation errors. "Static error" is not itself a category. Every
example must name its exact category in its [record 28](#d28) label, and the
words "semantic error" must refer only to the category that Diagnostics
defines.

**Why.** It removes the clash, and an exact category is something a test can
check.

**Consequences.** No program changes meaning, but some examples change their
stated result:

```vortex
// items: constant-evaluation error
fn make(size: usize) {
    let values: [f32; size] = [0.0; size];   // size is not a compile-time value
}
```

The compiler checks nothing new; test harnesses compare the exact category.
Pages changed: Conformance 1.8 and guide stage 11 rename the label, Arrays 7.2
and the grammar name the constant-evaluation error, the tour's answers name
categories, and the glossary defines "static error".

## 53. Literal storage in the cheat sheet {#d53}

**Status:** Accepted, 2026-09-23. **Applies to:** v0.1. **Resolves:** N21.
**Guide stage:** 2, 5.

**Question.** How should the cheat sheet's C++ illustration store a literal,
when a `char` literal can hold any Unicode character and a float literal gets
its type only after parsing?

**Before this decision.** The cheat sheet's
[`std::variant`](../language-and-compiler-cheatsheet.md#stdvariant) entry
stored a literal as `std::uint64_t`, `double`, `std::string`, `char` or `bool`.
A C++ `char` is one byte, so it cannot hold `'λ'`, which
[Lexical structure](../specification/lexical-structure.md#character-and-string-literals)
allows and [Types and values](../specification/types-and-values.md#46-characters-and-strings)
section 4.6 says is not a one-byte value. Storing a float literal as `double`
and later narrowing it to `f32` rounds twice, which can give a different `f32`
than rounding once.

**Options.**

- Keep the illustration and add a warning.
- Store a character as `char32_t`, and keep a number's spelling (its digits as
  written) until its type is known.

**Elsewhere.** In C++, an ordinary character literal
[must fit in one `char` code unit, while a `U'...'` literal is a `char32_t`][cpp-charlit]
holding the whole code point. [`std::strtof`][cpp-strtof] converts text
straight to `float`.

**Decision.** This record changes documentation only; the compiler's code
belongs to its owner. The cheat sheet's illustration must store a character
literal as `char32_t`, holding one Unicode scalar value (one character,
[record 15](lexical.md#d15)). A numeric literal must keep its spelling until
literal typing (the step that gives each literal its final type) is done, and
is then converted once: an `f32` with `std::strtof`, an `f64` with
`std::strtod`, and an integer with a range check against its type, so an
out-of-range literal is a type error at the literal
([record 32](numbers.md#d32)).

**Why.** A teaching page should not show a storage choice that loses characters
or rounds twice. Keeping the spelling costs nothing, because every token already
keeps it ([Lexical structure](../specification/lexical-structure.md#28-token-source-data)
section 2.8).

**Consequences.** No program changes meaning. These lines are valid, and the
float literal is rounded once, straight to `f32`:

```vortex
// statements: valid
let letter: char = 'λ';
let tenth: f32 = 0.1;
```

Stage 2 keeps each literal's spelling, as it already must, and stage 5
converts a number once its type is known. Pages changed: the cheat sheet's
`std::variant` entry and guide stage 5.

## 54. Ten wording corrections {#d54}

**Status:** Accepted, 2026-09-23. **Applies to:** v0.1. **Resolves:** N24.
**Guide stage:** none.

**Question.** Ten passages in the tour, the cheat sheet and the home page said
something the specification does not. What should they say?

**Before this decision.**

1. The [statements tour](../language-tour/09-statements.md) said statements
   end with `;` "when they are written on one line".
2. The [variables tour](../language-tour/04-variables-and-types.md#automatic-type-inference)
   said `mut` "stands for mutable type".
3. The [source rules tour](../language-tour/02-basic-source-rules.md#identifiers)
   said `let item-count = 2;` is "parsed as subtraction".
4. The [expressions tour](../language-tour/08-expressions.md#operator-precedence)
   precedence table left out prefix `&` and `&mut`, and said higher rows "run
   before" lower ones.
5. That tour and the
   [cheat sheet](../language-and-compiler-cheatsheet.md#expression-validity-at-a-glance)
   showed `Point { x: 1.0 }`, which lacks `y`, as valid.
6. The cheat sheet listed the type `[i32; 2 + 2]` among values.
7. The [declarations tour](../language-tour/10-declarations.md#parameter-declarations)
   described a C++ `std::vector<ParamDecl>`.
8. The [home page](../index.md#specification-chapters) promised nested
   structs, array-valued fields and warnings that the chapters do not cover.
9. Four tour sentences conflicted with records 3, 42, 39 and 30.
10. Two tour passages used vague category words.

**Options.** Correct each passage to match the specification, or change the
specification to match the passages.

**Elsewhere.** No precedent is needed: these are errors in this project's
prose.

**Decision.** Each passage must say what the specification says:

1. Simple statements always end with `;`, a statement that ends in a block
   takes none, and line breaks never matter
   ([Lexical structure 2.1](../specification/lexical-structure.md#21-whitespace-and-line-boundaries)).
2. `mut` makes the variable mutable and leaves its type alone
   ([References 9.3](../specification/references.md#93-local-mutability)).
3. `let item-count = 2;` is a syntax error at `-`.
4. The table adds `&` and `&mut`, and says higher rows bind more tightly;
   precedence decides grouping, not evaluation order
   ([record 38](statements.md#d38)).
5. The example becomes `Point { x: 1.0, y: 2.0 }`.
6. The row separates array values from the array type.
7. The paragraph moves out;
   [parser design](../compiler/parser-design.md#72-parameter-declaration)
   covers it.
8. Each summary lists what its chapter contains.
9. Records [3](names.md#d3), [42](numbers.md#d42), [39](diagnostics.md#d39)
   and [30](numbers.md#d30) fix those sentences.
10. [Record 51](#d51) fixes the category words.

**Why.** Most readers learn from these pages, so their mistakes spread
fastest.

**Consequences.** No rule or compiler stage changes. One example changes its
stated result:

```vortex
// statements: syntax error
let item-count = 2;   // a name cannot contain "-"
```

Pages changed: five tour chapters, the cheat sheet and the home page.

## 55. The list of implementation-defined behavior {#d55}

**Status:** Accepted, 2026-09-23. **Applies to:** v0.1. **Resolves:** N28, and
N13 with [record 42](numbers.md#d42). **Guide stage:** 11, with entries chosen
in stages 1, 6, 8 and 9.

**Question.** Where may one conforming implementation differ from another,
and where is that written down?

**Before this decision.** [Implementation-defined behavior](../specification/conformance.md#16-implementation-defined-behavior)
(behavior each implementation chooses and must document) was allowed only
where the specification "explicitly allows" it, and
[Implementation conformance](../specification/conformance.md#13-implementation-conformance)
asked for limits to be documented. No page listed either, although the `usize`
width, the stack size and error-message text were open.
[Stage 11](../compiler/guide/stage-11-release.md#writing-down-what-the-compiler-does)
asked each release to answer a list of open questions instead.

**Options.**

- Mark each choice where it occurs.
- Keep one list in the conformance chapter, linked to the chapters.
- Leave the choices unlisted.

**Elsewhere.** The Go specification marks where a compiler may choose, such as
ignoring a leading byte order mark, as ["Implementation restriction"][go-src]
notes. C++ separates
[implementation-defined behavior, which must be documented, from undefined behavior][cpp-ub],
which has no restrictions.

**Decision.** Conformance section 1.10 lists every implementation-defined
behavior and limit, and nothing outside it may vary:

- the width of `usize`, 64 bits on every v0.1 target
  ([record 42](numbers.md#d42));
- the stack size ([record 46](diagnostics.md#d46));
- the largest array extent and total array size;
- the longest identifier and literal, and the deepest nesting of blocks and
  expressions;
- diagnostics: their layout, wording and notes, how the compiler recovers
  after an error, and how many errors it reports ([I2](implementation.md#i2));
- which warnings exist ([record 48](diagnostics.md#d48));
- the message text of a runtime error ([I7](implementation.md#i7));
- the sizes, alignment and padding of arrays and structs
  ([record 43](arrays.md#d43));
- the targets and the back end ([I1](implementation.md#i1)).

An implementation must document its choice for each entry and apply it
consistently. Every other observable behavior is fully specified.

**Why.** A list makes Conformance 1.6 checkable: a reviewer compares an
implementation's documentation with the list, entry by entry.

**Consequences.** No program changes meaning, but a test that depends on an
entry, such as `usize` overflow, must say so. Each release documents its
choices (stage 11). Pages changed: Conformance 1.6 and the new 1.10, the
glossary, the home page and guide stage 11.

[rs-doctest]: https://doc.rust-lang.org/rustdoc/write-documentation/documentation-tests.html
[go-src]: https://go.dev/ref/spec#Source_code_representation
[cpp-ub]: https://en.cppreference.com/cpp/language/ub
[cpp-strtof]: https://en.cppreference.com/cpp/string/byte/strtof
[cpp-charlit]: https://en.cppreference.com/cpp/language/character_literal
